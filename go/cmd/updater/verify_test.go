package main

import (
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	releaseauth "greenrhythm_release"
)

func TestInstallerRejectsChecksumOnly(t *testing.T) {
	p := filepath.Join(t.TempDir(), "greenrhythm.zip")
	body := []byte("untrusted package")
	h := sha256.Sum256(body)
	os.WriteFile(p, body, 0644)
	os.WriteFile(p+".sha256", []byte(hex.EncodeToString(h[:])), 0644)
	if verifyArchive(p) == nil {
		t.Fatal("unsigned archive accepted")
	}
}

func TestInstallerVerifiesSignatureAndActualBytes(t *testing.T) {
	p := filepath.Join(t.TempDir(), "greenrhythm.zip")
	body := []byte("trusted package bytes")
	h := sha256.Sum256(body)
	pub, key, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	m := &releaseauth.Manifest{VersionCode: 1602, Version: "1.6.2", URL: "https://verdantvibe.ru/downloads/test.zip", SHA256: hex.EncodeToString(h[:]), SizeBytes: int64(len(body)), Platform: releaseauth.Platform(), Algorithm: releaseauth.Algorithm, KeyID: "test-key"}
	payload, err := releaseauth.Payload(m)
	if err != nil {
		t.Fatal(err)
	}
	m.Signature = base64.StdEncoding.EncodeToString(ed25519.Sign(key, payload))
	keys := map[string]string{"test-key": base64.StdEncoding.EncodeToString(pub)}
	verify := func(m *releaseauth.Manifest, platform string) error {
		return releaseauth.VerifyWithKeys(m, platform, keys)
	}
	encoded, _ := json.Marshal(m)
	os.WriteFile(p+".manifest.json", encoded, 0644)
	os.WriteFile(p, body, 0644)
	if err := verifyArchiveWith(p, releaseauth.Platform(), verify); err != nil {
		t.Fatal(err)
	}
	if verifyArchive(p) == nil {
		t.Fatal("test key accepted by production installer")
	}
	if verifyArchiveWith(p, "wrong-platform", verify) == nil {
		t.Fatal("wrong platform accepted")
	}
	os.WriteFile(p, []byte("tampered package byte"), 0644)
	if verifyArchiveWith(p, releaseauth.Platform(), verify) == nil {
		t.Fatal("tampered bytes accepted")
	}
	os.WriteFile(p, body, 0644)
	m.VersionCode++
	encoded, _ = json.Marshal(m)
	os.WriteFile(p+".manifest.json", encoded, 0644)
	if verifyArchiveWith(p, releaseauth.Platform(), verify) == nil {
		t.Fatal("tampered manifest accepted")
	}
	os.WriteFile(p+".manifest.json", append(encoded, []byte(" {}")...), 0644)
	if verifyArchiveWith(p, releaseauth.Platform(), verify) == nil {
		t.Fatal("trailing JSON accepted")
	}
}
