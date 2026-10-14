//go:build with_utls

// overlay-base: github.com/sagernet/sing-box v1.13.18
package tls

import (
	"bytes"
	"context"
	"crypto/ed25519"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha512"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/asn1"
	"encoding/base64"
	"math/big"
	"testing"

	"github.com/cloudflare/circl/sign/mldsa/mldsa65"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/logger"
)

func TestRealityMLDSA65Certificate(t *testing.T) {
	pqPub, pqPriv, err := mldsa65.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	edPub, edPriv, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	clientHello := []byte("wire ClientHello including encrypted session ID")
	serverHello := []byte("wire ServerHello including selected key share")
	authKey := bytes.Repeat([]byte{0x42}, 32)
	h := hmac.New(sha512.New, authKey)
	h.Write(edPub)
	hmacSignature := h.Sum(nil)
	h.Write(clientHello)
	h.Write(serverHello)
	signature := make([]byte, mldsa65.SignatureSize)
	if err := mldsa65.SignTo(pqPriv, h.Sum(nil), nil, false, signature); err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{SerialNumber: big.NewInt(0), ExtraExtensions: []pkix.Extension{{Id: asn1.ObjectIdentifier{0, 0}, Value: signature}}}
	der, err := x509.CreateCertificate(rand.Reader, template, template, edPub, edPriv)
	if err != nil {
		t.Fatal(err)
	}
	// XTLS/REALITY replaces the certificate's Ed25519 signature with this HMAC.
	copy(der[len(der)-64:], hmacSignature)
	cert, err := x509.ParseCertificate(der)
	if err != nil {
		t.Fatal(err)
	}
	tests := []struct {
		name   string
		mutate func(*x509.Certificate, *[]byte, *[]byte)
		pq     *mldsa65.PublicKey
		want   bool
	}{
		{"valid", nil, pqPub, true},
		{"ordinary-reality", nil, nil, true},
		{"altered-signature", func(c *x509.Certificate, _, _ *[]byte) { c.Extensions[0].Value[0] ^= 1 }, pqPub, false},
		{"truncated-signature", func(c *x509.Certificate, _, _ *[]byte) { c.Extensions[0].Value = c.Extensions[0].Value[:100] }, pqPub, false},
		{"missing-signature", func(c *x509.Certificate, _, _ *[]byte) { c.Extensions = nil }, pqPub, false},
		{"wrong-oid", func(c *x509.Certificate, _, _ *[]byte) { c.Extensions[0].Id = asn1.ObjectIdentifier{1, 2, 3} }, pqPub, false},
		{"duplicate-signature", func(c *x509.Certificate, _, _ *[]byte) { c.Extensions = append(c.Extensions, c.Extensions[0]) }, pqPub, false},
		{"altered-client-hello", func(_ *x509.Certificate, ch, _ *[]byte) { (*ch)[0] ^= 1 }, pqPub, false},
		{"altered-server-hello", func(_ *x509.Certificate, _, sh *[]byte) { (*sh)[0] ^= 1 }, pqPub, false},
		{"missing-transcript", func(_ *x509.Certificate, ch, _ *[]byte) { *ch = nil }, pqPub, false},
		{"altered-hmac", func(c *x509.Certificate, _, _ *[]byte) { c.Signature[0] ^= 1 }, pqPub, false},
	}
	wrongPub, _, err := mldsa65.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	tests = append(tests, struct {
		name   string
		mutate func(*x509.Certificate, *[]byte, *[]byte)
		pq     *mldsa65.PublicKey
		want   bool
	}{"wrong-public-key", nil, wrongPub, false})
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			fresh, err := x509.ParseCertificate(bytes.Clone(der))
			if err != nil {
				t.Fatal(err)
			}
			ch, sh := bytes.Clone(clientHello), bytes.Clone(serverHello)
			if tc.mutate != nil {
				tc.mutate(fresh, &ch, &sh)
			}
			got, err := realityVerifyCertificate(fresh, authKey, tc.pq, ch, sh)
			if got != tc.want || (tc.want && err != nil) {
				t.Fatalf("verified=%v error=%v, want %v", got, err, tc.want)
			}
		})
	}
	if ok, err := realityVerifyCertificate(cert, authKey, pqPub, clientHello, serverHello); !ok || err != nil {
		t.Fatalf("fixture mutated: %v %v", ok, err)
	}
}

func TestRealityMLDSA65Configuration(t *testing.T) {
	pub, _, err := mldsa65.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	raw, err := pub.MarshalBinary()
	if err != nil {
		t.Fatal(err)
	}
	encoded := base64.RawURLEncoding.EncodeToString(raw)
	for _, value := range []string{"!not-base64", base64.RawURLEncoding.EncodeToString(raw[:len(raw)-1]), base64.RawURLEncoding.EncodeToString(append(raw, 0))} {
		if _, err := realityMLDSA65PublicKey(value); err == nil {
			t.Fatal("accepted invalid public key")
		}
	}
	options := option.OutboundTLSOptions{Enabled: true, ServerName: "example.test", UTLS: &option.OutboundUTLSOptions{Enabled: true, Fingerprint: "firefox"}, Reality: &option.OutboundRealityOptions{Enabled: true, PublicKey: base64.RawURLEncoding.EncodeToString(make([]byte, 32)), MLDSA65Verify: encoded}}
	config, err := NewRealityClient(context.Background(), logger.NOP(), "example.test", options)
	if err != nil {
		t.Fatal(err)
	}
	cloned := config.Clone().(*RealityClientConfig)
	if cloned.mlDSA65Verify == nil || !cloned.mlDSA65Verify.Equal(pub) {
		t.Fatal("clone lost ML-DSA-65 verification")
	}
	options.Reality.MLDSA65Verify = "!invalid"
	if _, err := NewRealityClient(context.Background(), logger.NOP(), "example.test", options); err == nil {
		t.Fatal("configuration accepted malformed pqv")
	}
}
