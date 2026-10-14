//go:build with_utls

// overlay-base: github.com/sagernet/sing-box v1.13.18
package tls

import (
	"crypto/ed25519"
	"crypto/hmac"
	"crypto/sha512"
	"crypto/x509"
	"encoding/asn1"
	"encoding/base64"

	"github.com/cloudflare/circl/sign/mldsa/mldsa65"
	E "github.com/sagernet/sing/common/exceptions"
)

func realityMLDSA65PublicKey(value string) (*mldsa65.PublicKey, error) {
	if value == "" {
		return nil, nil
	}
	raw, err := base64.RawURLEncoding.DecodeString(value)
	if err != nil {
		return nil, E.New("reality: invalid mldsa65_verify encoding")
	}
	if len(raw) != mldsa65.PublicKeySize {
		return nil, E.New("reality: invalid mldsa65_verify size: ", len(raw), ", expected ", mldsa65.PublicKeySize)
	}
	key, err := mldsa65.Scheme().UnmarshalBinaryPublicKey(raw)
	if err != nil {
		return nil, E.New("reality: invalid mldsa65_verify key")
	}
	return key.(*mldsa65.PublicKey), nil
}

// Matches XTLS/Xray-core v26.9.9 transport/internet/reality/reality.go and
// XTLS/REALITY 8cdf7bf9c7f0 handshake_server_tls13.go. The server signs
// HMAC-SHA512(authKey, Ed25519 public key || wire ClientHello || wire ServerHello)
// with an empty ML-DSA context and stores it in certificate extension OID 0.0.
// Neither the X.509 signature nor a reconstructed TLS message is that transcript.
func realityVerifyCertificate(cert *x509.Certificate, authKey []byte, pqKey *mldsa65.PublicKey, clientHello, serverHello []byte) (bool, error) {
	pub, ok := cert.PublicKey.(ed25519.PublicKey)
	if !ok {
		return false, nil
	}
	h := hmac.New(sha512.New, authKey)
	h.Write(pub)
	if !hmac.Equal(h.Sum(nil), cert.Signature) {
		return false, nil
	}
	if pqKey == nil {
		return true, nil
	}
	if len(clientHello) == 0 || len(serverHello) == 0 {
		return false, E.New("reality: missing handshake transcript for ML-DSA-65")
	}
	var signature []byte
	found := false
	for _, extension := range cert.Extensions {
		if extension.Id.Equal(asn1.ObjectIdentifier{0, 0}) {
			if found {
				return false, E.New("reality: duplicate ML-DSA-65 certificate extension")
			}
			found = true
			signature = extension.Value
		}
	}
	if len(signature) != mldsa65.SignatureSize {
		return false, E.New("reality: missing or invalid ML-DSA-65 certificate signature")
	}
	h.Write(clientHello)
	h.Write(serverHello)
	if !mldsa65.Verify(pqKey, h.Sum(nil), nil, signature) {
		return false, E.New("reality: ML-DSA-65 certificate verification failed")
	}
	return true, nil
}
