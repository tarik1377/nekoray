// overlay-base: github.com/sagernet/sing-box v1.13.18
package dns

import (
	"context"
	"errors"
	"net/netip"

	mDNS "github.com/miekg/dns"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/common/workvpn"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing/service"
)

type workVPNResolver interface {
	HasScope(string) bool
	TryExchange(context.Context, *mDNS.Msg) (*mDNS.Msg, bool)
	TryLookup(context.Context, string) ([]netip.Addr, bool, error)
	Reset()
}

type workVPNClient struct {
	adapter.DNSClient
	manager    workVPNResolver
	transports adapter.DNSTransportManager
}

func newWorkVPNClient(ctx context.Context, client adapter.DNSClient) adapter.DNSClient {
	manager := service.FromContext[*workvpn.Manager](ctx)
	if manager == nil {
		return client
	}
	return &workVPNClient{client, manager, service.FromContext[adapter.DNSTransportManager](ctx)}
}

func (c *workVPNClient) ClearCache() { c.manager.Reset(); c.DNSClient.ClearCache() }

// Fake-IP's synthetic positive answer cannot classify a name. Use the normal
// real final DNS for classification, then preserve fake-IP for public names.
func (c *workVPNClient) ordinaryTransport(selected adapter.DNSTransport) adapter.DNSTransport {
	if selected == nil || selected.Type() != C.DNSTypeFakeIP || c.transports == nil {
		return selected
	}
	if def := c.transports.Default(); def != nil && def.Type() != C.DNSTypeFakeIP {
		return def
	}
	for _, transport := range c.transports.Transports() {
		if transport.Type() != C.DNSTypeFakeIP {
			return transport
		}
	}
	return selected
}

func negativeResponse(r *mDNS.Msg) bool {
	return r != nil && (r.Rcode == mDNS.RcodeNameError || r.Rcode == mDNS.RcodeSuccess && len(r.Answer) == 0)
}

func (c *workVPNClient) workResponse(ctx context.Context, message *mDNS.Msg, options adapter.DNSQueryOptions, check func([]netip.Addr) bool) (*mDNS.Msg, bool, error) {
	response, ok := c.manager.TryExchange(ctx, message)
	if !ok {
		return nil, false, nil
	}
	if check != nil && !check(MessageToAddresses(response)) {
		return nil, true, ErrResponseRejected
	}
	if options.RewriteTTL != nil {
		for _, list := range [][]mDNS.RR{response.Answer, response.Ns} {
			for _, rr := range list {
				rr.Header().Ttl = *options.RewriteTTL
			}
		}
	}
	return response, true, nil
}

func (c *workVPNClient) Exchange(ctx context.Context, transport adapter.DNSTransport, message *mDNS.Msg, options adapter.DNSQueryOptions, check func([]netip.Addr) bool) (*mDNS.Msg, error) {
	if len(message.Question) != 1 {
		return c.DNSClient.Exchange(ctx, transport, message, options, check)
	}
	question := message.Question[0]
	if question.Qtype == mDNS.TypeA && options.Strategy == C.DomainStrategyIPv6Only || question.Qtype == mDNS.TypeAAAA && options.Strategy == C.DomainStrategyIPv4Only {
		return FixedResponseStatus(message, mDNS.RcodeSuccess), nil
	}
	if c.manager.HasScope(question.Name) {
		if response, ok, err := c.workResponse(ctx, message, options, check); ok {
			return response, err
		}
		// An OS-advertised work namespace never falls through to public DNS.
		return FixedResponseStatus(message, mDNS.RcodeServerFailure), nil
	}
	ordinary := c.ordinaryTransport(transport)
	response, err := c.DNSClient.Exchange(ctx, ordinary, message, options, check)
	negative := err == nil && negativeResponse(response)
	addressQuery := question.Qtype == mDNS.TypeA || question.Qtype == mDNS.TypeAAAA
	if negative && !addressQuery {
		// HTTPS/SVCB (and other record types) may be queried before A/AAAA.
		// A missing record is not a missing public name: consult the ordinary
		// address resolver first, without adding a corporate probe for names
		// that already have a successful ordinary A/AAAA answer.
		ips, lookupErr := c.DNSClient.Lookup(ctx, ordinary, FqdnToDomain(question.Name), adapter.DNSQueryOptions{}, nil)
		var rcode RcodeError
		negative = len(ips) == 0 && (lookupErr == nil || errors.As(lookupErr, &rcode) && rcode == RcodeNameError)
	}
	// NODATA in one family is not a missing name if the normal resolver returns
	// a positive answer in the other family. NXDOMAIN already covers the name.
	if negative && addressQuery && response.Rcode == mDNS.RcodeSuccess {
		other := message.Copy()
		if question.Qtype == mDNS.TypeA {
			other.Question[0].Qtype = mDNS.TypeAAAA
		} else {
			other.Question[0].Qtype = mDNS.TypeA
		}
		otherResponse, otherErr := c.DNSClient.Exchange(ctx, ordinary, other, adapter.DNSQueryOptions{}, nil)
		negative = otherErr == nil && negativeResponse(otherResponse)
	}
	if negative {
		if work, ok, workErr := c.workResponse(ctx, message, options, check); ok {
			return work, workErr
		}
	}
	if ordinary != transport {
		return c.DNSClient.Exchange(ctx, transport, message, options, check)
	}
	return response, err
}

