// overlay-base: github.com/sagernet/sing-box v1.13.18
package dns

import (
	"context"
	"errors"
	"net/netip"
	"testing"

	mDNS "github.com/miekg/dns"
	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
)

type testVPNResolver struct {
	handled, scoped bool
	resets, queries int
}

func (m *testVPNResolver) HasScope(string) bool { return m.scoped }
func (m *testVPNResolver) TryExchange(_ context.Context, q *mDNS.Msg) (*mDNS.Msg, bool) {
	m.queries++
	return FixedResponse(q.Id, q.Question[0], []netip.Addr{netip.MustParseAddr("203.0.113.21")}, 60), m.handled
}
func (m *testVPNResolver) TryLookup(context.Context, string) ([]netip.Addr, bool, error) {
	m.queries++
	return []netip.Addr{netip.MustParseAddr("203.0.113.21"), netip.MustParseAddr("2001:db8:42::5")}, m.handled, nil
}
func (m *testVPNResolver) Reset() { m.resets++ }

type testOrdinaryDNSClient struct {
	calls, clears   int
	rcode           int
	empty, ipv4Only bool
	failure         error
}

func (c *testOrdinaryDNSClient) Start()      {}
func (c *testOrdinaryDNSClient) ClearCache() { c.clears++ }
func (c *testOrdinaryDNSClient) Exchange(_ context.Context, transport adapter.DNSTransport, q *mDNS.Msg, _ adapter.DNSQueryOptions, _ func([]netip.Addr) bool) (*mDNS.Msg, error) {
	c.calls++
	if transport != nil && transport.Type() == C.DNSTypeFakeIP {
		return FixedResponse(q.Id, q.Question[0], []netip.Addr{netip.MustParseAddr("198.18.0.5")}, 60), nil
	}
	if c.failure != nil {
		return nil, c.failure
	}
	if c.rcode != 0 {
		return FixedResponseStatus(q, c.rcode), nil
	}
	if c.empty || c.ipv4Only && q.Question[0].Qtype == mDNS.TypeAAAA {
		return FixedResponseStatus(q, mDNS.RcodeSuccess), nil
	}
	return FixedResponse(q.Id, q.Question[0], []netip.Addr{netip.MustParseAddr("198.51.100.7")}, 60), nil
}

type testDNSTransport struct{ TransportAdapter }

func (*testDNSTransport) Start(adapter.StartStage) error                         { return nil }
func (*testDNSTransport) Close() error                                           { return nil }
func (*testDNSTransport) Reset()                                                 {}
func (*testDNSTransport) Exchange(context.Context, *mDNS.Msg) (*mDNS.Msg, error) { return nil, nil }

func TestWorkVPNFakeIPPublicNamesKeepOriginalPolicy(t *testing.T) {
	real := &testDNSTransport{NewTransportAdapter(C.DNSTypeUDP, "ordinary", nil)}
	fake := &testDNSTransport{NewTransportAdapter(C.DNSTypeFakeIP, "fake", nil)}
	transports := &TransportManager{defaultTransport: real, transports: []adapter.DNSTransport{real, fake}}
	base := new(testOrdinaryDNSClient)
	manager := &testVPNResolver{handled: true}
	client := &workVPNClient{base, manager, transports}
	q := new(mDNS.Msg)
	q.SetQuestion("public.example.", mDNS.TypeA)
	r, err := client.Exchange(context.Background(), fake, q, adapter.DNSQueryOptions{}, nil)
	if err != nil || MessageToAddresses(r)[0].String() != "198.18.0.5" || manager.queries != 0 {
		t.Fatal("fake-IP public name leaked to corporate DNS or lost fake-IP")
	}
	base.rcode = mDNS.RcodeNameError
	r, err = client.Exchange(context.Background(), fake, q, adapter.DNSQueryOptions{}, nil)
	if err != nil || MessageToAddresses(r)[0].String() != "203.0.113.21" || manager.queries != 1 {
		t.Fatal("unknown corporate name was masked by synthetic fake-IP success")
	}
}
func (c *testOrdinaryDNSClient) Lookup(context.Context, adapter.DNSTransport, string, adapter.DNSQueryOptions, func([]netip.Addr) bool) ([]netip.Addr, error) {
	c.calls++
	if c.failure != nil {
		return nil, c.failure
	}
	if c.rcode != 0 {
		return nil, RcodeError(c.rcode)
	}
	if c.empty {
		return nil, nil
	}
	return []netip.Addr{netip.MustParseAddr("198.51.100.7")}, nil
}

