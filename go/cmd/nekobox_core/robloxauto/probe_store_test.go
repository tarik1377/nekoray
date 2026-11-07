package robloxauto

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestCatalogAndHTTPClassification(t *testing.T) {
	for _, host := range []string{"apis.roblox.com", "atl4-128-116-2-3.roblox.com", "setup.rbxcdn.com", "d143j4fdqe1jki.cloudfront.net"} {
		if !KnownHost(host) {
			t.Error(host)
		}
	}
	for _, host := range []string{"evilroblox.com", "roblox.com.evil.example", "other.cloudfront.net", "127.0.0.1", "localhost", "users.roblox.com/secret?token=abc", "roblox.com:443"} {
		if KnownHost(host) || ProbeURL(host) != "" {
			t.Error("arbitrary probe", host)
		}
	}
	if !Local("127.0.0.1") || !Local("10.1.2.3") || !Local("fe80::1") || Local("128.116.1.2") {
		t.Fatal("local isolation")
	}
	for _, code := range []int{403, 429} {
		if Classify("users.roblox.com", code, "application/json", []byte(`{"errors":[]}`)) != Unknown {
			t.Fatal("permission/rate limit guessed as filtering")
		}
	}
	if Classify("users.roblox.com", 200, "text/html", []byte("<html>blocked</html>")) != Unknown || Classify("users.roblox.com", 200, "application/json", []byte(`{"id":1}`)) != Working {
		t.Fatal("HTTP200 alone accepted as an API success")
	}
	if Classify("apis.roblox.com", 401, "application/json", []byte(`{"errors":[{"code":0}]}`)) != Working {
		t.Fatal("legitimate unauthenticated API response")
	}
	if Classify("apis.roblox.com", 404, "text/html", []byte("block")) != Unknown {
		t.Fatal("unidentified response treated as healthy")
	}
}

