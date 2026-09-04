package main

import (
	"crypto/sha256"
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Набор стоит перед одной ошибкой: применить не тот пакет. Каждый случай здесь —
// это форма, в которой «не тот» уже приходил или может прийти.

func writeArchive(t *testing.T, dir, name string, body []byte) string {
	t.Helper()
	p := filepath.Join(dir, name)
	if err := os.WriteFile(p, body, 0644); err != nil {
		t.Fatal(err)
	}
	return p
}

func sumOf(body []byte) string {
	h := sha256.Sum256(body)
	return hex.EncodeToString(h[:])
}

func TestVerifyArchiveAcceptsMatchingSum(t *testing.T) {
	dir := t.TempDir()
	body := []byte("это содержимое пакета")
	p := writeArchive(t, dir, "greenrhythm.zip", body)
	if err := os.WriteFile(p+".sha256", []byte(sumOf(body)), 0644); err != nil {
		t.Fatal(err)
	}
	if err := verifyArchive(p); err != nil {
		t.Fatalf("совпадающая сумма должна приниматься, получено: %v", err)
	}
}

func TestVerifyArchiveAcceptsSha256sumFormat(t *testing.T) {
	// Человек может положить файл, полученный обычным sha256sum: «<сумма>  <имя>».
	dir := t.TempDir()
	body := []byte("пакет")
	p := writeArchive(t, dir, "greenrhythm.zip", body)
	line := sumOf(body) + "  greenrhythm.zip\n"
	if err := os.WriteFile(p+".sha256", []byte(line), 0644); err != nil {
		t.Fatal(err)
	}
	if err := verifyArchive(p); err != nil {
		t.Fatalf("форма sha256sum должна приниматься, получено: %v", err)
	}
}

func TestVerifyArchiveRejectsWrongSum(t *testing.T) {
	dir := t.TempDir()
	p := writeArchive(t, dir, "greenrhythm.zip", []byte("настоящий пакет"))
	if err := os.WriteFile(p+".sha256", []byte(sumOf([]byte("другой пакет"))), 0644); err != nil {
		t.Fatal(err)
	}
	err := verifyArchive(p)
	if err == nil {
		t.Fatal("несовпадающая сумма должна отвергаться")
	}
	if !strings.Contains(err.Error(), "не сошлась") {
		t.Fatalf("причина отказа должна быть названа, получено: %v", err)
	}
}

func TestVerifyArchiveRejectsMissingSidecar(t *testing.T) {
	// Ровно тот случай, ради которого набор и написан: архив положили рядом с
	// программой, а нашим путём он не приходил.
	dir := t.TempDir()
	p := writeArchive(t, dir, "greenrhythm.zip", []byte("подложенный пакет"))
	if err := verifyArchive(p); err == nil {
		t.Fatal("пакет без файла суммы применять нельзя")
	}
}

func TestVerifyArchiveRejectsTruncatedSum(t *testing.T) {
	dir := t.TempDir()
	body := []byte("пакет")
	p := writeArchive(t, dir, "greenrhythm.zip", body)
	// Обрезанная сумма не должна проходить как «совпала по началу».
	if err := os.WriteFile(p+".sha256", []byte(sumOf(body)[:32]), 0644); err != nil {
		t.Fatal(err)
	}
	if err := verifyArchive(p); err == nil {
		t.Fatal("сумма неверной длины должна отвергаться")
	}
}

func TestVerifyArchiveRejectsMissingArchive(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "greenrhythm.zip")
	if err := os.WriteFile(p+".sha256", []byte(sumOf([]byte("x"))), 0644); err != nil {
		t.Fatal(err)
	}
	if err := verifyArchive(p); err == nil {
		t.Fatal("отсутствующий архив должен давать ошибку, а не проходить")
	}
}
