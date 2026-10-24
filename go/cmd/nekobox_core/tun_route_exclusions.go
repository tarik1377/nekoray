package main

import (
	"context"
	"encoding/json"
	"fmt"
	"net/netip"
	"strings"
	"time"

	"golang.org/x/net/idna"
)

type tunGatewayLookup func(context.Context, string, string) ([]netip.Addr, error)

// Resolve explicitly saved gateway names before creating the TUN adapter. These
// are OS route exclusions, not DIRECT outbounds. No interface or gateway is
// pinned, so VMware NAT and host traffic use the route Windows already owns.
// Existing CIDR-only configs retain their exact bytes and do not perform DNS.
func prepareTunRouteExclusions(ctx context.Context, content []byte, lookup tunGatewayLookup) ([]byte, error) {
	var root map[string]json.RawMessage
	if err := json.Unmarshal(content, &root); err != nil {
		return nil, err
	}
	var inbounds []json.RawMessage
	if len(root["inbounds"]) == 0 {
		return content, nil
	}
	if err := json.Unmarshal(root["inbounds"], &inbounds); err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	changed := false
	cache := make(map[string][]string)
	dnsAddresses := make(map[netip.Addr]bool)
	for index, raw := range inbounds {
		var inbound map[string]json.RawMessage
		if err := json.Unmarshal(raw, &inbound); err != nil {
			return nil, err
		}
		var kind string
		_ = json.Unmarshal(inbound["type"], &kind)
		if kind != "tun" || len(inbound["route_exclude_address"]) == 0 {
			continue
		}
		entries, err := exclusionStrings(inbound["route_exclude_address"])
		if err != nil {
			return nil, fmt.Errorf("TUN bypass: %w", err)
		}
		if len(entries) > 2048 {
			return nil, fmt.Errorf("TUN bypass: too many destinations")
		}
		var own []netip.Prefix
		addresses, _ := exclusionStrings(inbound["address"])
		for _, address := range addresses {
			if p, e := netip.ParsePrefix(address); e == nil {
				own = append(own, p)
			}
		}
		var resolved []string
		seen := make(map[string]bool)
		appendPrefix := func(prefix string) {
			if !seen[prefix] {
				resolved = append(resolved, prefix)
				seen[prefix] = true
			}
		}
		inboundChanged := false
		for _, entry := range entries {
			if _, err := netip.ParsePrefix(entry); err == nil {
				appendPrefix(entry)
				continue
			}
			if ip, err := netip.ParseAddr(entry); err == nil {
				ip = ip.Unmap()
				appendPrefix(netip.PrefixFrom(ip, ip.BitLen()).String())
				inboundChanged = true
				continue
			}
			host, err := tunGatewayName(entry)
			if err != nil {
				return nil, err
			}
			prefixes, exists := cache[host]
			if !exists {
				if len(cache) >= 64 {
					return nil, fmt.Errorf("TUN bypass: at most 64 DNS names are allowed")
				}
				ips, err := lookup(ctx, "ip", host)
				if err != nil {
					return nil, fmt.Errorf("TUN bypass: system DNS could not resolve %s: %w; tunnel not started", host, err)
				}
				if len(ips) == 0 || len(ips) > 32 {
					return nil, fmt.Errorf("TUN bypass: expected 1 to 32 DNS addresses for %s; tunnel not started", host)
				}
				for _, ip := range ips {
					ip = ip.Unmap()
					if !ip.IsValid() || ip.IsUnspecified() || ip.IsMulticast() || ip.IsLoopback() || ip.IsLinkLocalUnicast() || ip.Zone() != "" || ip == netip.MustParseAddr("255.255.255.255") {
						return nil, fmt.Errorf("TUN bypass: system DNS returned an unusable gateway address for %s", host)
					}
					// A stale FakeIP answer must not become an OS route hole.
					if netip.MustParsePrefix("198.18.0.0/15").Contains(ip) || netip.MustParsePrefix("fc00::/18").Contains(ip) {
						return nil, fmt.Errorf("TUN bypass: system DNS returned a synthetic address for %s; use the real gateway IP or restore system DNS", host)
					}
					dnsAddresses[ip] = true
					if len(dnsAddresses) > 128 {
						return nil, fmt.Errorf("TUN bypass: too many DNS addresses; at most 128 unique addresses are allowed")
					}
					prefixes = append(prefixes, netip.PrefixFrom(ip, ip.BitLen()).String())
				}
				cache[host] = prefixes
			}
			for _, prefix := range prefixes {
				p, _ := netip.ParsePrefix(prefix)
				for _, subnet := range own {
					if subnet.Contains(p.Addr()) {
						return nil, fmt.Errorf("TUN bypass: gateway %s overlaps the TUN interface", host)
					}
				}
				appendPrefix(prefix)
			}
			inboundChanged = true
		}
		if inboundChanged {
			inbound["route_exclude_address"], _ = json.Marshal(resolved)
			inbounds[index], _ = json.Marshal(inbound)
			changed = true
		}
	}
	if !changed {
		return content, nil
	}
	root["inbounds"], _ = json.Marshal(inbounds)
	return json.Marshal(root)
}

func exclusionStrings(raw json.RawMessage) ([]string, error) {
	if len(raw) == 0 {
		return nil, nil
	}
	var list []string
	if json.Unmarshal(raw, &list) == nil {
		return list, nil
	}
	var single string
	if json.Unmarshal(raw, &single) == nil {
		return []string{single}, nil
	}
	return nil, fmt.Errorf("route exclusions must contain IPs, CIDRs or DNS names")
}

func tunGatewayName(text string) (string, error) {
	if len(text) > 253 || strings.TrimSpace(text) != text || strings.ContainsAny(text, "/\\:@*?#[]\x00") {
		return "", fmt.Errorf("TUN bypass: invalid gateway name; use a domain, IP or CIDR")
	}
	host, err := idna.Lookup.ToASCII(strings.TrimSuffix(text, "."))
	if err != nil || len(host) == 0 || len(host) > 253 {
		return "", fmt.Errorf("TUN bypass: invalid DNS gateway name")
	}
	host = strings.ToLower(host)
	for _, label := range strings.Split(host, ".") {
		if len(label) == 0 || len(label) > 63 || label[0] == '-' || label[len(label)-1] == '-' {
			return "", fmt.Errorf("TUN bypass: invalid DNS gateway name")
		}
		for _, ch := range label {
			if !(ch >= 'a' && ch <= 'z' || ch >= '0' && ch <= '9' || ch == '-') {
				return "", fmt.Errorf("TUN bypass: invalid DNS gateway name")
			}
		}
	}
	return host, nil
}