func ownedTLS(t *testing.T) (*tls.Config, tls.Certificate) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	cert := &x509.Certificate{SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "owned test only"}, DNSNames: []string{"users.roblox.com"}, NotBefore: time.Now().Add(-time.Hour), NotAfter: time.Now().Add(time.Hour), KeyUsage: x509.KeyUsageDigitalSignature, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}}
	der, err := x509.CreateCertificate(rand.Reader, cert, cert, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	encodedKey, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	pair, err := tls.X509KeyPair(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: encodedKey}))
	if err != nil {
		t.Fatal(err)
	}
	leaf, err := x509.ParseCertificate(der)
	if err != nil {
		t.Fatal(err)
	}
	pool := x509.NewCertPool()
	pool.AddCert(leaf)
	return &tls.Config{RootCAs: pool, MinVersion: tls.VersionTLS12}, pair
}
func TestHTTPSProbeUsesOwnedDetoursWithoutCredentialsOrRedirects(t *testing.T) {
	config, certificate := ownedTLS(t)
	var requests atomic.Int32
	var response atomic.Int32
	response.Store(200)
	server := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests.Add(1)
		if r.Method != "GET" || r.URL.Path != "/v1/users/1" || r.URL.RawQuery != "" || r.Header.Get("Cookie") != "" || r.Header.Get("Authorization") != "" {
			t.Error("application data in probe", r.Method, r.URL)
		}
		if response.Load() == 302 {
			w.Header().Set("Location", "https://not-followed.invalid/private")
		}
		w.WriteHeader(int(response.Load()))
		_, _ = w.Write([]byte(`{"id":1}`))
	}))
	server.TLS = &tls.Config{Certificates: []tls.Certificate{certificate}}
	server.StartTLS()
	defer server.Close()
	dial := func(ctx context.Context, _ Path, network, address string) (net.Conn, error) {
		if address != "users.roblox.com:443" {
			t.Errorf("unexpected address %s", address)
		}
		var dialer net.Dialer
		return dialer.DialContext(ctx, network, server.Listener.Addr().String())
	}
	probe := httpsProbe(dial, config)
	if probe(context.Background(), Direct, "users.roblox.com") != Working {
		t.Fatal("owned valid API unreachable")
	}
	response.Store(302)
	if probe(context.Background(), Proxy, "users.roblox.com") != Working || requests.Load() != 2 {
		t.Fatal("redirect followed")
	}
	// The production probe must NOT trust our test root or bypass TLS errors.
	if HTTPSProbe(dial)(context.Background(), Direct, "users.roblox.com") != Failed {
		t.Fatal("untrusted TLS accepted")
	}
}
func TestPersistentCacheBoundToProfileNetworkAndTTL(t *testing.T) {
	dir := t.TempDir()
	scope := strings.Repeat("a", 64)
	network := strings.Repeat("b", 64)
	s := &Store{Directory: filepath.Join(dir, "cache"), Scope: scope}
	entries := map[string]Entry{"tcp:users.roblox.com": {Path: Direct, Verified: true, Expires: time.Now().Add(time.Minute), Reason: "owned_fixture"}}
	sentinel := filepath.Join(dir, "routes")
	if err := os.WriteFile(sentinel, []byte("user routes"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := s.Save(network, entries, true); err != nil {
		t.Fatal(err)
	}
	if got := s.Load(network); got["tcp:users.roblox.com"].Path != Direct {
		t.Fatal("cache did not survive a new reader", got)
	}
	other := &Store{Directory: s.Directory, Scope: strings.Repeat("c", 64)}
	if len(other.Load(network)) != 0 || len(s.Load(strings.Repeat("d", 64))) != 0 {
		t.Fatal("cache crossed a profile/network boundary")
	}
	entries["tcp:users.roblox.com"] = Entry{Path: Direct, Verified: true, Expires: time.Now().Add(-time.Second)}
	if err := s.Save(network, entries, true); err != nil {
		t.Fatal(err)
	}
	if len(s.Load(network)) != 0 {
		t.Fatal("expired cache accepted")
	}
	if err := os.WriteFile(s.path(network), []byte("broken JSON"), 0600); err != nil {
		t.Fatal(err)
	}
	if len(s.Load(network)) != 0 {
		t.Fatal("broken cache accepted")
	}
	bad := &Store{Directory: sentinel, Scope: scope}
	if bad.Save(network, entries, true) == nil {
		t.Fatal("wrote through a non-directory")
	}
	if bytes, err := os.ReadFile(sentinel); err != nil || string(bytes) != "user routes" {
		t.Fatal("unrelated file changed")
	}
}
func TestPacketFlowIsNotMigratedOrReplayed(t *testing.T) {
	server, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	var count atomic.Int32
	go func() {
		buf := make([]byte, 64)
		for {
			n, peer, err := server.ReadFrom(buf)
			if err != nil {
				return
			}
			count.Add(1)
			_, _ = server.WriteTo(buf[:n], peer)
		}
	}()
	conn, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	replies := make(chan bool, 4)
	packet := TrackPacket(conn, 25*time.Millisecond, func(reply bool) { replies <- reply })
	defer packet.Close()
	if _, err = packet.WriteTo([]byte("first"), server.LocalAddr()); err != nil {
		t.Fatal(err)
	}
	select {
	case ok := <-replies:
		if ok {
			t.Fatal("reply before client read")
		}
	case <-time.After(time.Second):
		t.Fatal("no timeout observation")
	}
	// A late reply repairs the observation without killing the same socket.
	_ = packet.SetReadDeadline(time.Now().Add(time.Second))
	buf := make([]byte, 64)
	n, _, err := packet.ReadFrom(buf)
	if err != nil || string(buf[:n]) != "first" {
		t.Fatal("flow closed or payload changed", err)
	}
	if !<-replies {
		t.Fatal("late response was ignored")
	}
	_, _ = packet.WriteTo([]byte("second"), server.LocalAddr())
	n, _, err = packet.ReadFrom(buf)
	if err != nil || string(buf[:n]) != "second" || count.Load() != 2 {
		t.Fatal("connection moved or packets duplicated", count.Load(), err)
	}
}

func TestFailedStreamExpiresChoiceWithoutReplaying(t *testing.T) {
	client, server := net.Pipe()
	defer server.Close()
	var invalidations, received atomic.Int32
	go func() { b := make([]byte, 128); n, _ := server.Read(b); received.Add(int32(n)); server.Close() }()
	conn := TrackTCP(client, time.Second, func() { invalidations.Add(1) })
	defer conn.Close()
	_, err := conn.Write([]byte("one-owned-request"))
	if err != nil {
		t.Fatal(err)
	}
	_, _ = conn.Read(make([]byte, 128))
	if invalidations.Load() != 1 || received.Load() != int32(len("one-owned-request")) {
		t.Fatal("failure not observed once or payload replayed")
	}
	other, peer := net.Pipe()
	var aborted atomic.Int32
	wrapped := TrackTCP(other, 10*time.Millisecond, func() { aborted.Add(1) })
	wrapped.Close()
	peer.Close()
	if aborted.Load() != 0 {
		t.Fatal("intentional close invalidated routing")
	}
}
