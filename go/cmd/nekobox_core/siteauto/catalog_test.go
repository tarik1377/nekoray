package siteauto_test

import (
	"encoding/json"
	"nekobox_core/robloxauto"
	"nekobox_core/siteauto"
	"os"
	"testing"
)

func TestExactSuffixAndValidation(t *testing.T) {
	catalog, err := siteauto.New([]string{"domain:example.com", "full:one.cloudfront.net"})
	if err != nil {
		t.Fatal(err)
	}
	for _, host := range []string{"example.com", "A.example.com.", "one.cloudfront.net"} {
		if !catalog.Contains(host) {
			t.Fatal("missing match", host)
		}
	}
	for _, host := range []string{"evil-example.com", "example.com.evil.net", "other.cloudfront.net", "child.one.cloudfront.net", "a..example.com", "127.0.0.1"} {
		if catalog.Contains(host) {
			t.Fatal("overbroad match", host)
		}
	}
	for _, entry := range []string{"domain:cloudfront.net", "domain:one.cloudfront.net", "full:amazonaws.com", "domain:co.uk", "full:127.0.0.1", "domain:foo.local", "domain:foo.lan", "domain:one..com", "domain:EXAMPLE.COM", "regexp:.*", "https://example.com", "domain:com"} {
		if _, err := siteauto.New([]string{entry}); err == nil {
			t.Fatal("unsafe selector", entry)
		}
	}
	if catalog.ProbeURL("evil.com") != "" || catalog.ProbeURL("example.com") != "https://example.com/" {
		t.Fatal("probe allowlist failed")
	}
}
func TestSuppliedRobloxCatalog(t *testing.T) {
	data, err := os.ReadFile("../testdata/site_preset.json")
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	if err = json.Unmarshal(data, &names); err != nil {
		t.Fatal(err)
	}
	catalog, err := siteauto.New(names)
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range names {
		host := name[7:]
		if name[:5] == "full:" {
			host = name[5:]
		}
		if !catalog.Contains(host) || !robloxauto.KnownHost(host) {
			t.Fatal("GUI/core preset drift", name)
		}
	}
	for _, host := range []string{"users.roblox.com", "accountinformation.roblox.com", "apis.rbxcdn.com", "atl4-128-116-2-3.roblox.com", "tr.rbxcdn.com.edgesuite.net"} {
		if !catalog.Contains(host) {
			t.Fatal("supplied subdomain not covered", host)
		}
	}
	for _, host := range []string{"unrelated.cloudfront.net", "other.arkoselabs.com", "chatgpt.com", "gitlab.private.example.org"} {
		if catalog.Contains(host) {
			t.Fatal("unrelated site captured", host)
		}
	}
}
