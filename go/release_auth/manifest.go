// Package releaseauth verifies releases against keys pinned in the binary.
// The web server never receives the private release-signing key.
package releaseauth

import (
	"crypto/ed25519"
	_ "embed"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"regexp"
	"runtime"
	"strings"
	"unicode"
)

//go:embed trusted_keys.json
var pinnedKeysJSON []byte

const Algorithm = "ed25519-v1"

type Manifest struct {
	VersionCode int64  `json:"versionCode"`
	Version     string `json:"version"`
	URL         string `json:"url"`
	SHA256      string `json:"sha256"`
	SizeBytes   int64  `json:"sizeBytes"`
	Mandatory   bool   `json:"mandatory"`
	Platform    string `json:"platform"`
	Algorithm   string `json:"signatureAlgorithm"`
	KeyID       string `json:"signingKeyId"`
	Signature   string `json:"signature"`
}

var keyIDPattern = regexp.MustCompile(`^[a-z0-9-]{1,64}$`)
var versionPattern = regexp.MustCompile(`^\d+\.\d+(?:\.\d+)?$`)

func Platform() string {
	switch {
	case runtime.GOOS == "windows" && runtime.GOARCH == "amd64":
		return "windows-x64"
	case runtime.GOOS == "linux" && runtime.GOARCH == "amd64":
		return "linux-x64"
	case runtime.GOOS == "darwin" && (runtime.GOARCH == "amd64" || runtime.GOARCH == "arm64"):
		return "macos-" + runtime.GOARCH
	}
	return ""
}

// Payload uses exact UTF-8 bytes, LF separators and decimal integers.
func Payload(m *Manifest) ([]byte, error) {
	if m == nil {
		return nil, errors.New("манифест отсутствует")
	}
	u, err := url.Parse(m.URL)
	validPlatform := m.Platform == "windows-x64" || m.Platform == "linux-x64" || m.Platform == "macos-amd64" || m.Platform == "macos-arm64"
	hash, hashErr := hex.DecodeString(m.SHA256)
	if err != nil || !validPlatform || !keyIDPattern.MatchString(m.KeyID) || !versionPattern.MatchString(m.Version) ||
		m.VersionCode <= 0 || m.VersionCode > 2147483647 || m.SizeBytes <= 0 || m.SizeBytes > 1073741824 ||
		hashErr != nil || len(hash) != 32 || m.SHA256 != strings.ToLower(m.SHA256) ||
		u.Scheme != "https" || u.Host != "verdantvibe.ru" || u.User != nil || u.RawQuery != "" || u.Fragment != "" || !strings.HasPrefix(m.URL, "https://verdantvibe.ru/downloads/") ||
		strings.IndexFunc(m.URL, func(r rune) bool { return unicode.IsSpace(r) || r < 0x20 || r == 0x7f }) >= 0 {
		return nil, errors.New("некорректный подписываемый манифест выпуска")
	}
	mandatory := "0"
	if m.Mandatory {
		mandatory = "1"
	}
	return []byte(fmt.Sprintf("GreenRhythm release v1\n%s\n%s\n%d\n%s\n%s\n%s\n%d\n%s\n", m.KeyID, m.Platform, m.VersionCode, m.Version, m.URL, m.SHA256, m.SizeBytes, mandatory)), nil
}

func Verify(m *Manifest, platform string) error {
	var keys map[string]string
	if err := json.Unmarshal(pinnedKeysJSON, &keys); err != nil {
		return err
	}
	return VerifyWithKeys(m, platform, keys)
}

// VerifyWithKeys is also used for cross-language test vectors; production uses Verify.
func VerifyWithKeys(m *Manifest, platform string, keys map[string]string) error {
	if m == nil || platform == "" || m.Platform != platform || m.Algorithm != Algorithm {
		return errors.New("платформа или формат подписи не совпадает — обновление отменено")
	}
	payload, err := Payload(m)
	if err != nil {
		return err
	}
	key, err := base64.StdEncoding.Strict().DecodeString(keys[m.KeyID])
	if err != nil || len(key) != ed25519.PublicKeySize {
		return errors.New("ключ подписи выпуска неизвестен — обновление отменено")
	}
	sig, err := base64.StdEncoding.Strict().DecodeString(m.Signature)
	if err != nil || len(sig) != ed25519.SignatureSize || !ed25519.Verify(ed25519.PublicKey(key), payload, sig) {
		return errors.New("подпись выпуска не прошла проверку — обновление отменено")
	}
	return nil
}
