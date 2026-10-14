// overlay-base: github.com/sagernet/sing-vmess v0.2.8-0.20250909125414-3aed155119a1
package vless

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"io"
	"math/big"
	"net"
	"testing"
	"time"

	"github.com/sagernet/sing/common/logger"
)

func streamTLSCertificate(t *testing.T) (tls.Certificate, *x509.CertPool) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "stream.test"}, DNSNames: []string{"stream.test"}, NotBefore: time.Now().Add(-time.Hour), NotAfter: time.Now().Add(time.Hour), KeyUsage: x509.KeyUsageDigitalSignature, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}}
	der, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	keyDER, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	certPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})
	cert, err := tls.X509KeyPair(certPEM, pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: keyDER}))
	if err != nil {
		t.Fatal(err)
	}
	roots := x509.NewCertPool()
	roots.AppendCertsFromPEM(certPEM)
	return cert, roots
}

type streamTLSClientFactory func(net.Conn, *x509.CertPool) interface {
	net.Conn
	Handshake() error
}

func testVisionTLSStream(t *testing.T, factory streamTLSClientFactory) {
	for _, split := range []int{0, 1, 5, 16, 18, 20, 24} {
		t.Run(fmt.Sprint(split), func(t *testing.T) {
			cert, roots := streamTLSCertificate(t)
			a, b := net.Pipe()
			defer a.Close()
			defer b.Close()
			deadline := time.Now().Add(3 * time.Second)
			a.SetDeadline(deadline)
			b.SetDeadline(deadline)
			client := factory(a, roots)
			server := tls.Server(b, &tls.Config{Certificates: []tls.Certificate{cert}, MinVersion: tls.VersionTLS13})
			payload := bytes.Repeat([]byte("actual TLS stream "), 4000)
			var frames []byte
			for offset := 0; offset < len(payload); offset += 8192 {
				end := visionMin(offset+8192, len(payload))
				command := byte(commandPaddingContinue)
				if end == len(payload) {
					command = commandPaddingEnd
				}
				frames = append(frames, streamTestFrame(offset == 0, command, payload[offset:end], 9)...)
			}
			done := make(chan error, 1)
			go func() {
				if err := server.Handshake(); err != nil {
					done <- err
					return
				}
				if split > 0 {
					if _, err := server.Write(frames[:split]); err != nil {
						done <- err
						return
					}
				}
				_, err := server.Write(frames[split:])
				done <- err
			}()
			if err := client.Handshake(); err != nil {
				t.Fatal(err)
			}
			vision, err := NewVisionConn(client, client, streamTestUUID, logger.NOP())
			if err != nil {
				t.Fatal(err)
			}
			got := make([]byte, len(payload))
			if _, err := io.ReadFull(vision, got); err != nil {
				t.Fatal(err)
			}
			if !bytes.Equal(got, payload) {
				t.Fatal("real TLS records corrupt Vision stream")
			}
			if err := <-done; err != nil {
				t.Fatal(err)
			}
		})
	}
}

func TestVisionRealTLSRecordBoundaries(t *testing.T) {
	testVisionTLSStream(t, func(conn net.Conn, roots *x509.CertPool) interface {
		net.Conn
		Handshake() error
	} {
		return tls.Client(conn, &tls.Config{RootCAs: roots, ServerName: "stream.test", MinVersion: tls.VersionTLS13})
	})
}
