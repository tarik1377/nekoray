package main

import (
	"encoding/json"
	"os"
	"testing"
)

// addRuleSet must prefer a bundled local .srs but fall back to a remote rule-set
// for any geo tag that is not shipped, so that referencing e.g. geoip:cn or
// geosite:google no longer points at a missing local file and breaks the config.
func TestAddRuleSetLocalVsRemoteFallback(t *testing.T) {
	t.Chdir(t.TempDir())

	if err := os.WriteFile("geoip-ru.srs", []byte("stub"), 0o644); err != nil {
		t.Fatalf("seed local .srs: %v", err)
	}

	var ruleSets []interface{}
	known := map[string]bool{}
	addRuleSet(&ruleSets, known, "geoip-ru", "https://example.invalid/geoip-ru.srs")
	addRuleSet(&ruleSets, known, "geosite-google", "https://example.invalid/geosite-google.srs")
	// duplicate tag must be ignored
	addRuleSet(&ruleSets, known, "geoip-ru", "https://example.invalid/geoip-ru.srs")

	if len(ruleSets) != 2 {
		t.Fatalf("expected 2 rule-sets, got %d", len(ruleSets))
	}

	local := ruleSets[0].(map[string]interface{})
	if local["type"] != "local" || local["path"] != "geoip-ru.srs" {
		t.Errorf("bundled tag should be local file, got %#v", local)
	}
	if _, hasURL := local["url"]; hasURL {
		t.Errorf("local rule-set must not carry a url: %#v", local)
	}

	remote := ruleSets[1].(map[string]interface{})
	if remote["type"] != "remote" {
		t.Fatalf("unshipped tag should be remote, got %#v", remote)
	}
	if remote["url"] != "https://example.invalid/geosite-google.srs" {
		t.Errorf("remote rule-set must carry the download url, got %#v", remote["url"])
	}
	if remote["download_detour"] != "proxy" {
		t.Errorf("remote rule-set should download via proxy, got %#v", remote["download_detour"])
	}
	if _, ok := remote["update_interval"]; !ok {
		t.Errorf("remote rule-set should set an update_interval: %#v", remote)
	}
}

func mustTransform(t *testing.T, in map[string]interface{}) map[string]interface{} {
	t.Helper()
	raw, err := json.Marshal(in)
	if err != nil {
		t.Fatalf("marshal input: %v", err)
	}
	out, err := transformConfigBytes(raw)
	if err != nil {
		t.Fatalf("transformConfigBytes: %v", err)
	}
	var m map[string]interface{}
	if err := json.Unmarshal(out, &m); err != nil {
		t.Fatalf("unmarshal output: %v", err)
	}
	return m
}

func firstRule(t *testing.T, m map[string]interface{}) map[string]interface{} {
	t.Helper()
	route := m["route"].(map[string]interface{})
	rules := route["rules"].([]interface{})
	return rules[0].(map[string]interface{})
}

func contains(arr []interface{}, want string) bool {
	for _, v := range arr {
		if s, ok := v.(string); ok && s == want {
			return true
		}
	}
	return false
}

// geosite/geoip in a route rule become rule_set references; geoip:private becomes
// ip_is_private instead of a (non-existent) rule-set.
func TestTransformGeoRules(t *testing.T) {
	t.Chdir(t.TempDir())
	m := mustTransform(t, map[string]interface{}{
		"route": map[string]interface{}{
			"rules": []interface{}{
				map[string]interface{}{
					"geoip":    []interface{}{"ru", "private"},
					"geosite":  "category-ads-all",
					"outbound": "direct",
				},
			},
		},
	})

	rule := firstRule(t, m)
	if _, ok := rule["geoip"]; ok {
		t.Error("geoip key should be removed from rule")
	}
	if _, ok := rule["geosite"]; ok {
		t.Error("geosite key should be removed from rule")
	}
	if rule["ip_is_private"] != true {
		t.Errorf("geoip:private should become ip_is_private:true, got %#v", rule["ip_is_private"])
	}
	if rule["outbound"] != "direct" {
		t.Errorf("unrelated outbound must be preserved, got %#v", rule["outbound"])
	}
	refs := rule["rule_set"].([]interface{})
	if !contains(refs, "geoip-ru") || !contains(refs, "geosite-category-ads-all") {
		t.Errorf("expected rule_set refs for geoip-ru and geosite-category-ads-all, got %#v", refs)
	}
	if contains(refs, "geoip-private") {
		t.Error("private must not produce a rule_set reference")
	}

	route := m["route"].(map[string]interface{})
	defs := route["rule_set"].([]interface{})
	if len(defs) != 2 {
		t.Errorf("expected 2 rule_set definitions, got %d (%#v)", len(defs), defs)
	}
}