func TestWorkVPNPublicDNSNeverQueriesCorporateResolver(t *testing.T) {
	base := new(testOrdinaryDNSClient)
	if newWorkVPNClient(context.Background(), base) != base {
		t.Fatal("disabled option changed client")
	}
	manager := &testVPNResolver{handled: true}
	client := &workVPNClient{DNSClient: base, manager: manager}
	q := new(mDNS.Msg)
	q.SetQuestion("public.example.", mDNS.TypeA)
	r, err := client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{}, nil)
	if err != nil || MessageToAddresses(r)[0].String() != "198.51.100.7" || manager.queries != 0 {
		t.Fatal("successful public A query reached corporate DNS")
	}
	_, err = client.Lookup(context.Background(), nil, "public.example", adapter.DNSQueryOptions{}, nil)
	if err != nil || manager.queries != 0 {
		t.Fatal("successful public lookup reached corporate DNS")
	}
	base.ipv4Only = true
	q.SetQuestion("public.example.", mDNS.TypeAAAA)
	_, err = client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{}, nil)
	if err != nil || manager.queries != 0 {
		t.Fatal("IPv4-only public name reached corporate DNS after AAAA NODATA")
	}
	client.ClearCache()
	if manager.resets != 1 || base.clears != 1 {
		t.Fatal("cache reset failed")
	}
}

func TestWorkVPNUnknownNameFallbackOnlyAfterNegativeOrdinaryDNS(t *testing.T) {
	for _, tc := range []struct {
		name      string
		rcode     int
		empty     bool
		failure   error
		wantQuery bool
	}{
		{"NXDOMAIN", mDNS.RcodeNameError, false, nil, true}, {"NODATA", 0, true, nil, true},
		{"timeout", 0, false, context.DeadlineExceeded, false}, {"refused", mDNS.RcodeRefused, false, nil, false}, {"response rejected", 0, false, ErrResponseRejected, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			base := &testOrdinaryDNSClient{rcode: tc.rcode, empty: tc.empty, failure: tc.failure}
			manager := &testVPNResolver{handled: true}
			client := &workVPNClient{DNSClient: base, manager: manager}
			q := new(mDNS.Msg)
			q.SetQuestion("never-listed.example.", mDNS.TypeA)
			r, err := client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{}, nil)
			if (manager.queries > 0) != tc.wantQuery {
				t.Fatalf("corporate DNS calls=%d", manager.queries)
			}
			if tc.wantQuery && (err != nil || MessageToAddresses(r)[0].String() != "203.0.113.21" || base.calls == 0) {
				t.Fatal("unknown routed name not recovered after ordinary negative")
			}
			manager.queries = 0
			ips, err := client.Lookup(context.Background(), nil, "never-listed.example", adapter.DNSQueryOptions{}, nil)
			if (manager.queries > 0) != tc.wantQuery {
				t.Fatal("lookup fallback ran without ordinary negative")
			}
			if tc.wantQuery && (err != nil || len(ips) == 0 || ips[0].String() != "203.0.113.21") {
				t.Fatal("lookup fallback did not return corporate address")
			}
		})
	}
}

