package main

import (
	"context"
	"encoding/json"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/log"
	M "github.com/sagernet/sing/common/metadata"
	"io"
	"nekobox_core/robloxauto"
	"nekobox_core/siteauto"
	"net"
	"net/netip"
	"strings"
	"testing"
	"time"
)

func TestSiteAutoRegisteredAndOptIn(t *testing.T) {
	config := map[string]any{"log": map[string]any{"disabled": true}, "outbounds": []any{
		map[string]any{"type": "direct", "tag": "bypass"}, map[string]any{"type": "direct", "tag": "proxy"},
		map[string]any{"type": "greenrhythm-site-auto", "tag": "site-auto", "direct": "bypass", "proxy": "proxy", "cache_scope": strings.Repeat("a", 64), "site_domains": []string{"domain:example.com"}},
	}, "route": map[string]any{"final": "proxy", "rules": []any{map[string]any{"domain_suffix": []string{"example.com"}, "network": "tcp", "port": []int{443}, "outbound": "site-auto"}}}}
	data, _ := json.Marshal(config)
	box, cancel, err := nekoCreate(data)
	if err != nil {
		t.Fatal(err)
	}
	defer cancel()
	defer box.Close()
	item, ok := box.Outbound().Outbound("site-auto")
	if !ok {
		t.Fatal("site outbound not registered")
	}
	auto := item.(*robloxAutoOutbound)
	if auto.sites == nil || auto.store != nil || !auto.known("a.example.com") || auto.known("chatgpt.com") || box.Outbound().Default().Tag() != "proxy" {
		t.Fatal("scope or default changed")
	}
	if _, err = newSiteAuto(context.Background(), nil, nil, "x", robloxAutoOptions{SiteDomains: []string{"domain:cloudfront.net"}}); err == nil {
		t.Fatal("unsafe catalog accepted")
	}
}
func TestSiteAutoPinnedStreamsAndProtocolIsolation(t *testing.T) {
	direct, proxy := ownedRobloxEcho(t, "D"), ownedRobloxEcho(t, "P")
	logger, err := log.New(log.Options{})
	if err != nil {
		t.Fatal(err)
	}
	engine := robloxauto.New(context.Background())
	t.Cleanup(engine.Close)
	catalog, _ := siteauto.New([]string{"domain:example.com"})
	auto := &robloxAutoOutbound{direct: direct, proxy: proxy, engine: engine, logger: logger.Logger(), sites: catalog}
	auto.refreshNetwork()
	generation := engine.Generation()
	restore := func(path robloxauto.Path) {
		engine.Restore(generation, map[string]robloxauto.Entry{"tcp:www.example.com": {Path: path, Verified: true, Expires: time.Now().Add(time.Minute)}})
	}
	restore(robloxauto.Direct)
	destination := M.ParseSocksaddr("www.example.com:443")
	selected, err := auto.SelectConnection(context.Background(), adapter.InboundContext{Network: "tcp", Destination: destination})
	if err != nil || selected.Tag() != direct.Tag() {
		t.Fatal("incorrect accounting", err)
	}
	restore(robloxauto.Proxy)
	held, err := selected.DialContext(context.Background(), "tcp", destination)
	if err != nil {
		t.Fatal(err)
	}
	defer held.Close()
	expect := func(conn net.Conn, want byte) {
		t.Helper()
		_ = conn.SetDeadline(time.Now().Add(time.Second))
		if _, err := conn.Write([]byte{1}); err != nil {
			t.Fatal(err)
		}
		var b [1]byte
		if _, err := io.ReadFull(conn, b[:]); err != nil || b[0] != want {
			t.Fatalf("want %c, got %q: %v", want, b, err)
		}
	}
	expect(held, 'D')
	next, err := auto.DialContext(context.Background(), "tcp", destination)
	if err != nil {
		t.Fatal(err)
	}
	defer next.Close()
	expect(next, 'P')
	expect(held, 'D')
	if auto.chooseTCP(context.Background(), M.ParseSocksaddr("unrelated.example.net:443")) != robloxauto.Proxy {
		t.Fatal("unlisted site was probed")
	}
	if auto.chooseTCP(context.Background(), M.ParseSocksaddr("www.example.com:8443")) != robloxauto.Proxy {
		t.Fatal("unconfigured service port was probed")
	}
	if path, _, key := auto.chooseUDP(context.Background(), destination); path != robloxauto.Proxy || key != "" {
		t.Fatal("website auto captured UDP learner")
	}
	if auto.chooseTCP(context.Background(), M.ParseSocksaddr("10.44.0.7:443")) != robloxauto.Direct {
		t.Fatal("private destination left local network")
	}
	privateContext := adapter.WithContext(context.Background(), &adapter.InboundContext{DestinationAddresses: []netip.Addr{netip.MustParseAddr("10.44.0.7")}})
	if auto.chooseTCP(privateContext, destination) != robloxauto.Direct {
		t.Fatal("split-DNS private destination was probed through VPN")
	}
}
