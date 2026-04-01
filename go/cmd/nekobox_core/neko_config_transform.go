package main

// Complete config transformer: legacy sing-box (1.9.x) → sing-box 1.13.5
// Handles: geosite/geoip→rule_set, legacy DNS, legacy inbounds, dns outbound,
// empty fields cleanup, deprecated options removal.

import (
	"encoding/json"
	"fmt"
)

const (
	geositeURLBase = "https://raw.githubusercontent.com/SagerNet/sing-geosite/rule-set/geosite-%s.srs"
	geoipURLBase   = "https://raw.githubusercontent.com/SagerNet/sing-geoip/rule-set/geoip-%s.srs"
)

func transformConfigBytes(configJSON []byte) ([]byte, error) {
	var config map[string]interface{}
	if err := json.Unmarshal(configJSON, &config); err != nil {
		return configJSON, nil
	}

	ruleSets := make([]interface{}, 0)
	tags := make(map[string]bool)

	// 1. Clean route: remove deprecated geoip/geosite DB paths
	if route, ok := config["route"].(map[string]interface{}); ok {
		delete(route, "geoip")
		delete(route, "geosite")
		if rules, ok := route["rules"].([]interface{}); ok {
			for _, r := range rules {
				if rm, ok := r.(map[string]interface{}); ok {
					convertGeoRule(rm, &ruleSets, tags)
					cleanEmptyArrays(rm)
				}
			}
		}
	}

	// 2. Clean DNS rules
	if dns, ok := config["dns"].(map[string]interface{}); ok {
		if rules, ok := dns["rules"].([]interface{}); ok {
			for _, r := range rules {
				if rm, ok := r.(map[string]interface{}); ok {
					convertGeoRule(rm, &ruleSets, tags)
					cleanEmptyArrays(rm)
				}
			}
		}
	}

	// 3. Add rule_set definitions
	if len(ruleSets) > 0 {
		route := ensureMap(config, "route")
		existing, _ := route["rule_set"].([]interface{})
		route["rule_set"] = append(existing, ruleSets...)
	}

	// 4. Remove legacy inbound fields, add sniff route action
	transformInbounds(config)

	// 5. Remove dns outbound, convert dns rules to hijack-dns action
	transformDNSOutbound(config)

	// 6. Convert block outbound to reject action (deprecated in 1.13)
	transformBlockOutbound(config)

	// 7. Clean outbounds: remove empty strings, deprecated fields
	cleanOutbounds(config)

	return json.Marshal(config)
}

// --- Inbounds ---

func transformInbounds(config map[string]interface{}) {
	inbounds, ok := config["inbounds"].([]interface{})
	if !ok {
		return
	}

	needSniff := false
	for _, ib := range inbounds {
		ibMap, ok := ib.(map[string]interface{})
		if !ok {
			continue
		}
		if sniff, ok := ibMap["sniff"].(bool); ok && sniff {
			needSniff = true
		}
		// All legacy InboundOptions fields — removed in 1.13
		delete(ibMap, "sniff")
		delete(ibMap, "sniff_override_destination")
		delete(ibMap, "sniff_timeout")
		delete(ibMap, "domain_strategy")
		delete(ibMap, "udp_disable_domain_unmapping")
		delete(ibMap, "endpoint_independent_nat")

		// Legacy TUN address fields — removed in 1.12
		// inet4_address/inet6_address → address (combined list)
		if ibMap["type"] == "tun" {
			var addresses []interface{}
			if v, ok := ibMap["inet4_address"]; ok {
				switch val := v.(type) {
				case string:
					addresses = append(addresses, val)
				case []interface{}:
					addresses = append(addresses, val...)
				}
				delete(ibMap, "inet4_address")
			}
			if v, ok := ibMap["inet6_address"]; ok {
				switch val := v.(type) {
				case string:
					addresses = append(addresses, val)
				case []interface{}:
					addresses = append(addresses, val...)
				}
				delete(ibMap, "inet6_address")
			}
			if len(addresses) > 0 {
				ibMap["address"] = addresses
			}
			// Also remove other legacy TUN fields
			delete(ibMap, "inet4_route_address")
			delete(ibMap, "inet6_route_address")
			delete(ibMap, "inet4_route_exclude_address")
			delete(ibMap, "inet6_route_exclude_address")
			delete(ibMap, "gso")
		}
	}

	if needSniff {
		route := ensureMap(config, "route")
		rules, _ := route["rules"].([]interface{})
		// Prepend sniff action
		newRules := make([]interface{}, 0, len(rules)+1)
		newRules = append(newRules, map[string]interface{}{"action": "sniff"})
		newRules = append(newRules, rules...)
		route["rules"] = newRules
	}
}

// --- DNS outbound ---

