// overlay-base: github.com/sagernet/sing-box v1.13.18
package workvpn

import (
	"context"
	"errors"
	"net"
	"net/netip"
	"os"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/miekg/dns"
)

func syntheticSnapshot() Snapshot {
	return Snapshot{
		Adapters: []Adapter{
			{Name: "Office A", Description: "TAP-Windows Adapter V9", Index: 7, IPv6Index: 7, Up: true, IPv4Metric: 20, IPv6Metric: 20, DNS: []netip.Addr{netip.MustParseAddr("10.60.0.53")}},
			{Name: "Office B", Description: "OpenVPN Data Channel Offload", Index: 8, IPv6Index: 8, Up: true, IPv4Metric: 10, IPv6Metric: 10, DNS: []netip.Addr{netip.MustParseAddr("10.61.0.53")}},
			{Name: "Ethernet", Description: "Physical NIC", Index: 3, Up: true},
			{Name: "v2RayTun", Description: "sing-tun Tunnel", Index: 9, Up: true, DNS: []netip.Addr{netip.MustParseAddr("172.16.0.2")}},
			{Name: "Unidentified", Description: "Wintun Userspace Tunnel", Index: 10, Up: true, DNS: []netip.Addr{netip.MustParseAddr("10.62.0.53")}},
		},
		Routes: []Route{
			{netip.MustParsePrefix("10.60.0.0/16"), 7, 5},
			{netip.MustParsePrefix("10.61.0.0/16"), 8, 5},
			{netip.MustParsePrefix("203.0.113.0/24"), 7, 5},
			{netip.MustParsePrefix("2001:db8:42::/48"), 8, 5},
			{netip.MustParsePrefix("10.60.9.0/24"), 8, 5},
			{netip.MustParsePrefix("10.60.9.2/32"), 3, 0},
			{netip.MustParsePrefix("10.62.0.0/16"), 10, 0},
			{netip.MustParsePrefix("0.0.0.0/0"), 7, 0},
			{netip.MustParsePrefix("0.0.0.0/1"), 7, 0},
			{netip.MustParsePrefix("128.0.0.0/1"), 7, 0},
			{netip.MustParsePrefix("0.0.0.0/2"), 9, 0},
			{netip.MustParsePrefix("203.0.113.21/32"), 9, 0},
			{netip.MustParsePrefix("172.16.0.0/30"), 9, 0},
		},
	}
}

func TestWorkVPNRouteSelection(t *testing.T) {
	s := syntheticSnapshot()
	for _, tc := range []struct {
		ip    string
		index int
	}{
		{"10.60.4.5", 7}, {"::ffff:10.60.4.5", 7}, {"10.60.9.1", 8},
		{"203.0.113.21", 7}, {"2001:db8:42::5", 8},
		{"10.60.9.2", 0}, {"10.62.0.5", 0}, {"1.1.1.1", 0}, {"128.1.1.1", 0},
		{"172.16.0.2", 0}, {"127.0.0.1", 0}, {"224.0.0.1", 0}, {"0.0.0.0", 0},
	} {
		_, index, ok := s.Interface(netip.MustParseAddr(tc.ip), nil)
		if (tc.index != 0) != ok || ok && index != tc.index {
			t.Fatalf("%s selected %d %v; want %d", tc.ip, index, ok, tc.index)
		}
	}
	if _, _, ok := s.Interface(netip.MustParseAddr("203.0.113.21"), []string{"Office A"}); ok {
		t.Fatal("own configured TUN alias was selected")
	}
	s.Routes = append(s.Routes, Route{netip.MustParsePrefix("203.0.113.0/24"), 8, 5})
	if _, index, ok := s.Interface(netip.MustParseAddr("203.0.113.21"), nil); !ok || index != 8 {
		t.Fatal("route+interface metric ignored")
	}
	s.Adapters[1].Up = false
	if _, index, ok := s.Interface(netip.MustParseAddr("203.0.113.21"), nil); !ok || index != 7 {
		t.Fatal("disconnected VPN retained")
	}
	if got := len(syntheticSnapshot().resolvers(nil)); got != 2 {
		t.Fatalf("discovered %d DNS interfaces; want 2", got)
	}
	for _, tc := range []struct{ name, description string }{{"v2RayTun", "TAP-Windows Adapter V9"}, {"Renamed", "sing-tun Tunnel"}, {"neko-tun", "OpenVPN"}} {
		if (Adapter{Name: tc.name, Description: tc.description, Index: 99, Up: true}).isVPN(nil) {
			t.Fatal("proxy accepted as VPN")
		}
	}
}

