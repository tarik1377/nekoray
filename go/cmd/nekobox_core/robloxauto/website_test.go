package robloxauto

import (
	"context"
	"net"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestWebsiteProbeStrictTLSAndNoAccountReplay(t *testing.T) {
	roots, certificate := ownedTLS(t)
	var calls atomic.Int32
	server := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		if r.Method != "GET" || r.URL.Path != "/" || r.URL.RawQuery != "" || r.Header.Get("Cookie") != "" || r.Header.Get("Authorization") != "" {
			t.Error("account request replayed")
		}
		w.Header().Set("Location", "https://unrelated.example.net/secret")
		w.WriteHeader(http.StatusFound)
	}))
	server.TLS = roots.Clone()
	server.TLS.Certificates = append(server.TLS.Certificates, certificate)
	server.StartTLS()
	defer server.Close()
	dial := func(ctx context.Context, _ Path, _, _ string) (net.Conn, error) {
		var d net.Dialer
		return d.DialContext(ctx, "tcp", server.Listener.Addr().String())
	}
	urlFor := func(host string) string {
		if host != "users.roblox.com" {
			return ""
		}
		return "https://users.roblox.com/"
	}
	probe := policyProbe(dial, roots, urlFor)
	if probe(context.Background(), Direct, "users.roblox.com") != Working || calls.Load() != 1 {
		t.Fatal("probe failed or followed redirect")
	}
	if probe(context.Background(), Direct, "other.example.net") != Unknown || calls.Load() != 1 {
		t.Fatal("out-of-catalog probe")
	}
	if WebsiteProbe(dial, urlFor)(context.Background(), Proxy, "users.roblox.com") != Failed {
		t.Fatal("untrusted certificate accepted")
	}
}
func TestWebsiteCacheRestoresOnlyConfiguredVerifiedHosts(t *testing.T) {
	network, scope := strings.Repeat("b", 64), strings.Repeat("a", 64)
	store := &Store{Directory: filepath.Join(t.TempDir(), "sites"), Scope: scope, AllowHost: func(host string) bool { return host == "www.example.com" }}
	entries := map[string]Entry{
		"tcp:www.example.com":  {Path: Direct, Verified: true, Expires: time.Now().Add(time.Minute)},
		"tcp:users.roblox.com": {Path: Proxy, Verified: true, Expires: time.Now().Add(time.Minute)},
	}
	if err := store.Save(network, entries, true); err != nil {
		t.Fatal(err)
	}
	loaded := store.Load(network)
	if len(loaded) != 1 || loaded["tcp:www.example.com"].Path != Direct {
		t.Fatal("generic host forgotten or unrelated cache accepted", loaded)
	}
	if len(store.Load(strings.Repeat("c", 64))) != 0 {
		t.Fatal("network cache leakage")
	}
}