func TestWorkVPNScopeAndStrategy(t *testing.T) {
	base := new(testOrdinaryDNSClient)
	manager := &testVPNResolver{handled: true, scoped: true}
	client := &workVPNClient{DNSClient: base, manager: manager}
	ips, err := client.Lookup(context.Background(), nil, "work.example", adapter.DNSQueryOptions{Strategy: C.DomainStrategyIPv4Only, LookupStrategy: C.DomainStrategyIPv6Only}, nil)
	if err != nil || len(ips) != 1 || !ips[0].Is6() || base.calls != 0 {
		t.Fatal("OS scope or lookup strategy ignored")
	}
	q := new(mDNS.Msg)
	q.SetQuestion("work.example.", mDNS.TypeA)
	if _, err := client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{}, func([]netip.Addr) bool { return false }); err != ErrResponseRejected {
		t.Fatal("response filter ignored")
	}
	manager.handled = false
	r, err := client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{}, nil)
	if err != nil || r.Rcode != mDNS.RcodeServerFailure || base.calls != 0 {
		t.Fatal("OS scope leaked to ordinary DNS")
	}
	before := manager.queries
	q.SetQuestion("work.example.", mDNS.TypeAAAA)
	r, err = client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{Strategy: C.DomainStrategyIPv4Only}, nil)
	if err != nil || len(r.Answer) != 0 || manager.queries != before {
		t.Fatal("disabled family queried DNS")
	}
}

func TestWorkVPNFallbackUnavailablePreservesOrdinaryError(t *testing.T) {
	base := &testOrdinaryDNSClient{rcode: mDNS.RcodeNameError}
	manager := &testVPNResolver{}
	client := &workVPNClient{DNSClient: base, manager: manager}
	_, err := client.Lookup(context.Background(), nil, "unknown.example", adapter.DNSQueryOptions{}, nil)
	var rcode RcodeError
	if !errors.As(err, &rcode) || rcode != RcodeNameError {
		t.Fatal("normal negative error lost")
	}
}

type testRecordDNSClient struct {
	*testOrdinaryDNSClient
	recordRcode    int
	addresses      []netip.Addr
	addressError   error
	recordFailure  error
	recordPositive bool
}

func (c *testRecordDNSClient) Exchange(_ context.Context, _ adapter.DNSTransport, q *mDNS.Msg, _ adapter.DNSQueryOptions, _ func([]netip.Addr) bool) (*mDNS.Msg, error) {
	c.calls++
	if c.recordFailure != nil {
		return nil, c.recordFailure
	}
	if c.recordPositive {
		response := new(mDNS.Msg)
		response.SetReply(q)
		value := mDNS.SVCB{Hdr: mDNS.RR_Header{Name: q.Question[0].Name, Rrtype: q.Question[0].Qtype, Class: mDNS.ClassINET, Ttl: 60}, Priority: 1, Target: "."}
		if q.Question[0].Qtype == mDNS.TypeHTTPS {
			response.Answer = []mDNS.RR{&mDNS.HTTPS{SVCB: value}}
		} else {
			response.Answer = []mDNS.RR{&value}
		}
		return response, nil
	}
	return FixedResponseStatus(q, c.recordRcode), nil
}
func (c *testRecordDNSClient) Lookup(context.Context, adapter.DNSTransport, string, adapter.DNSQueryOptions, func([]netip.Addr) bool) ([]netip.Addr, error) {
	c.calls++
	return c.addresses, c.addressError
}

type testRecordVPNResolver struct{ testVPNResolver }

func (m *testRecordVPNResolver) TryExchange(_ context.Context, q *mDNS.Msg) (*mDNS.Msg, bool) {
	m.queries++
	return FixedResponseStatus(q, mDNS.RcodeSuccess), m.handled
}

func TestWorkVPNBrowserRecordsUseCorporateNODATA(t *testing.T) {
	for _, qtype := range []uint16{mDNS.TypeHTTPS, mDNS.TypeSVCB} {
		t.Run(mDNS.TypeToString[qtype], func(t *testing.T) {
			for _, ordinaryRcode := range []int{mDNS.RcodeNameError, mDNS.RcodeSuccess} {
				base := &testRecordDNSClient{testOrdinaryDNSClient: new(testOrdinaryDNSClient), recordRcode: ordinaryRcode,
					addressError: RcodeError(mDNS.RcodeNameError)}
				manager := &testRecordVPNResolver{testVPNResolver{handled: true}}
				client := &workVPNClient{DNSClient: base, manager: manager}
				q := new(mDNS.Msg)
				q.SetQuestion("previously-unlisted-work-host.example.", qtype)
				r, err := client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{}, nil)
				if err != nil || r.Rcode != mDNS.RcodeSuccess || len(r.Answer) != 0 || manager.queries != 1 || r.Question[0].Qtype != qtype {
					t.Fatal("browser record retained public NXDOMAIN instead of corporate NODATA")
				}
			}
		})
	}
}