func syntheticManager() *Manager {
	m := New(nil)
	m.read = func() (Snapshot, error) { return syntheticSnapshot(), nil }
	return m
}

func TestWorkVPNHasNetworksRequiresSpecificActiveVPNRoutes(t *testing.T) {
	for _, test := range []struct {
		name     string
		data     Snapshot
		excluded []string
		want     bool
	}{
		{"active", syntheticSnapshot(), nil, true},
		{"absent", Snapshot{}, nil, false},
		{"own excluded", syntheticSnapshot(), []string{"Office A", "Office B"}, false},
		{"default routes only", Snapshot{Adapters: syntheticSnapshot().Adapters, Routes: []Route{
			{netip.MustParsePrefix("0.0.0.0/0"), 7, 0},
			{netip.MustParsePrefix("0.0.0.0/1"), 7, 0},
			{netip.MustParsePrefix("128.0.0.0/1"), 7, 0},
		}}, nil, false},
	} {
		t.Run(test.name, func(t *testing.T) {
			m := New(test.excluded)
			m.read = func() (Snapshot, error) { return test.data, nil }
			if m.HasNetworks() != test.want {
				t.Fatal("incorrect active VPN route detection")
			}
		})
	}
}

func response(q *dns.Msg, ips ...string) *dns.Msg {
	r := new(dns.Msg)
	r.SetReply(q)
	for _, text := range ips {
		ip := net.ParseIP(text)
		h := dns.RR_Header{Name: q.Question[0].Name, Class: dns.ClassINET, Ttl: 60}
		if ip.To4() != nil {
			h.Rrtype = dns.TypeA
			r.Answer = append(r.Answer, &dns.A{Hdr: h, A: ip})
		} else {
			h.Rrtype = dns.TypeAAAA
			r.Answer = append(r.Answer, &dns.AAAA{Hdr: h, AAAA: ip})
		}
	}
	return r
}

func TestWorkVPNUnknownNamesAndPublicPrefixes(t *testing.T) {
	type cacheProbeKey struct{}
	var cacheMisses atomic.Int32
	m := syntheticManager()
	m.exchange = func(ctx context.Context, r resolver, q *dns.Msg) (*dns.Msg, error) {
		if ctx.Value(cacheProbeKey{}) != nil {
			cacheMisses.Add(1)
		}
		if r.index == 7 {
			return nil, errors.New("one VPN DNS unreachable")
		}
		if q.Question[0].Qtype == dns.TypeA {
			return response(q, "203.0.113.21"), nil
		}
		return response(q), nil
	}
	ips, ok, err := m.TryLookup(context.Background(), "never-configured-before.example")
	if err != nil || !ok || len(ips) != 1 || ips[0] != netip.MustParseAddr("203.0.113.21") {
		t.Fatalf("unknown corporate name ignored: %v %v", ips, ok)
	}
	q := new(dns.Msg)
	q.SetQuestion("never-configured-before.example.", dns.TypeAAAA)
	r, ok := m.TryExchange(context.Background(), q)
	if !ok || r.Rcode != dns.RcodeSuccess || len(r.Answer) != 0 {
		t.Fatal("IPv4-only work name's AAAA escaped to fallback DNS")
	}
	if _, err := r.Pack(); err != nil {
		t.Fatalf("invalid NODATA response: %v", err)
	}
	// The first successful resolver can return before its losing workers run.
	// Count exchanges belonging to this lookup, not late work from the first.
	probe := context.WithValue(context.Background(), cacheProbeKey{}, true)
	ips, ok, err = m.TryLookup(probe, "never-configured-before.example")
	if err != nil || !ok || len(ips) != 1 || ips[0] != netip.MustParseAddr("203.0.113.21") {
		t.Fatalf("cached corporate lookup changed: %v %v %v", ips, ok, err)
	}
	if cacheMisses.Load() != 0 {
		t.Fatal("classified answer was not cached")
	}
	// OS changes invalidate corporate DNS independently from ordinary DNS cache.
	m.read = func() (Snapshot, error) { return Snapshot{}, nil }
	m.Reset()
	if _, ok, _ := m.TryLookup(context.Background(), "never-configured-before.example"); ok {
		t.Fatal("disconnected VPN DNS persisted")
	}
}

