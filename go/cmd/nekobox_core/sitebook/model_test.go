package sitebook

import (
	"strings"
	"testing"
)

func TestURLSanitizationAndEndpointBoundaries(t *testing.T) {
	nav, origin, err := Normalize("https://ExAmPle.com:443/a/b?token=private#fragment")
	if err != nil || nav != "https://example.com/a/b" || origin != "https://example.com" {
		t.Fatal(nav, origin, err)
	}
	for _, value := range []string{"file:///etc/passwd", "http://localhost/", "https://user:password@example.com/", "http://127.0.0.1/", "http://169.254.169.254/", "http://10.44.0.1/", "https://foo.lan/", "http://[::1]/"} {
		if _, _, err := Normalize(value); err == nil {
			t.Fatal("unsafe root", value)
		}
	}
	raw := `{"schema":1,"sites":[{"id":"game","title":"Roblox","url":"https://www.roblox.com","mode":"auto","enabled":true,"endpoints":["https://www.roblox.com","https://one.cloudfront.net"]},{"id":"other","title":"Other","url":"https://other.example.org","mode":"proxy","enabled":true,"endpoints":["https://other.example.org","https://one.cloudfront.net"]}]}`
	_, policy, err := Parse(raw)
	if err != nil {
		t.Fatal(err)
	}
	shared := policy.Items[Key("one.cloudfront.net", 443)]
	if shared.Mode != "proxy" || len(shared.SiteIDs) != 2 {
		t.Fatal("shared dependency precedence", shared)
	}
	if _, ok := policy.Items[Key("other.cloudfront.net", 443)]; ok {
		t.Fatal("CDN wildcard leak")
	}
	if _, ok := policy.Items[Key("www.roblox.com", 3333)]; ok {
		t.Fatal("website captured SSH")
	}
	if _, _, err := Parse(strings.ReplaceAll(raw, `"schema":1`, `"schema":9`)); err == nil {
		t.Fatal("future schema overwritten")
	}
}