func transformDNSOutbound(config map[string]interface{}) {
	// Remove dns type outbound
	if outbounds, ok := config["outbounds"].([]interface{}); ok {
		filtered := make([]interface{}, 0, len(outbounds))
		for _, ob := range outbounds {
			if obMap, ok := ob.(map[string]interface{}); ok {
				if obMap["type"] == "dns" {
					continue
				}
			}
			filtered = append(filtered, ob)
		}
		config["outbounds"] = filtered
	}

	// Convert route rules: outbound=dns-out → action=hijack-dns
	if route, ok := config["route"].(map[string]interface{}); ok {
		if rules, ok := route["rules"].([]interface{}); ok {
			for _, r := range rules {
				if rm, ok := r.(map[string]interface{}); ok {
					if rm["outbound"] == "dns-out" {
						rm["action"] = "hijack-dns"
						delete(rm, "outbound")
					}
				}
			}
		}
	}
}

// --- Outbounds ---

func cleanOutbounds(config map[string]interface{}) {
	outbounds, ok := config["outbounds"].([]interface{})
	if !ok {
		return
	}
	for _, ob := range outbounds {
		obMap, ok := ob.(map[string]interface{})
		if !ok {
			continue
		}
		// Remove empty string fields
		removeEmptyStrings(obMap, "domain_strategy", "flow", "packet_encoding")
	}
}

// --- Geosite/GeoIP → rule_set ---

func convertGeoRule(rule map[string]interface{}, ruleSets *[]interface{}, known map[string]bool) {
	var refs []interface{}
	if existing, ok := rule["rule_set"].([]interface{}); ok {
		refs = append(refs, existing...)
	}

	if v, ok := rule["geosite"]; ok {
		for _, name := range asStringSlice(v) {
			tag := "geosite-" + name
			refs = append(refs, tag)
			addRuleSet(ruleSets, known, tag, fmt.Sprintf(geositeURLBase, name))
		}
		delete(rule, "geosite")
	}

	if v, ok := rule["geoip"]; ok {
		for _, name := range asStringSlice(v) {
			if name == "private" {
				rule["ip_is_private"] = true
				continue
			}
			tag := "geoip-" + name
			refs = append(refs, tag)
			addRuleSet(ruleSets, known, tag, fmt.Sprintf(geoipURLBase, name))
		}
		delete(rule, "geoip")
	}

	if v, ok := rule["source_geoip"]; ok {
		for _, name := range asStringSlice(v) {
			if name == "private" {
				rule["source_ip_is_private"] = true
				continue
			}
			tag := "geoip-" + name
			refs = append(refs, tag)
			addRuleSet(ruleSets, known, tag, fmt.Sprintf(geoipURLBase, name))
		}
		delete(rule, "source_geoip")
	}

	if len(refs) > 0 {
		rule["rule_set"] = refs
	}
}

// --- Helpers ---

func addRuleSet(ruleSets *[]interface{}, known map[string]bool, tag, url string) {
	if !known[tag] {
		known[tag] = true
		localPath := tag + ".srs"
		*ruleSets = append(*ruleSets, map[string]interface{}{
			"tag":    tag,
			"type":   "local",
			"format": "binary",
			"path":   localPath,
		})
	}
}

func ensureMap(config map[string]interface{}, key string) map[string]interface{} {
	m, ok := config[key].(map[string]interface{})
	if !ok {
		m = make(map[string]interface{})
		config[key] = m
	}
	return m
}

func cleanEmptyArrays(m map[string]interface{}) {
	for k, v := range m {
		if arr, ok := v.([]interface{}); ok && len(arr) == 0 {
			delete(m, k)
		}
	}
}

func removeEmptyStrings(m map[string]interface{}, keys ...string) {
	for _, k := range keys {
		if v, ok := m[k].(string); ok && v == "" {
			delete(m, k)
		}
	}
}

func asStringSlice(v interface{}) []string {
	switch val := v.(type) {
	case string:
		return []string{val}
	case []interface{}:
		out := make([]string, 0, len(val))
		for _, item := range val {
			if s, ok := item.(string); ok {
				out = append(out, s)
			}
		}
		return out
	}
	return nil
}

// --- Block outbound ---

func transformBlockOutbound(config map[string]interface{}) {
	// Remove block type outbound
	if outbounds, ok := config["outbounds"].([]interface{}); ok {
		filtered := make([]interface{}, 0, len(outbounds))
		for _, ob := range outbounds {
			if obMap, ok := ob.(map[string]interface{}); ok {
				if obMap["type"] == "block" {
					continue
				}
			}
			filtered = append(filtered, ob)
		}
		config["outbounds"] = filtered
	}
	// Convert route rules: outbound=block → action=reject
	if route, ok := config["route"].(map[string]interface{}); ok {
		if rules, ok := route["rules"].([]interface{}); ok {
			for _, r := range rules {
				if rm, ok := r.(map[string]interface{}); ok {
					if rm["outbound"] == "block" {
						rm["action"] = "reject"
						delete(rm, "outbound")
					}
				}
			}
		}
	}
}
