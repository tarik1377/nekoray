package releaseauth

import (
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"strings"
	"testing"
)

func signedForTest(t *testing.T) (*Manifest, map[string]string) {
	t.Helper()
	pub, key, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	m := &Manifest{VersionCode: 1602, Version: "1.6.2", URL: "https://verdantvibe.ru/downloads/test.zip", SHA256: strings.Repeat("a", 64), SizeBytes: 123, Platform: "windows-x64", Algorithm: Algorithm, KeyID: "test-key"}
	payload, err := Payload(m)
	if err != nil {
		t.Fatal(err)
	}
	m.Signature = base64.StdEncoding.EncodeToString(ed25519.Sign(key, payload))
	return m, map[string]string{"test-key": base64.StdEncoding.EncodeToString(pub)}
}

func TestValidSignatureAndPlatform(t *testing.T) {
	m, keys := signedForTest(t)
	if err := VerifyWithKeys(m, "windows-x64", keys); err != nil {
		t.Fatal(err)
	}
	if VerifyWithKeys(m, "macos-arm64", keys) == nil {
		t.Fatal("cross-platform package accepted")
	}
	if Verify(m, "windows-x64") == nil {
		t.Fatal("untrusted test signing key accepted by production trust store")
	}
}

func TestSignatureBindsEveryExecutableField(t *testing.T) {
	m, keys := signedForTest(t)
	mutations := map[string]func(*Manifest){
		"platform":  func(m *Manifest) { m.Platform = "linux-x64" },
		"version":   func(m *Manifest) { m.Version = "9.9.9" },
		"code":      func(m *Manifest) { m.VersionCode++ },
		"url":       func(m *Manifest) { m.URL = "https://verdantvibe.ru/downloads/other.zip" },
		"sha":       func(m *Manifest) { m.SHA256 = strings.Repeat("b", 64) },
		"size":      func(m *Manifest) { m.SizeBytes++ },
		"mandatory": func(m *Manifest) { m.Mandatory = true },
		"key":       func(m *Manifest) { m.KeyID = "other-key" },
		"algorithm": func(m *Manifest) { m.Algorithm = "none" },
		"signature": func(m *Manifest) { m.Signature = strings.Repeat("A", 86) + "==" },
	}
	for name, mutate := range mutations {
		t.Run(name, func(t *testing.T) {
			copy := *m
			mutate(&copy)
			if VerifyWithKeys(&copy, copy.Platform, keys) == nil {
				t.Fatal("tampered release accepted")
			}
		})
	}
}

func TestCanonicalPayload(t *testing.T) {
	m, _ := signedForTest(t)
	payload, err := Payload(m)
	if err != nil {
		t.Fatal(err)
	}
	want := "GreenRhythm release v1\ntest-key\nwindows-x64\n1602\n1.6.2\nhttps://verdantvibe.ru/downloads/test.zip\n" + strings.Repeat("a", 64) + "\n123\n0\n"
	if string(payload) != want {
		t.Fatal("canonical encoding differs from JS contract")
	}
	m.URL = "https://verdantvibe.ru:443/downloads/test.zip"
	if _, err := Payload(m); err == nil {
		t.Fatal("noncanonical origin accepted")
	}
}