func (c *workVPNClient) workLookup(ctx context.Context, domain string, options adapter.DNSQueryOptions, check func([]netip.Addr) bool) ([]netip.Addr, bool, error) {
	ips, ok, err := c.manager.TryLookup(ctx, domain)
	if !ok {
		return nil, false, nil
	}
	if err != nil {
		var lookupError workvpn.LookupError
		if errors.As(err, &lookupError) {
			return nil, true, RcodeError(lookupError.Rcode)
		}
		return nil, true, err
	}
	strategy := options.Strategy
	if options.LookupStrategy != C.DomainStrategyAsIS {
		strategy = options.LookupStrategy
	}
	var ipv4, ipv6 []netip.Addr
	for _, ip := range ips {
		if ip.Is4() {
			ipv4 = append(ipv4, ip)
		} else {
			ipv6 = append(ipv6, ip)
		}
	}
	if strategy == C.DomainStrategyIPv4Only {
		ipv6 = nil
	}
	if strategy == C.DomainStrategyIPv6Only {
		ipv4 = nil
	}
	ips = sortAddresses(ipv4, ipv6, strategy)
	if check != nil && !check(ips) {
		return nil, true, ErrResponseRejected
	}
	return ips, true, nil
}

func (c *workVPNClient) Lookup(ctx context.Context, transport adapter.DNSTransport, domain string, options adapter.DNSQueryOptions, check func([]netip.Addr) bool) ([]netip.Addr, error) {
	if c.manager.HasScope(domain) {
		if ips, ok, err := c.workLookup(ctx, domain, options, check); ok {
			return ips, err
		}
		return nil, RcodeError(mDNS.RcodeServerFailure)
	}
	ips, err := c.DNSClient.Lookup(ctx, transport, domain, options, check)
	var rcode RcodeError
	negative := len(ips) == 0 && (err == nil || errors.As(err, &rcode) && rcode == RcodeNameError)
	// With a family restriction, verify that the unrestricted ordinary lookup
	// is also negative; do not send IPv4-only/IPv6-only public names to work DNS.
	if negative && (options.Strategy == C.DomainStrategyIPv4Only || options.Strategy == C.DomainStrategyIPv6Only || options.LookupStrategy == C.DomainStrategyIPv4Only || options.LookupStrategy == C.DomainStrategyIPv6Only) {
		unrestricted := options
		unrestricted.Strategy, unrestricted.LookupStrategy = C.DomainStrategyAsIS, C.DomainStrategyAsIS
		other, otherErr := c.DNSClient.Lookup(ctx, transport, domain, unrestricted, nil)
		negative = len(other) == 0 && (otherErr == nil || errors.As(otherErr, &rcode) && rcode == RcodeNameError)
	}
	if negative {
		if work, ok, workErr := c.workLookup(ctx, domain, options, check); ok {
			return work, workErr
		}
	}
	return ips, err
}
