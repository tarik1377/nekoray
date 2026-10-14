// overlay-base: github.com/sagernet/sing-box v1.13.18
package rule

import (
	"context"
	"net/netip"
	"testing"

	mDNS "github.com/miekg/dns"
	"github.com/sagernet/sing-box/adapter"
	M "github.com/sagernet/sing/common/metadata"
)

type testWorkInterface struct{}

type testNoWorkInterface struct{ testWorkInterface }

func (testNoWorkInterface) HasNetworks() bool { return false }

func (testWorkInterface) HasScope(string) bool { return false }
func (testWorkInterface) Interface(ip netip.Addr) (string, int, bool) {
	return "Office", 7, netip.MustParsePrefix("203.0.113.0/24").Contains(ip)
}

type testWorkDNSRouter struct {
	ips   []netip.Addr
	calls int
}

func (r *testWorkDNSRouter) Lookup(context.Context, string, adapter.DNSQueryOptions) ([]netip.Addr, error) {
	r.calls++
	return r.ips, nil
}
func (*testWorkDNSRouter) Exchange(context.Context, *mDNS.Msg, adapter.DNSQueryOptions) (*mDNS.Msg, error) {
	return nil, nil
}
func (*testWorkDNSRouter) Start(adapter.StartStage) error                 { return nil }
func (*testWorkDNSRouter) Close() error                                   { return nil }
func (*testWorkDNSRouter) ClearCache()                                    {}
func (*testWorkDNSRouter) ResetNetwork()                                  {}
func (*testWorkDNSRouter) LookupReverseMapping(netip.Addr) (string, bool) { return "", false }

func TestWorkVPNRouteLeavesPublicTrafficOnConfiguredOutbound(t *testing.T) {
	dns := &testWorkDNSRouter{ips: []netip.Addr{netip.MustParseAddr("198.51.100.7")}}
	r := &WorkVPNItem{context.Background(), testWorkInterface{}, dns}
	m := &adapter.InboundContext{Destination: M.Socksaddr{Fqdn: "public.example", Port: 443}}
	if r.Match(m) || len(m.DestinationAddresses) != 0 || dns.calls != 1 {
		t.Fatal("public domain was redirected to work outbound")
	}
	m.Destination = M.Socksaddr{Addr: netip.MustParseAddr("198.51.100.7"), Port: 443}
	if r.Match(m) || dns.calls != 1 {
		t.Fatal("public IP used work outbound/DNS")
	}
	dns.ips = []netip.Addr{netip.MustParseAddr("203.0.113.21")}
	m.Destination = M.Socksaddr{Fqdn: "unknown-work.example", Port: 443}
	if !r.Match(m) || len(m.DestinationAddresses) != 1 {
		t.Fatal("unknown domain in explicit public VPN prefix ignored")
	}
	m.Destination = M.Socksaddr{Addr: netip.MustParseAddr("203.0.113.21"), Port: 443}
	if !r.Match(m) {
		t.Fatal("specific public VPN route ignored")
	}
}

func TestWorkVPNRouteWithoutWorkNetworksSkipsDNS(t *testing.T) {
	dns := &testWorkDNSRouter{ips: []netip.Addr{netip.MustParseAddr("198.51.100.7")}}
	r := &WorkVPNItem{context.Background(), testNoWorkInterface{}, dns}
	m := &adapter.InboundContext{Destination: M.Socksaddr{Fqdn: "public.example", Port: 443}}
	if r.Match(m) || dns.calls != 0 || len(m.DestinationAddresses) != 0 {
		t.Fatal("inactive work VPN added a DNS lookup or changed the proxy destination")
	}
}