func TestWorkVPNUnrelatedDNSAndErrorsFallBack(t *testing.T) {
	for _, tc := range []struct {
		name   string
		answer func(*dns.Msg) (*dns.Msg, error)
	}{
		{"public", func(q *dns.Msg) (*dns.Msg, error) { return response(q, "198.51.100.7"), nil }},
		{"mixed", func(q *dns.Msg) (*dns.Msg, error) { return response(q, "203.0.113.21", "198.51.100.7"), nil }},
		{"NXDOMAIN", func(q *dns.Msg) (*dns.Msg, error) { r := response(q); r.Rcode = dns.RcodeNameError; return r, nil }},
		{"refused", func(q *dns.Msg) (*dns.Msg, error) { r := response(q); r.Rcode = dns.RcodeRefused; return r, nil }},
		{"blocked", func(q *dns.Msg) (*dns.Msg, error) { return nil, errors.New("access denied") }},
	} {
		t.Run(tc.name, func(t *testing.T) {
			m := syntheticManager()
			m.exchange = func(_ context.Context, _ resolver, q *dns.Msg) (*dns.Msg, error) { return tc.answer(q) }
			q := new(dns.Msg)
			q.SetQuestion("ordinary.example.", dns.TypeA)
			if _, ok := m.TryExchange(context.Background(), q); ok {
				t.Fatal("unrelated/failed DNS replaced ordinary policy")
			}
		})
	}
}

func TestWorkVPNBrowserRecordClassification(t *testing.T) {
	for _, qtype := range []uint16{dns.TypeHTTPS, dns.TypeSVCB} {
		for _, recordRcode := range []int{dns.RcodeSuccess, dns.RcodeNameError} {
			t.Run(dns.TypeToString[qtype]+"/"+dns.RcodeToString[recordRcode], func(t *testing.T) {
				m := syntheticManager()
				m.exchange = func(_ context.Context, _ resolver, q *dns.Msg) (*dns.Msg, error) {
					if q.Question[0].Qtype == dns.TypeA {
						return response(q, "10.60.4.5"), nil
					}
					result := response(q)
					result.Rcode = recordRcode
					return result, nil
				}
				q := new(dns.Msg)
				q.SetQuestion("unlisted-host.example.", qtype)
				r, handled := m.TryExchange(context.Background(), q)
				if !handled || r.Rcode != dns.RcodeSuccess || len(r.Answer) != 0 || r.Question[0] != q.Question[0] {
					t.Fatal("positive corporate A did not recover modern-record NODATA")
				}
				if _, err := r.Pack(); err != nil {
					t.Fatal(err)
				}
			})
		}
	}
}

func TestWorkVPNBrowserRecordPreservesActualAnswer(t *testing.T) {
	for _, qtype := range []uint16{dns.TypeHTTPS, dns.TypeSVCB} {
		m := syntheticManager()
		m.exchange = func(_ context.Context, _ resolver, q *dns.Msg) (*dns.Msg, error) {
			if q.Question[0].Qtype == dns.TypeA {
				return response(q, "10.60.4.5"), nil
			}
			r := response(q)
			value := dns.SVCB{Hdr: dns.RR_Header{Name: q.Question[0].Name, Rrtype: qtype, Class: dns.ClassINET, Ttl: 60}, Priority: 1, Target: "."}
			if qtype == dns.TypeHTTPS {
				r.Answer = []dns.RR{&dns.HTTPS{SVCB: value}}
			} else {
				r.Answer = []dns.RR{&value}
			}
			return r, nil
		}
		q := new(dns.Msg)
		q.SetQuestion("record-present.example.", qtype)
		r, handled := m.TryExchange(context.Background(), q)
		if !handled || r.Rcode != dns.RcodeSuccess || len(r.Answer) != 1 || r.Answer[0].Header().Rrtype != qtype {
			t.Fatal("actual corporate service-binding record was discarded")
		}
	}
}

func TestWorkVPNBrowserRecordIPv6OnlyClassification(t *testing.T) {
	m := syntheticManager()
	m.exchange = func(_ context.Context, _ resolver, q *dns.Msg) (*dns.Msg, error) {
		if q.Question[0].Qtype == dns.TypeAAAA {
			return response(q, "2001:db8:42::5"), nil
		}
		return response(q), nil
	}
	q := new(dns.Msg)
	q.SetQuestion("ipv6-only-work-host.example.", dns.TypeHTTPS)
	r, handled := m.TryExchange(context.Background(), q)
	if !handled || r.Rcode != dns.RcodeSuccess || len(r.Answer) != 0 {
		t.Fatal("IPv6-only VPN route did not classify service-binding name")
	}
}

