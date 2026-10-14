//go:build with_gvisor

package main

import (
	"context"
	"fmt"
	"net/netip"
	"testing"

	"github.com/sagernet/gvisor/pkg/tcpip/stack"
	tun "github.com/sagernet/sing-tun"
)

// Constructors must never touch a real adapter. Embedded methods panic if used.
type constructorOnlyTun struct{ tun.Tun }

func (constructorOnlyTun) WritePacket(*stack.PacketBuffer) (int, error) { panic("unexpected I/O") }
func (constructorOnlyTun) NewEndpoint() (stack.LinkEndpoint, stack.NICOptions, error) {
	panic("unexpected adapter start")
}

var _ tun.GVisorTun = constructorOnlyTun{}

func TestTransformedSelectionCreatesActualStack(t *testing.T) {
	for _, test := range []struct{ selection, implementation string }{
		{"", "*tun.System"}, {"system", "*tun.System"},
		{"mixed", "*tun.Mixed"}, {"gvisor", "*tun.GVisor"},
	} {
		t.Run(test.selection, func(t *testing.T) {
			config := mustTransform(t, map[string]interface{}{
				"inbounds": []interface{}{map[string]interface{}{
					"type": "tun", "stack": test.selection, "inet4_address": "172.19.0.1/28",
					"inet6_address": "fdfe:dcba:9876::1/126",
				}},
			})
			inbound := config["inbounds"].([]interface{})[0].(map[string]interface{})
			value, err := tun.NewStack(inbound["stack"].(string), tun.StackOptions{
				Context: context.Background(), Tun: constructorOnlyTun{},
				TunOptions: tun.Options{
					Inet4Address: []netip.Prefix{netip.MustParsePrefix(inbound["address"].([]interface{})[0].(string))},
					Inet6Address: []netip.Prefix{netip.MustParsePrefix(inbound["address"].([]interface{})[1].(string))},
					MTU:          1500,
				},
			})
			if err != nil {
				t.Fatal(err)
			}
			if got := fmt.Sprintf("%T", value); got != test.implementation {
				t.Fatalf("selection %q constructed %s, wanted %s", test.selection, got, test.implementation)
			}
			// No Start: it would alter network state and create firewall exceptions.
		})
	}
}

func TestSystemConstraintsFailWithoutFallback(t *testing.T) {
	options := tun.StackOptions{Context: context.Background(), Tun: constructorOnlyTun{},
		TunOptions: tun.Options{Inet4Address: []netip.Prefix{netip.MustParsePrefix("172.19.0.1/32")}}}
	if _, err := tun.NewStack("system", options); err == nil {
		t.Fatal("System requires another address in its first prefix")
	}
	options.IncludeAllNetworks = true
	options.TunOptions.Inet4Address = []netip.Prefix{netip.MustParsePrefix("172.19.0.1/30")}
	if _, err := tun.NewStack("system", options); err == nil {
		t.Fatal("includeAllNetworks must fail explicitly, not substitute gVisor")
	}
	if _, err := tun.NewStack("invalid", options); err == nil {
		t.Fatal("invalid implementation must fail")
	}
}
