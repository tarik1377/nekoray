package main

import (
	"context"
	"encoding/json"
	"grpc_server/gen"
	"io"
	"net"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/adapter/outbound"
	M "github.com/sagernet/sing/common/metadata"
	"nekobox_core/sitebook"
)

func ownedSiteLibrary(mode string) string {
	return `{"schema":1,"sites":[{"id":"owned","title":"Owned","url":"https://www.example.com","mode":"` + mode + `","enabled":true,"endpoints":["https://www.example.com","https://one.cloudfront.net","https://203.0.113.9:8443"]}]}`
}
func TestSiteServicesLiveRulesPreserveStreamsAndPorts(t *testing.T) {
	// No inbound, TUN, system proxy, public DNS or production configuration.
	direct, proxy := ownedRobloxEcho(t, "D"), ownedRobloxEcho(t, "P")
	library := ownedSiteLibrary("direct")
	_, policies, err := sitebook.Parse(library)
	if err != nil {
		t.Fatal(err)
	}
	rules, err := serviceHeadlessRules(policies)
	if err != nil {
		t.Fatal(err)
	}
	config := map[string]any{"log": map[string]any{"disabled": true}, "outbounds": []any{
		map[string]any{"type": "direct", "tag": "bypass"}, map[string]any{"type": "direct", "tag": "proxy"},
		map[string]any{"type": siteServicesType, "tag": siteServicesTag, "direct": "bypass", "proxy": "proxy", "cache_scope": strings.Repeat("c", 64), "library": library}},
		"route": map[string]any{"final": "proxy", "rule_set": []any{map[string]any{"type": "inline", "tag": siteServicesTag, "rules": rules}}, "rules": []any{
			map[string]any{"process_name": []string{"Discord.exe"}, "outbound": "proxy"}, map[string]any{"process_name": []string{"dota2.exe"}, "outbound": "bypass"},
			map[string]any{"network": "tcp", "rule_set": []string{siteServicesTag}, "outbound": siteServicesTag}}}}
	data, _ := json.Marshal(config)
	box, cancel, err := nekoCreate(data)
	if err != nil {
		t.Fatal(err)
	}
	defer cancel()
	defer box.Close()
	item, _ := box.Outbound().Outbound(siteServicesTag)
	manager := item.(*siteServicesOutbound)
	previous := activeSiteServices.Swap(manager)
	defer activeSiteServices.Store(previous)
	response, _ := (&server{}).SiteTools(context.Background(), &gen.SiteToolsReq{Action: "status"})
	if response.Error != "" || !strings.Contains(response.ResultJson, "configuration_hash") {
		t.Fatal("actual scoped RPC status", response)
	}
	response, _ = (&server{}).SiteTools(context.Background(), &gen.SiteToolsReq{Action: "apply", PayloadJson: `{"scope":"wrong","library":"{}"}`})
	if response.Error != "profile_changed" {
		t.Fatal("RPC failed to fence stale profile", response)
	}
	response, _ = (&server{}).SiteTools(context.Background(), &gen.SiteToolsReq{Action: "scan", PayloadJson: `{"scope":"` + strings.Repeat("c", 64) + `","url":"http://127.0.0.1/"}`})
	if response.Error == "" {
		t.Fatal("RPC accepted private scan")
	}
	direct.Adapter = outbound.NewAdapter("owned", "bypass", []string{"tcp"}, nil)
	proxy.Adapter = outbound.NewAdapter("owned", "proxy", []string{"tcp"}, nil)
	manager.direct, manager.proxy = direct, proxy
	open := func(destination string) net.Conn {
		t.Helper()
		client, server := net.Pipe()
		go box.Router().RouteConnection(context.Background(), server, adapter.InboundContext{Network: "tcp", Destination: M.ParseSocksaddr(destination)})
		t.Cleanup(func() { client.Close() })
		return client
	}
	expect := func(connection net.Conn, want byte) {
		t.Helper()
		connection.SetDeadline(time.Now().Add(2 * time.Second))
		if _, err := connection.Write([]byte{1}); err != nil {
			t.Fatal(err)
		}
		var data [1]byte
		if _, err := io.ReadFull(connection, data[:]); err != nil || data[0] != want {
			t.Fatalf("route got %q %v, want %c", data, err, want)
		}
	}
	old := open("www.example.com:443")
	expect(old, 'D')
	// Exact public IP and custom port must match as an IP rule, not a domain.
	expect(open("203.0.113.9:8443"), 'D')
	if _, err = manager.apply(strings.Repeat("c", 64), ownedSiteLibrary("proxy"), ""); err != nil {
		t.Fatal(err)
	}
	expect(open("www.example.com:443"), 'P')
	expect(old, 'D')
	if _, err = manager.apply("stale", ownedSiteLibrary("direct"), ""); err == nil {
		t.Fatal("stale profile accepted")
	}
	expect(open("www.example.com:443"), 'P')
	before := manager.policy.Load().Active.Digest
	if _, err = manager.apply(strings.Repeat("c", 64), `{"schema":99}`, ""); err == nil || manager.policy.Load().Active.Digest != before {
		t.Fatal("invalid apply mutated policy")
	}
	for _, entry := range rules {
		raw, _ := json.Marshal(entry)
		if strings.Contains(string(raw), "203.0.113.9") && !strings.Contains(string(raw), "203.0.113.9/32") {
			t.Fatal("IP emitted as domain", string(raw))
		}
	}
	if _, ok := manager.policy.Load().Active.Items[sitebook.Key("www.example.com", 3333)]; ok {
		t.Fatal("web rule captures SSH")
	}
	// Concurrent selection/status while replacing the dedicated immutable matcher.
	var group sync.WaitGroup
	for i := 0; i < 4; i++ {
		group.Add(1)
		go func() {
			defer group.Done()
			for n := 0; n < 20; n++ {
				manager.choose(context.Background(), M.ParseSocksaddr("www.example.com:443"))
				manager.status()
			}
		}()
	}
	for n := 0; n < 10; n++ {
		mode := "direct"
		if n%2 == 1 {
			mode = "proxy"
		}
		if _, err = manager.apply(strings.Repeat("c", 64), ownedSiteLibrary(mode), ""); err != nil {
			t.Fatal(err)
		}
	}
	group.Wait()
	expect(old, 'D')
	if _, err = manager.apply(strings.Repeat("c", 64), ownedSiteLibrary("direct"), ""); err != nil {
		t.Fatal(err)
	}
	if _, err = manager.apply(strings.Repeat("c", 64), `{"schema":1,"sites":[]}`, ""); err != nil {
		t.Fatal(err)
	}
	if path, _, _ := manager.choose(context.Background(), M.ParseSocksaddr("www.example.com:443")); path != "direct" {
		t.Fatal("removed manual DIRECT rule changed an in-flight router selection", path)
	}
	expect(old, 'D')
}