func TestWorkVPNBrowserRecordRejectsUnroutedAndFailedClassifications(t *testing.T) {
	for _, tc := range []struct {
		name    string
		ips     []string
		failure bool
	}{
		{"public", []string{"198.51.100.7"}, false},
		{"private-LAN", []string{"192.168.50.7"}, false},
		{"mixed", []string{"10.60.4.5", "198.51.100.7"}, false},
		{"no-address", nil, false}, {"timeout", nil, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			m := syntheticManager()
			m.exchange = func(_ context.Context, _ resolver, q *dns.Msg) (*dns.Msg, error) {
				if q.Question[0].Qtype == dns.TypeA || q.Question[0].Qtype == dns.TypeAAAA {
					if tc.failure {
						return nil, context.DeadlineExceeded
					}
					return response(q, tc.ips...), nil
				}
				return response(q), nil
			}
			q := new(dns.Msg)
			q.SetQuestion("not-classified.example.", dns.TypeHTTPS)
			if _, handled := m.TryExchange(context.Background(), q); handled {
				t.Fatal("unrelated or failed address classification handled modern record")
			}
		})
	}
}

func TestWorkVPNSuffixesAndBoundedCancellation(t *testing.T) {
	m := syntheticManager()
	m.read = func() (Snapshot, error) {
		s := syntheticSnapshot()
		s.Adapters[0].Suffixes = []string{"office.example"}
		return s, nil
	}
	m.exchange = func(ctx context.Context, r resolver, q *dns.Msg) (*dns.Msg, error) {
		if r.index != 7 {
			t.Error("scoped name sent to unrelated VPN")
		}
		return response(q), nil
	}
	q := new(dns.Msg)
	q.SetQuestion("_ldap._tcp.office.example.", dns.TypeSRV)
	if _, ok := m.TryExchange(context.Background(), q); !ok {
		t.Fatal("OS suffix did not route SRV")
	}
	m.exchange = func(ctx context.Context, _ resolver, _ *dns.Msg) (*dns.Msg, error) {
		<-ctx.Done()
		return nil, ctx.Err()
	}
	m.Reset()
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Millisecond)
	defer cancel()
	start := time.Now()
	r, ok := m.TryExchange(ctx, q)
	if !ok || r.Rcode != dns.RcodeServerFailure {
		t.Fatal("known VPN suffix leaked on timeout")
	}
	if time.Since(start) > 200*time.Millisecond {
		t.Fatal("query ignored cancellation")
	}
	q.SetQuestion("unrelatedoffice.example.", dns.TypeSRV)
	if _, ok := m.TryExchange(context.Background(), q); ok {
		t.Fatal("suffix matched without label boundary")
	}
}

func TestWorkVPNCacheCopiesAndConcurrentUse(t *testing.T) {
	m := syntheticManager()
	m.exchange = func(_ context.Context, _ resolver, q *dns.Msg) (*dns.Msg, error) {
		return response(q, "203.0.113.21"), nil
	}
	var wg sync.WaitGroup
	for range 20 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			q := new(dns.Msg)
			q.SetQuestion("concurrent.example.", dns.TypeA)
			for range 5 {
				r, ok := m.TryExchange(context.Background(), q)
				if !ok {
					t.Error("concurrent lookup failed")
					return
				}
				r.Answer[0].Header().Ttl = 0
			}
		}()
	}
	wg.Wait()
	q := new(dns.Msg)
	q.SetQuestion("concurrent.example.", dns.TypeA)
	q.Id = 123
	r, ok := m.TryExchange(context.Background(), q)
	if !ok || r.Id != 123 || r.Answer[0].Header().Ttl == 0 {
		t.Fatal("caller modified shared cached DNS")
	}
	m.read = func() (Snapshot, error) { return Snapshot{}, errors.New("OS table unavailable") }
	m.Reset()
	if _, _, ok := m.Interface(netip.MustParseAddr("203.0.113.21")); ok {
		t.Fatal("OS failure retained routes")
	}
}

