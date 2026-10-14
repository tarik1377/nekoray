// overlay-base: github.com/sagernet/sing-box v1.13.18
// Package workvpn reads Windows VPN configuration without changing the OS.
package workvpn

import (
	"net/netip"
	"strings"
)

type Adapter struct {
	Name, Description      string
	Index, IPv6Index       int
	Up                     bool
	IPv4Metric, IPv6Metric uint32
	DNS                    []netip.Addr
	Suffixes               []string
}

type PolicyDNS struct {
	Namespace string
	Address   netip.Addr
}

type Route struct {
	Prefix netip.Prefix
	Index  int
	Metric uint32
}

type Snapshot struct {
	Adapters []Adapter
	Routes   []Route
	NRPT     []PolicyDNS
}

// Specific NRPT exclusions to a LAN DNS (or security policy we cannot enforce)
// override broader adapter suffixes. They must not be turned into work queries.
func (s Snapshot) resolversForName(excluded []string, name string) ([]resolver, int) {
	scope := 0
	for _, policy := range s.NRPT {
		scope = max(scope, namespaceScope(policy.Namespace, name))
	}
	if scope > 0 {
		var result []resolver
		for _, policy := range s.NRPT {
			if namespaceScope(policy.Namespace, name) != scope {
				continue
			}
			a, index, ok := s.Interface(policy.Address, excluded)
			if ok {
				result = append(result, resolver{a, policy.Address, index, policy.Namespace})
			}
		}
		return result, scope
	}
	servers := s.resolvers(excluded)
	eligible := servers[:0]
	for _, server := range servers {
		if server.namespace == "" || server.nameScope(name) > 0 {
			eligible = append(eligible, server)
		}
	}
	servers = eligible
	for _, server := range servers {
		scope = max(scope, server.nameScope(name))
	}
	if scope > 0 {
		eligible = servers[:0]
		for _, server := range servers {
			if server.nameScope(name) == scope {
				eligible = append(eligible, server)
			}
		}
		servers = eligible
	}
	return servers, scope
}

// Proxy tunnels must never be rediscovered as a work VPN, even when renamed.
func IsProxyTunnel(name, description string) bool {
	identity := strings.ToLower(name + " " + description)
	for _, marker := range []string{"neko-tun", "sing-tun", "v2raytun", "v2ray-tun"} {
		if strings.Contains(identity, marker) {
			return true
		}
	}
	return false
}

func (a Adapter) isVPN(excluded []string) bool {
	if !a.Up || a.Index <= 0 || a.Name == "" || IsProxyTunnel(a.Name, a.Description) {
		return false
	}
	for _, name := range excluded {
		if strings.EqualFold(a.Name, name) {
			return false
		}
	}
	identity := strings.ToLower(a.Name + " " + a.Description)
	// Generic Wintun/Hyper-V/Ethernet adapters are deliberately not guessed.
	return strings.Contains(identity, "openvpn") || strings.Contains(identity, "tap-windows adapter")
}

func specificPrefix(p netip.Prefix) bool {
	// Redirect-gateway's /0 and two /1 routes express Internet policy, not
	// corporate destinations. Preserve the configured Internet outbound.
	return p.IsValid() && p.Bits() > 1 && p.Addr().IsGlobalUnicast()
}

func (s Snapshot) adapter(index int) (Adapter, bool) {
	for _, a := range s.Adapters {
		if a.Up && (a.Index == index || a.IPv6Index == index) {
			return a, true
		}
	}
	return Adapter{}, false
}

// Interface chooses the longest OS prefix, then route+interface metric.
// Capture routes belonging to proxy tunnels are ignored. More specific routes
// on ordinary LAN adapters still take precedence over a corporate aggregate.
func (s Snapshot) Interface(address netip.Addr, excluded []string) (Adapter, int, bool) {
	address = address.Unmap().WithZone("")
	if !address.IsValid() || !address.IsGlobalUnicast() {
		return Adapter{}, 0, false
	}
	var best Adapter
	bestIndex, bestBits := 0, -1
	bestMetric := ^uint64(0)
	for _, r := range s.Routes {
		if !specificPrefix(r.Prefix) || !r.Prefix.Contains(address) {
			continue
		}
		a, ok := s.adapter(r.Index)
		if !ok || IsProxyTunnel(a.Name, a.Description) {
			continue
		}
		ownTun := false
		for _, name := range excluded {
			if strings.EqualFold(a.Name, name) {
				ownTun = true
				break
			}
		}
		if ownTun {
			continue
		}
		metric := uint64(r.Metric)
		if address.Is4() {
			metric += uint64(a.IPv4Metric)
		} else {
			metric += uint64(a.IPv6Metric)
		}
		if bits := r.Prefix.Bits(); bits > bestBits || bits == bestBits && (metric < bestMetric || metric == bestMetric && r.Index < bestIndex) {
			best, bestIndex, bestBits, bestMetric = a, r.Index, bits, metric
		}
	}
	return best, bestIndex, bestIndex > 0 && best.isVPN(excluded)
}

type resolver struct {
	adapter   Adapter
	address   netip.Addr
	index     int
	namespace string
}

func namespaceScope(namespace, name string) int {
	name = strings.TrimSuffix(strings.ToLower(name), ".")
	namespace = strings.ToLower(namespace)
	if namespace == "" || namespace == "." || strings.ContainsAny(namespace, "*/:") {
		return 0
	}
	if strings.HasPrefix(namespace, ".") {
		suffix := strings.Trim(namespace, ".")
		if name == suffix || strings.HasSuffix(name, "."+suffix) {
			return len(suffix)
		}
	} else if name == strings.TrimSuffix(namespace, ".") {
		return len(namespace)
	}
	return 0
}

func (r resolver) nameScope(name string) int {
	if r.namespace != "" {
		return namespaceScope(r.namespace, name)
	}
	return r.adapter.nameScope(name)
}

func (r resolver) ownsName(name string) bool { return r.nameScope(name) > 0 }

func (a Adapter) ownsName(name string) bool {
	return a.nameScope(name) > 0
}

func (a Adapter) nameScope(name string) int {
	name = strings.TrimSuffix(strings.ToLower(name), ".")
	longest := 0
	for _, suffix := range a.Suffixes {
		suffix = strings.Trim(strings.ToLower(suffix), ".")
		if suffix != "" && (name == suffix || strings.HasSuffix(name, "."+suffix)) {
			longest = max(longest, len(suffix))
		}
	}
	return longest
}

func (s Snapshot) resolvers(excluded []string) []resolver {
	var result []resolver
	for _, a := range s.Adapters {
		if !a.isVPN(excluded) {
			continue
		}
		for _, ip := range a.DNS {
			ip = ip.Unmap()
			index := a.Index
			if ip.Is6() && a.IPv6Index > 0 {
				index = a.IPv6Index
			}
			// A configured DNS server must itself have a specific route on this
			// VPN; never bind its queries to a default route or another TUN.
			for _, r := range s.Routes {
				if r.Index == index && specificPrefix(r.Prefix) && r.Prefix.Contains(ip.WithZone("")) {
					result = append(result, resolver{adapter: a, address: ip, index: index})
					break
				}
			}
		}
	}
	return result
}
