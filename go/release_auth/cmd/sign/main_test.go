package main

import (
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"os"
	"path/filepath"
	"testing"

	releaseauth "greenrhythm_release"
)

func TestSignedArchiveMetadata(t *testing.T) {
	public, private, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	archive := filepath.Join(t.TempDir(), "GreenRhythm-test.zip")
	if err := os.WriteFile(archive, []byte("fixture archive bytes"), 0600); err != nil {
		t.Fatal(err)
	}
	m, err := signedManifest(archive, "windows-x64", "1.9.0", 1900, true, private)
	if err != nil {
		t.Fatal(err)
	}
	keys := map[string]string{m.KeyID: base64.StdEncoding.EncodeToString(public)}
	if err := releaseauth.VerifyWithKeys(m, "windows-x64", keys); err != nil {
		t.Fatal(err)
	}
	if m.SizeBytes != 21 || !m.Mandatory {
		t.Fatal("signed size or mandatory flag changed")
	}
	m.SizeBytes++
	if err := releaseauth.VerifyWithKeys(m, "windows-x64", keys); err == nil {
		t.Fatal("tampered size accepted")
	}
}

func TestSignerRejectsUnusableArchive(t *testing.T) {
	_, private, _ := ed25519.GenerateKey(rand.Reader)
	if _, err := signedManifest(t.TempDir(), "windows-x64", "1.9.0", 1900, false, private); err == nil {
		t.Fatal("directory accepted")
	}
	if _, err := signedManifest("unused", "windows-x64", "1.9.0", 1900, false, nil); err == nil {
		t.Fatal("missing key accepted")
	}
}