func TestWorkVPNNRPTNamespaces(t *testing.T) {
	for _, tc := range []struct {
		ns, name string
		match    bool
	}{
		{".office.example", "new.office.example", true}, {".office.example", "otheroffice.example", false},
		{"host.office.example", "host.office.example", true}, {"host.office.example", "child.host.office.example", false},
		{".", "public.example", false}, {"*.example", "host.example", false},
	} {
		if (namespaceScope(tc.ns, tc.name) > 0) != tc.match {
			t.Fatalf("NRPT %s matched %s incorrectly", tc.ns, tc.name)
		}
	}
	m := syntheticManager()
	m.read = func() (Snapshot, error) {
		s := syntheticSnapshot()
		s.NRPT = []PolicyDNS{{".nrpt.example", netip.MustParseAddr("10.60.1.53")}}
		return s, nil
	}
	if !m.HasScope("new.nrpt.example") || m.HasScope("public.example") {
		t.Fatal("NRPT scope not bound to work VPN")
	}
	m.exchange = func(_ context.Context, r resolver, q *dns.Msg) (*dns.Msg, error) {
		if r.address.String() != "10.60.1.53" {
			t.Fatal("NRPT nameserver selection ignored")
		}
		return response(q), nil
	}
	q := new(dns.Msg)
	q.SetQuestion("new.nrpt.example.", dns.TypeSRV)
	if _, ok := m.TryExchange(context.Background(), q); !ok {
		t.Fatal("NRPT SRV was not handled")
	}
	m.read = func() (Snapshot, error) {
		s := syntheticSnapshot()
		s.Adapters[0].Suffixes = []string{"nrpt.example"}
		s.NRPT = []PolicyDNS{{"host.nrpt.example", netip.MustParseAddr("198.51.100.53")}}
		return s, nil
	}
	m.Reset()
	q.SetQuestion("host.nrpt.example.", dns.TypeA)
	if m.HasScope(q.Question[0].Name) {
		t.Fatal("non-VPN NRPT exclusion ignored")
	}
	if _, ok := m.TryExchange(context.Background(), q); ok {
		t.Fatal("explicit non-VPN NRPT exclusion queried corporate DNS")
	}
}

// Explicit opt-in reads native tables only. No network or privileged mutation.
func TestWorkVPNLiveDiscoveryReadOnly(t *testing.T) {
	if os.Getenv("GREENRHYTHM_WORK_VPN_READONLY_TEST") != "1" {
		t.Skip("native table observation is opt-in")
	}
	s, err := readSystemSnapshot()
	if err != nil {
		t.Fatal(err)
	}
	servers := s.resolvers(nil)
	vpns, public := 0, 0
	for _, a := range s.Adapters {
		if a.isVPN(nil) {
			vpns++
		}
	}
	for _, r := range s.Routes {
		if a, ok := s.adapter(r.Index); ok && a.isVPN(nil) && specificPrefix(r.Prefix) && !r.Prefix.Addr().IsPrivate() {
			public++
		}
	}
	t.Logf("active work VPN adapters=%d, discovered DNS bindings=%d, specific public prefixes=%d", vpns, len(servers), public)
	if address := os.Getenv("GREENRHYTHM_WORK_VPN_TEST_ADDRESS"); address != "" {
		ip, err := netip.ParseAddr(address)
		if err != nil {
			t.Fatal(err)
		}
		_, index, ok := s.Interface(ip, nil)
		if !ok {
			t.Fatal("requested live destination has no selected work VPN route")
		}
		t.Logf("requested destination selects VPN interface index=%d", index)
	}
}

func TestWorkVPNLiveDNSObservationReadOnly(t *testing.T) {
	domain := os.Getenv("GREENRHYTHM_WORK_VPN_TEST_DOMAIN")
	if os.Getenv("GREENRHYTHM_WORK_VPN_READONLY_TEST") != "1" || domain == "" {
		t.Skip("explicit native DNS observation only")
	}
	s, err := readSystemSnapshot()
	if err != nil {
		t.Fatal(err)
	}
	servers := s.resolvers(nil)
	for _, server := range servers {
		ctx, cancel := context.WithTimeout(context.Background(), queryBudget)
		q := new(dns.Msg)
		q.SetQuestion(dns.Fqdn(domain), dns.TypeA)
		r, err := exchangeBound(ctx, server, q)
		cancel()
		if err != nil {
			t.Logf("bound VPN DNS interface=%d unavailable: %v", server.index, err)
			continue
		}
		t.Logf("bound VPN DNS interface=%d rcode=%d routed_answer=%v", server.index, r.Rcode, routedResponse(s, nil, r))
	}
}
