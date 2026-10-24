package main

import (
	"net/netip"

	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
)

// Windows sing-tun installs the first address's successor as the adapter DNS
// server. It is a virtual plaintext DNS endpoint, not a remote server. Routing
// DoT (or any other TCP/UDP service) there creates another TUN connection, which
// used to select the core's bypass rule and dial itself indefinitely (#53).
// These runtime-only guards precede sniffing and user routes; saved settings,
// external DNS servers and every destination outside our own endpoint survive.
func configureTunDNSGuard(options *option.Options) {
	var addresses []string
	seen := make(map[netip.Addr]bool)
	for _, inbound := range options.Inbounds {
		if inbound.Type != C.TypeTun {
			continue
		}
		tun, ok := inbound.Options.(*option.TunInboundOptions)
		if !ok || !tun.AutoRoute {
			continue
		}
		var has4, has6 bool
		for _, prefix := range tun.Address {
			address := prefix.Addr()
			if !prefix.IsValid() || address.Is4() && has4 || address.Is6() && has6 {
				continue
			}
			if address.Is4() {
				has4 = true
			} else {
				has6 = true
			}
			dns := address.Next()
			if prefix.Contains(dns) && !seen[dns] {
				seen[dns] = true
				addresses = append(addresses, netip.PrefixFrom(dns, dns.BitLen()).String())
			}
		}
	}
	if len(addresses) == 0 {
		return
	}
	if options.Route == nil {
		options.Route = new(option.RouteOptions)
	}
	rules := []option.Rule{
		{Type: C.RuleTypeDefault, DefaultOptions: option.DefaultRule{
			RawDefaultRule: option.RawDefaultRule{IPCIDR: addresses, Port: []uint16{53}, Network: []string{"tcp", "udp"}},
			RuleAction:     option.RuleAction{Action: C.RuleActionTypeHijackDNS},
		}},
		{Type: C.RuleTypeDefault, DefaultOptions: option.DefaultRule{
			RawDefaultRule: option.RawDefaultRule{IPCIDR: addresses, Network: []string{"tcp", "udp"}},
			// A reset/ICMP refusal lets DNS clients fall back promptly. The normal
			// reject rate limiter must not turn a local retry burst into drops.
			RuleAction: option.RuleAction{Action: C.RuleActionTypeReject, RejectOptions: option.RejectActionOptions{Method: C.RuleActionRejectMethodDefault, NoDrop: true}},
		}},
	}
	options.Route.Rules = append(rules, options.Route.Rules...)
}
