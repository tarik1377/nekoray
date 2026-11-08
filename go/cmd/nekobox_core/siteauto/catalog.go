// Package siteauto validates the explicit, opt-in website availability catalog.
package siteauto

import (
	"errors"
	"net/netip"
	"strings"
)

type Catalog struct{ suffix, exact []string }

func validHost(host string) bool {
	if host == "" || len(host) > 253 || !strings.Contains(host, ".") {
		return false
	}
	if _, err := netip.ParseAddr(host); err == nil {
		return false
	}
	for _, label := range strings.Split(host, ".") {
		if len(label) == 0 || len(label) > 63 || label[0] == '-' || label[len(label)-1] == '-' {
			return false
		}
		for _, ch := range label {
			if !(ch >= 'a' && ch <= 'z' || ch >= '0' && ch <= '9' || ch == '-') {
				return false
			}
		}
	}
	labels := strings.Split(host, ".")
	last := labels[len(labels)-1]
	if last[0] < 'a' || last[0] > 'z' {
		return false
	}
	for _, tail := range []string{".local", ".lan", ".localhost", ".internal", ".home.arpa"} {
		if strings.HasSuffix(host, tail) {
			return false
		}
	}
	return true
}
func New(patterns []string) (*Catalog, error) {
	if len(patterns) == 0 || len(patterns) > 512 {
		return nil, errors.New("site auto requires 1..512 explicit domains")
	}
	c := &Catalog{}
	shared := []string{"cloudfront.net", "amazonaws.com", "cloudflare.net", "edgesuite.net", "fastly.net", "arkoselabs.com", "llnwi.net"}
	seen := map[string]bool{}
	for _, pattern := range patterns {
		full := strings.HasPrefix(pattern, "full:")
		suffix := strings.HasPrefix(pattern, "domain:")
		if !full && !suffix {
			return nil, errors.New("site auto domain needs domain: or full:")
		}
		host := strings.TrimPrefix(pattern, "domain:")
		if full {
			host = strings.TrimPrefix(pattern, "full:")
		}
		if !validHost(host) || host != strings.ToLower(host) || host == "co.uk" || host == "co.jp" || host == "com.au" {
			return nil, errors.New("invalid site auto host")
		}
		for _, provider := range shared {
			if host == provider || suffix && strings.HasSuffix(host, "."+provider) {
				return nil, errors.New("shared CDN must be an exact tenant name")
			}
		}
		if seen[pattern] {
			continue
		}
		seen[pattern] = true
		if full {
			c.exact = append(c.exact, host)
		} else {
			c.suffix = append(c.suffix, host)
		}
	}
	return c, nil
}
func (c *Catalog) Contains(host string) bool {
	host = strings.TrimSuffix(strings.ToLower(host), ".")
	if !validHost(host) {
		return false
	}
	for _, name := range c.exact {
		if name == host {
			return true
		}
	}
	for _, name := range c.suffix {
		if name == host || strings.HasSuffix(host, "."+name) {
			return true
		}
	}
	return false
}
func (c *Catalog) ProbeURL(host string) string {
	if !c.Contains(host) {
		return ""
	}
	return "https://" + strings.TrimSuffix(strings.ToLower(host), ".") + "/"
}
