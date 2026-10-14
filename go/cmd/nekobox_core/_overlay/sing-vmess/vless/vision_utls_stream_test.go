// overlay-base: github.com/sagernet/sing-vmess v0.2.8-0.20250909125414-3aed155119a1
//go:build with_utls

package vless

import (
	"crypto/x509"
	"net"
	"testing"

	utls "github.com/metacubex/utls"
)

func TestVisionRealUTLSRecordBoundaries(t *testing.T) {
	testVisionTLSStream(t, func(conn net.Conn, roots *x509.CertPool) interface {
		net.Conn
		Handshake() error
	} {
		return utls.UClient(conn, &utls.Config{RootCAs: roots, ServerName: "stream.test", MinVersion: utls.VersionTLS13}, utls.HelloChrome_Auto)
	})
}