func TestWorkVPNBrowserRecordsPreserveOrdinaryPolicy(t *testing.T) {
	for _, qtype := range []uint16{mDNS.TypeHTTPS, mDNS.TypeSVCB} {
		t.Run(mDNS.TypeToString[qtype]+"/positive-service-record", func(t *testing.T) {
			base := &testRecordDNSClient{testOrdinaryDNSClient: new(testOrdinaryDNSClient), recordPositive: true,
				addressError: RcodeError(mDNS.RcodeNameError)}
			manager := &testRecordVPNResolver{testVPNResolver{handled: true}}
			client := &workVPNClient{DNSClient: base, manager: manager}
			q := new(mDNS.Msg)
			q.SetQuestion("public-record.example.", qtype)
			r, err := client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{}, nil)
			if err != nil || len(r.Answer) != 1 || manager.queries != 0 || base.calls != 1 {
				t.Fatal("positive ordinary service record triggered address/corporate classification")
			}
		})
		for _, tc := range []struct {
			name                        string
			ips                         []netip.Addr
			addressError, recordFailure error
			rcode                       int
		}{
			{"public-address", []netip.Addr{netip.MustParseAddr("198.51.100.7")}, nil, nil, mDNS.RcodeNameError},
			{"private-LAN-address", []netip.Addr{netip.MustParseAddr("192.168.50.7")}, nil, nil, mDNS.RcodeSuccess},
			{"address-timeout", nil, context.DeadlineExceeded, nil, mDNS.RcodeNameError},
			{"record-timeout", nil, RcodeError(mDNS.RcodeNameError), context.DeadlineExceeded, 0},
			{"record-refused", nil, RcodeError(mDNS.RcodeNameError), nil, mDNS.RcodeRefused},
			{"response-rejected", nil, RcodeError(mDNS.RcodeNameError), ErrResponseRejected, 0},
		} {
			t.Run(mDNS.TypeToString[qtype]+"/"+tc.name, func(t *testing.T) {
				base := &testRecordDNSClient{testOrdinaryDNSClient: new(testOrdinaryDNSClient), recordRcode: tc.rcode,
					addresses: tc.ips, addressError: tc.addressError, recordFailure: tc.recordFailure}
				manager := &testRecordVPNResolver{testVPNResolver{handled: true}}
				client := &workVPNClient{DNSClient: base, manager: manager}
				q := new(mDNS.Msg)
				q.SetQuestion("ordinary-host.example.", qtype)
				r, err := client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{}, nil)
				if manager.queries != 0 || tc.recordFailure != nil && !errors.Is(err, tc.recordFailure) || tc.recordFailure == nil && (err != nil || r.Rcode != tc.rcode) {
					t.Fatal("ordinary record policy changed or reached corporate DNS")
				}
			})
		}
	}
}

func TestWorkVPNBrowserRecordsUnavailablePreservesNXDOMAIN(t *testing.T) {
	base := &testRecordDNSClient{testOrdinaryDNSClient: new(testOrdinaryDNSClient), recordRcode: mDNS.RcodeNameError,
		addressError: RcodeError(mDNS.RcodeNameError)}
	manager := &testRecordVPNResolver{}
	client := &workVPNClient{DNSClient: base, manager: manager}
	q := new(mDNS.Msg)
	q.SetQuestion("not-a-work-host.example.", mDNS.TypeHTTPS)
	r, err := client.Exchange(context.Background(), nil, q, adapter.DNSQueryOptions{}, nil)
	if err != nil || r.Rcode != mDNS.RcodeNameError || manager.queries != 1 {
		t.Fatal("unavailable work fallback blackholed an ordinary name")
	}
}
