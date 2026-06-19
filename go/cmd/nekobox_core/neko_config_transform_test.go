package main

import (
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
