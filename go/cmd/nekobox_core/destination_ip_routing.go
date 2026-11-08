package main

import "strings"
import "net/netip"

// resolveBeforeDestinationIPRules restores domain-to-IP routing after the
// removal of legacy inbound.domain_strategy in sing-box 1.13. Inserting at the
// first destination-IP rule (rather than at the front) leaves earlier explicit
// domain/process VPN, reject and DNS-interception rules in charge. This is a
// runtime conversion only: saved routes and DNS servers are never rewritten.
func resolveBeforeDestinationIPRules(config map[string]interface{}) {
	route, ok := config["route"].(map[string]interface{})
	if !ok {
		return
	}
	rules, _ := route["rules"].([]interface{})
	definitions := make(map[string]map[string]interface{})
	for _, raw := range interfaceList(route["rule_set"]) {
		if definition, ok := raw.(map[string]interface{}); ok {
			if tag, ok := definition["tag"].(string); ok {
				definitions[tag] = definition
			}
		}
	}
	for index, raw := range rules {
		rule, ok := raw.(map[string]interface{})
		if !ok || !hasDestinationIPMatch(rule, definitions, map[string]bool{}) {
			continue
		}
		if index > 0 {
			previous, _ := rules[index-1].(map[string]interface{})
			if unconditionalResolve(previous) {
				return // Honour an existing resolver/strategy; also makes this idempotent.
			}
		}
		converted := make([]interface{}, 0, len(rules)+1)
		converted = append(converted, rules[:index]...)
		// No server or strategy override: use existing split-DNS routing and
		// dns.strategy, including corporate/private zones and IPv6 preferences.
		converted = append(converted, map[string]interface{}{"action": "resolve"})
		converted = append(converted, rules[index:]...)
		route["rules"] = converted
		return
	}
}

func hasDestinationIPMatch(rule map[string]interface{}, definitions map[string]map[string]interface{}, visiting map[string]bool) bool {
	// An exact-IP service guard explicitly excludes DNS names. Resolving all
	// names before it would alter the final fallback and break proxy-side DNS.
	if rule["type"] == "logical" && rule["mode"] == "and" && rule["invert"] != true {
		for _, raw := range interfaceList(rule["rules"]) {
			if child, ok := raw.(map[string]interface{}); ok && isLiteralIPNameGuard(child) {
				return false
			}
		}
	}
	for _, field := range []string{"ip_cidr", "geoip"} {
		if len(asStringSlice(rule[field])) > 0 {
			return true
		}
	}
	if rule["ip_is_private"] == true {
		return true
	}
	if rule["type"] == "logical" {
		for _, raw := range interfaceList(rule["rules"]) {
			if child, ok := raw.(map[string]interface{}); ok && hasDestinationIPMatch(child, definitions, visiting) {
				return true
			}
		}
	}
	if rule["rule_set_ip_cidr_match_source"] == true {
		return false
	}
	for _, tag := range asStringSlice(rule["rule_set"]) {
		if visiting[tag] {
			continue
		}
		definition, exists := definitions[tag]
		if !exists || definition["type"] != "inline" {
			// Standard country sets, including the managed and hot-reloaded RU
			// set, contain destination IPs. Unrelated external sets are left alone.
			if strings.HasPrefix(tag, "geoip-") {
				return true
			}
			continue
		}
		visiting[tag] = true
		for _, raw := range interfaceList(definition["rules"]) {
			if child, ok := raw.(map[string]interface{}); ok && hasDestinationIPMatch(child, definitions, visiting) {
				delete(visiting, tag)
				return true
			}
		}
		delete(visiting, tag)
	}
	return false
}

func isLiteralIPNameGuard(rule map[string]interface{}) bool {
	if rule["type"] != "logical" || rule["mode"] != "or" || rule["invert"] == true {
		return false
	}
	children := interfaceList(rule["rules"])
	if len(children) != 2 {
		return false
	}
	empty, names := false, false
	for _, raw := range children {
		child, ok := raw.(map[string]interface{})
		if !ok {
			return false
		}
		if child["invert"] == true {
			patterns := asStringSlice(child["domain_regex"])
			if len(patterns) != 1 || patterns[0] != ".+" {
				return false
			}
			for key := range child {
				if key != "type" && key != "invert" && key != "domain_regex" {
					return false
				}
			}
			empty = true
		} else {
			values := asStringSlice(child["domain"])
			if len(values) == 0 {
				return false
			}
			for key := range child {
				if key != "type" && key != "domain" {
					return false
				}
			}
			for _, name := range values {
				if _, err := netip.ParseAddr(name); err != nil {
					return false
				}
			}
			names = true
		}
	}
	return empty && names
}

func unconditionalResolve(rule map[string]interface{}) bool {
	if rule["action"] != "resolve" {
		return false
	}
	for key := range rule {
		switch key {
		case "action", "server", "strategy", "disable_cache", "rewrite_ttl", "client_subnet":
		default:
			return false
		}
	}
	return true
}

func interfaceList(value interface{}) []interface{} {
	list, _ := value.([]interface{})
	return list
}
