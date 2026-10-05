// sign runs locally on the release-signing machine. Never provision its key on a web server or CI runner.
package main

import (
	"crypto/ed25519"
	"crypto/sha256"
	"crypto/x509"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"

	releaseauth "greenrhythm_release"
)

func signedManifest(archive, platform, version string, code int64, mandatory bool, private ed25519.PrivateKey) (*releaseauth.Manifest, error) {
	if len(private) != ed25519.PrivateKeySize {
		return nil, errors.New("expected an Ed25519 private key")
	}
	name := filepath.Base(archive)
	if !regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._-]{0,119}$`).MatchString(name) {
		return nil, errors.New("unsafe archive filename")
	}
	f, err := os.Open(archive)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil {
		return nil, err
	}
	if !info.Mode().IsRegular() || info.Size() <= 0 || info.Size() > 1<<30 {
		return nil, errors.New("archive must be a regular file of 1 byte to 1 GiB")
	}
	hash := sha256.New()
	size, err := io.Copy(hash, io.LimitReader(f, (1<<30)+1))
	if err != nil {
		return nil, err
	}
	if size != info.Size() {
		return nil, errors.New("archive changed while signing")
	}
	public := private.Public().(ed25519.PublicKey)
	keyHash := sha256.Sum256(public)
	m := &releaseauth.Manifest{Platform: platform, Version: version, VersionCode: code, URL: "https://verdantvibe.ru/downloads/" + name,
		SHA256: hex.EncodeToString(hash.Sum(nil)), SizeBytes: size, Mandatory: mandatory, Algorithm: releaseauth.Algorithm, KeyID: "gr-" + hex.EncodeToString(keyHash[:8])}
	payload, err := releaseauth.Payload(m)
	if err != nil {
		return nil, err
	}
	m.Signature = base64.StdEncoding.EncodeToString(ed25519.Sign(private, payload))
	return m, nil
}

func run() error {
	keyPath := flag.String("key", "", "PKCS8 PEM private key outside repositories and servers")
	archive := flag.String("archive", "", "finished release archive")
	platform := flag.String("platform", "windows-x64", "exact target platform")
	version := flag.String("version", "", "dotted version, without v")
	code := flag.Int64("version-code", 0, "monotonically increasing release code")
	mandatory := flag.Bool("mandatory", false, "sign mandatory update flag")
	flag.Parse()
	if *keyPath == "" || *archive == "" {
		return errors.New("-key and -archive are required")
	}
	keyData, err := os.ReadFile(*keyPath)
	if err != nil {
		return err
	}
	block, rest := pem.Decode(keyData)
	if block == nil || block.Type != "PRIVATE KEY" || len(rest) > 0 {
		return errors.New("expected one PKCS8 PRIVATE KEY PEM block")
	}
	parsed, err := x509.ParsePKCS8PrivateKey(block.Bytes)
	if err != nil {
		return errors.New("invalid PKCS8 private key")
	}
	private, ok := parsed.(ed25519.PrivateKey)
	if !ok {
		return errors.New("expected an Ed25519 private key")
	}
	m, err := signedManifest(*archive, *platform, *version, *code, *mandatory, private)
	if err != nil {
		return err
	}
	// An unrelated local key must not produce a release that installed clients reject.
	if err := releaseauth.Verify(m, *platform); err != nil {
		return err
	}
	data, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		return err
	}
	out, err := os.OpenFile(*archive+".manifest.json", os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0644)
	if err != nil {
		return err
	}
	_, writeErr := out.Write(append(data, '\n'))
	closeErr := out.Close()
	if writeErr != nil {
		return writeErr
	}
	if closeErr != nil {
		return closeErr
	}
	fmt.Println("Signed manifest:", *archive+".manifest.json")
	return nil
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
