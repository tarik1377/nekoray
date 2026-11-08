package sitebook

import (
	"context"
	"encoding/base64"
	"net"
	"net/http"
	"net/netip"
	"net/url"
	"sync/atomic"
	"testing"
	"time"
)

func TestDiscoveryProxyDeniesUnauthenticatedAndPrivateDestinations(t *testing.T) {
	var dialed atomic.Int32
	proxy, err := NewScanProxy(context.Background(), "direct", func(context.Context, string, string, string) (net.Conn, error) {
		dialed.Add(1)
		return nil, context.Canceled
	}, func(context.Context, string) ([]netip.Addr, error) {
		return []netip.Addr{netip.MustParseAddr("10.1.2.3")}, nil
	}, false)
	if err != nil {
		t.Fatal(err)
	}
	defer proxy.Close()
	address, _ := url.Parse(proxy.Address)
	transport := &http.Transport{Proxy: http.ProxyURL(address)}
	defer transport.CloseIdleConnections()
	client := &http.Client{Transport: transport, Timeout: 2 * time.Second}
	for _, authenticated := range []bool{false, true} {
		request, _ := http.NewRequest("GET", "http://public.example.test/", nil)
		if authenticated {
			request.Header.Set("Proxy-Authorization", "Basic "+base64.StdEncoding.EncodeToString([]byte(proxy.User+":"+proxy.Password)))
		}
		response, err := client.Do(request)
		if err != nil {
			t.Fatal(err)
		}
		response.Body.Close()
		expected := 407
		if authenticated {
			expected = 502
		}
		if response.StatusCode != expected {
			t.Fatal(response.StatusCode, expected)
		}
	}
	if dialed.Load() != 0 {
		t.Fatal("scanner dialed private DNS destination")
	}
}
