package main

import (
	"archive/zip"
	"os"
	"path/filepath"
	"testing"
)

func makeZip(t *testing.T, path string, entries map[string]string) {
	t.Helper()
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	zw := zip.NewWriter(f)
	for name, body := range entries {
		w, err := zw.Create(name)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := w.Write([]byte(body)); err != nil {
			t.Fatal(err)
		}
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}
}

// A Zip-Slip entry (../escape.txt) must be rejected and nothing written outside dst.
func TestExtractZipRejectsTraversal(t *testing.T) {
	dir := t.TempDir()
	zipPath := filepath.Join(dir, "evil.zip")
	makeZip(t, zipPath, map[string]string{"../escape.txt": "pwned"})

	dst := filepath.Join(dir, "out")
	if err := extractZip(zipPath, dst); err == nil {
		t.Fatal("expected error for path-traversal entry, got nil")
	}
	if _, err := os.Stat(filepath.Join(dir, "escape.txt")); err == nil {
		t.Fatal("traversal entry was written outside the destination directory")
	}
}

// A normal entry extracts to the expected location with its contents.
func TestExtractZipNormal(t *testing.T) {
	dir := t.TempDir()
	zipPath := filepath.Join(dir, "ok.zip")
	makeZip(t, zipPath, map[string]string{"GreenRhythm/app.txt": "hello"})

	dst := filepath.Join(dir, "out")
	if err := extractZip(zipPath, dst); err != nil {
		t.Fatalf("normal extract failed: %v", err)
	}
	b, err := os.ReadFile(filepath.Join(dst, "GreenRhythm", "app.txt"))
	if err != nil || string(b) != "hello" {
		t.Fatalf("expected extracted file 'hello', got %q (err %v)", b, err)
	}
}

func TestSafeJoin(t *testing.T) {
	dst := filepath.Join(t.TempDir(), "out")
	if safeJoin(dst, "../x") != "" {
		t.Error("../x should be rejected")
	}
	if safeJoin(dst, "a/b.txt") == "" {
		t.Error("a/b.txt should be allowed")
	}
}