// The legacy dns/block outbounds are removed and their route rules rewritten to
// the 1.13 action form.
func TestTransformDNSAndBlockOutbounds(t *testing.T) {
	m := mustTransform(t, map[string]interface{}{
		"outbounds": []interface{}{
			map[string]interface{}{"type": "dns", "tag": "dns-out"},
			map[string]interface{}{"type": "block", "tag": "block"},
			map[string]interface{}{"type": "direct", "tag": "direct"},
		},
		"route": map[string]interface{}{
			"rules": []interface{}{
				map[string]interface{}{"protocol": "dns", "outbound": "dns-out"},
				map[string]interface{}{"domain": "ads.example", "outbound": "block"},
			},
		},
	})

	obs := m["outbounds"].([]interface{})
	if len(obs) != 1 {
		t.Fatalf("dns and block outbounds should be removed, got %#v", obs)
	}
	if obs[0].(map[string]interface{})["type"] != "direct" {
		t.Errorf("only the direct outbound should remain, got %#v", obs[0])
	}

	rules := m["route"].(map[string]interface{})["rules"].([]interface{})
	dnsRule := rules[0].(map[string]interface{})
	if dnsRule["action"] != "hijack-dns" {
		t.Errorf("dns-out rule should become action hijack-dns, got %#v", dnsRule)
	}
	if _, ok := dnsRule["outbound"]; ok {
		t.Error("dns-out rule should drop the outbound key")
	}
	blockRule := rules[1].(map[string]interface{})
	if blockRule["action"] != "reject" {
		t.Errorf("block rule should become action reject, got %#v", blockRule)
	}
	if _, ok := blockRule["outbound"]; ok {
		t.Error("block rule should drop the outbound key")
	}
}

// Legacy inbound fields are stripped, TUN gvisor stack is forced to mixed, legacy
// TUN address fields are merged into address[], and sniff:true yields a prepended
// {action:sniff} route rule.
func TestTransformInboundsSniffAndTun(t *testing.T) {
	m := mustTransform(t, map[string]interface{}{
		"inbounds": []interface{}{
			map[string]interface{}{
				"type": "mixed", "tag": "mixed-in",
				"sniff": true, "sniff_override_destination": true, "domain_strategy": "prefer_ipv4",
			},
			map[string]interface{}{
				"type": "tun", "tag": "tun-in", "stack": "gvisor",
				"inet4_address": "172.19.0.1/28", "gso": true,
				"inet4_route_address": []interface{}{"0.0.0.0/0"},
			},
		},
		"route": map[string]interface{}{
			"rules": []interface{}{map[string]interface{}{"outbound": "proxy"}},
		},
	})

	ibs := m["inbounds"].([]interface{})
	mixed := ibs[0].(map[string]interface{})
	for _, k := range []string{"sniff", "sniff_override_destination", "domain_strategy"} {
		if _, ok := mixed[k]; ok {
			t.Errorf("legacy inbound field %q should be removed", k)
		}
	}
	tun := ibs[1].(map[string]interface{})
	if tun["stack"] != "mixed" {
		t.Errorf("gvisor stack should be forced to mixed, got %#v", tun["stack"])
	}
	if _, ok := tun["inet4_address"]; ok {
		t.Error("legacy inet4_address should be removed")
	}
	if _, ok := tun["gso"]; ok {
		t.Error("legacy gso should be removed")
	}
	if _, ok := tun["inet4_route_address"]; ok {
		t.Error("legacy inet4_route_address should be removed")
	}
	addr := tun["address"].([]interface{})
	if len(addr) != 1 || addr[0] != "172.19.0.1/28" {
		t.Errorf("inet4_address should be merged into address[], got %#v", addr)
	}

	rules := m["route"].(map[string]interface{})["rules"].([]interface{})
	if rules[0].(map[string]interface{})["action"] != "sniff" {
		t.Errorf("sniff:true should prepend an {action:sniff} rule, got %#v", rules[0])
	}
	if rules[1].(map[string]interface{})["outbound"] != "proxy" {
		t.Errorf("original rules should be preserved after the sniff rule, got %#v", rules[1])
	}
}

