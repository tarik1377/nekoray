package main

import (
	"context"
	"encoding/json"
	"runtime"
	"testing"

	"github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/common/workvpn"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/include"
	"github.com/sagernet/sing-box/option"
	singjson "github.com/sagernet/sing/common/json"
	"github.com/sagernet/sing/service"
)

func TestWorkVPNOptInExtraction(t *testing.T) {
	for _, tc := range []struct {
		raw              string
		enabled, invalid bool
	}{
		{`{"route":{}}`, false, false}, {`{"windows_work_vpn":false}`, false, false}, {`{"windows_work_vpn":true,"dns":{"final":"original"}}`, true, false},
		{`{"windows_work_vpn":"true"}`, false, true}, {`{"windows_work_vpn":null}`, false, true},
	} {
		result, enabled, err := extractWorkVPNOption([]byte(tc.raw))
		if (err != nil) != tc.invalid || enabled != tc.enabled {
			t.Fatalf("%s: enabled=%v err=%v", tc.raw, enabled, err)
		}
		if err == nil {
			var object map[string]any
			if json.Unmarshal(result, &object) != nil {
				t.Fatal("invalid transformed JSON")
			}
			if _, exists := object["windows_work_vpn"]; exists {
				t.Fatal("core-only field leaked into sing-box options")
			}
		}
	}
}

func TestWorkVPNCoreConfigurationReadOnly(t *testing.T) {
	if runtime.GOOS != "windows" {
		t.Skip("Windows feature")
	}
	ctx := include.Context(context.Background())
	options, err := singjson.UnmarshalExtendedContext[option.Options](ctx, []byte(`{"inbounds":[{"type":"tun","interface_name":"Custom Office TUN","address":["172.19.0.1/28"],"auto_route":false}],"outbounds":[{"type":"direct","tag":"direct"}],"dns":{"servers":[{"type":"local","tag":"original"}],"final":"original"},"route":{"auto_detect_interface":true,"final":"direct","rules":[{"action":"sniff"},{"port":53,"action":"hijack-dns"},{"ip_cidr":["224.0.0.0/4"],"action":"reject"},{"outbound":"direct"}]}}`))
	if err != nil {
		t.Fatal(err)
	}
	if err = configureWorkVPN(ctx, &options); err != nil {
		t.Fatal(err)
	}
	if options.Route.Final != "direct" || options.DNS.Final != "original" || len(options.DNS.Servers) != 1 {
		t.Fatal("configured Internet/DNS policy replaced")
	}
	if len(options.Route.Rules) != 5 || !options.Route.Rules[3].DefaultOptions.WindowsWorkVPN {
		t.Fatal("DNS interception/guards/ordinary routing ordering changed")
	}
	if service.FromContext[*workvpn.Manager](ctx) == nil {
		t.Fatal("per-instance manager absent")
	}
	// Construction only: never starts the TUN or changes OS network state.
	instance, err := box.New(box.Options{Context: ctx, Options: options})
	if err != nil {
		t.Fatal(err)
	}
	_ = instance.Close()
}

func TestWorkVPNRejectsUnsafeBindingConfiguration(t *testing.T) {
	for _, route := range []*option.RouteOptions{nil, {}, {AutoDetectInterface: true, DefaultInterface: "forced NIC"}} {
		ctx := include.Context(context.Background())
		options := option.Options{Route: route}
		if configureWorkVPN(ctx, &options) == nil {
			t.Fatal("work VPN accepted unsafe default binding")
		}
	}
	ctx := include.Context(context.Background())
	options := option.Options{Route: &option.RouteOptions{AutoDetectInterface: true}, Outbounds: []option.Outbound{{Type: C.TypeDirect, Tag: workVPNOutboundTag, Options: &option.DirectOutboundOptions{}}}}
	if configureWorkVPN(ctx, &options) == nil {
		t.Fatal("reserved tag collision accepted")
	}
}

func TestWorkVPNCoreLoopbackOnlyStart(t *testing.T) {
	if runtime.GOOS != "windows" {
		t.Skip("Windows feature")
	}
	// Mirrors standalone run -c through nekoCreate, with no TUN/auto_route.
	instance, cancel, err := nekoCreate([]byte(`{"windows_work_vpn":true,"inbounds":[{"type":"mixed","listen":"127.0.0.1","listen_port":0}],"outbounds":[{"type":"direct","tag":"direct"}],"dns":{"servers":[{"type":"local","tag":"original"}],"final":"original"},"route":{"auto_detect_interface":true,"final":"direct"}}`))
	if err != nil {
		t.Fatal(err)
	}
	defer cancel()
	_ = instance.Close() // Existing v2ray-api shutdown may report an already closed listener.
}