// Malformed JSON must be returned unchanged with no error so a bad config never
// crashes the transform (the core then surfaces the real parse error).
func TestTransformInvalidJSONPassthrough(t *testing.T) {
	in := []byte("{not valid json")
	out, err := transformConfigBytes(in)
	if err != nil {
		t.Fatalf("invalid JSON should not error, got %v", err)
	}
	if string(out) != string(in) {
		t.Errorf("invalid JSON should pass through unchanged, got %q", out)
	}
}

// v2ray_api stats must be injected for all outbound tags so the GUI traffic/speed
// display works (QueryStats reads these counters).
func TestTransformInjectsV2RayStats(t *testing.T) {
	m := mustTransform(t, map[string]interface{}{
		"outbounds": []interface{}{
			map[string]interface{}{"type": "vless", "tag": "proxy"},
			map[string]interface{}{"type": "direct", "tag": "direct"},
		},
	})
	exp, ok := m["experimental"].(map[string]interface{})
	if !ok {
		t.Fatalf("experimental section missing: %#v", m["experimental"])
	}
	api, ok := exp["v2ray_api"].(map[string]interface{})
	if !ok {
		t.Fatalf("v2ray_api missing: %#v", exp)
	}
	if s, _ := api["listen"].(string); s == "" {
		t.Errorf("v2ray_api.listen must be a non-empty address, got %#v", api["listen"])
	}
	stats, ok := api["stats"].(map[string]interface{})
	if !ok || stats["enabled"] != true {
		t.Fatalf("stats.enabled must be true, got %#v", api["stats"])
	}
	tags, _ := stats["outbounds"].([]interface{})
	if !contains(tags, "proxy") || !contains(tags, "direct") {
		t.Errorf("stats.outbounds should list all outbound tags, got %#v", tags)
	}
}

// An explicit experimental.v2ray_api must be preserved (no override).
func TestTransformPreservesExistingV2RayApi(t *testing.T) {
	m := mustTransform(t, map[string]interface{}{
		"outbounds":    []interface{}{map[string]interface{}{"type": "direct", "tag": "direct"}},
		"experimental": map[string]interface{}{"v2ray_api": map[string]interface{}{"listen": "keep-me"}},
	})
	api := m["experimental"].(map[string]interface{})["v2ray_api"].(map[string]interface{})
	if api["listen"] != "keep-me" {
		t.Errorf("existing v2ray_api must be preserved, got %#v", api)
	}
}

// gvisor/empty -> mixed (robust default); an explicit "system" is honoured
// (native UDP for voice/gaming).
func TestNormalizeTunStack(t *testing.T) {
	cases := []struct{ stack, want string }{
		{"", "mixed"},
		{"gvisor", "mixed"},
		{"system", "system"},
		{"mixed", "mixed"},
	}
	for _, c := range cases {
		if got := normalizeTunStack(c.stack); got != c.want {
			t.Errorf("normalizeTunStack(%q) = %q, want %q", c.stack, got, c.want)
		}
	}
}
