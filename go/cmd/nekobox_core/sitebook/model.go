// Package sitebook describes URL-based, explicitly saved services. It never
// stores browsing sessions, request paths, cookies, headers or response bodies.
package sitebook

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net"
	"net/netip"
	"net/url"
	"sort"
	"strconv"
	"strings"

	"golang.org/x/net/idna"
)

type Site struct {
	ID        string   `json:"id"`
	Title     string   `json:"title"`
	URL       string   `json:"url"`
	Mode      string   `json:"mode"`
	Enabled   bool     `json:"enabled"`
	Endpoints []string `json:"endpoints"`
}
type Library struct {
	Schema int    `json:"schema"`
	Sites  []Site `json:"sites"`
}
type Policy struct {
	Origin, Host string
	Port         uint16
	Mode         string
	SiteIDs      []string
}
type Policies struct {
	Items  map[string]Policy
	Digest string
}

func PublicIP(ip netip.Addr) bool {
	ip = ip.Unmap()
	if !ip.IsValid() || !ip.IsGlobalUnicast() || ip.IsPrivate() || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
		return false
	}
	return !netip.MustParsePrefix("100.64.0.0/10").Contains(ip)
}
func Host(raw string) (string, error) {
	host := strings.TrimSuffix(strings.ToLower(raw), ".")
	if ip, err := netip.ParseAddr(host); err == nil {
		if !PublicIP(ip) {
			return "", errors.New("internal address")
		}
		return ip.Unmap().String(), nil
	}
	host, err := idna.Lookup.ToASCII(host)
	if err != nil || len(host) > 253 || !strings.Contains(host, ".") {
		return "", errors.New("invalid public hostname")
	}
	for _, suffix := range []string{".local", ".lan", ".localhost", ".internal", ".home.arpa"} {
		if strings.HasSuffix(host, suffix) {
			return "", errors.New("internal hostname")
		}
	}
	for _, label := range strings.Split(host, ".") {
		if label == "" || len(label) > 63 || label[0] == '-' || label[len(label)-1] == '-' {
			return "", errors.New("invalid hostname label")
		}
		for _, ch := range label {
			if !(ch >= 'a' && ch <= 'z' || ch >= '0' && ch <= '9' || ch == '-') {
				return "", errors.New("invalid hostname character")
			}
		}
	}
	return host, nil
}

// Normalize retains the entered navigation URL only for this isolated load;
// Origin is the only URL identity persisted in a library/history. Userinfo is
// rejected; URL paths, queries and SPA fragments are not saved.
func Normalize(raw string) (navigation, origin string, err error) {
	raw = strings.TrimSpace(raw)
	if len(raw) == 0 || len(raw) > 4096 {
		return "", "", errors.New("URL is empty or too long")
	}
	if !strings.Contains(raw, "://") {
		raw = "https://" + raw
	}
	u, e := url.Parse(raw)
	if e != nil || u.User != nil || u.Opaque != "" {
		return "", "", errors.New("invalid URL or embedded credentials")
	}
	if u.Scheme == "wss" {
		u.Scheme = "https"
	}
	if u.Scheme == "ws" {
		u.Scheme = "http"
	}
	if u.Scheme != "https" && u.Scheme != "http" {
		return "", "", errors.New("use an HTTP or HTTPS URL")
	}
	host, e := Host(u.Hostname())
	if e != nil {
		return "", "", e
	}
	port := u.Port()
	if port != "" {
		p, e := strconv.ParseUint(port, 10, 16)
		if e != nil || p == 0 {
			return "", "", errors.New("invalid URL port")
		}
	}
	if u.Scheme == "https" && port == "443" || u.Scheme == "http" && port == "80" {
		port = ""
	}
	authority := host
	if strings.Contains(host, ":") {
		authority = "[" + host + "]"
	}
	if port != "" {
		authority = net.JoinHostPort(host, port)
	}
	u.Scheme = strings.ToLower(u.Scheme)
	u.Host = authority
	if u.Path == "" {
		u.Path = "/"
	}
	origin = u.Scheme + "://" + authority
	return u.String(), origin, nil
}
func Address(origin string) (string, uint16) {
	u, _ := url.Parse(origin)
	if u == nil {
		return "", 0
	}
	p := uint64(443)
	if u.Scheme == "http" {
		p = 80
	}
	if u.Port() != "" {
		p, _ = strconv.ParseUint(u.Port(), 10, 16)
	}
	return u.Hostname(), uint16(p)
}
func Key(host string, port uint16) string {
	return net.JoinHostPort(strings.ToLower(strings.TrimSuffix(host, ".")), strconv.Itoa(int(port)))
}
func priority(mode string) int {
	switch mode {
	case "proxy":
		return 3
	case "direct":
		return 2
	case "auto":
		return 1
	}
	return 0
}
func Parse(raw string) (Library, *Policies, error) {
	lib := Library{Schema: 1, Sites: []Site{}}
	if raw != "" {
		if len(raw) > 2<<20 {
			return lib, nil, errors.New("saved sites exceed size limit")
		}
		if err := json.Unmarshal([]byte(raw), &lib); err != nil {
			return lib, nil, errors.New("invalid saved sites JSON")
		}
	}
	if lib.Schema != 1 || len(lib.Sites) > 32 {
		return lib, nil, errors.New("unsupported saved sites schema or too many sites")
	}
	policies := &Policies{Items: map[string]Policy{}}
	ids := map[string]bool{}
	for _, site := range lib.Sites {
		if site.ID == "" || len(site.ID) > 64 || ids[site.ID] {
			return lib, nil, errors.New("invalid or repeated site identity")
		}
		ids[site.ID] = true
		for _, ch := range site.ID {
			if !(ch >= 'a' && ch <= 'z' || ch >= 'A' && ch <= 'Z' || ch >= '0' && ch <= '9' || ch == '-' || ch == '_') {
				return lib, nil, errors.New("invalid site identity")
			}
		}
		if priority(site.Mode) == 0 || len(site.Endpoints) == 0 || len(site.Endpoints) > 128 || len(site.Title) > 512 {
			return lib, nil, errors.New("invalid site mode, label or endpoint count")
		}
		_, root, err := Normalize(site.URL)
		if err != nil || root != site.URL {
			return lib, nil, errors.New("site URL must be a sanitized origin")
		}
		hasRoot := false
		seen := map[string]bool{}
		for _, origin := range site.Endpoints {
			_, normal, err := Normalize(origin)
			if err != nil || normal != origin || seen[origin] {
				return lib, nil, errors.New("invalid or repeated service endpoint")
			}
			seen[origin] = true
			hasRoot = hasRoot || origin == root
			if !site.Enabled {
				continue
			}
			host, port := Address(origin)
			key := Key(host, port)
			old := policies.Items[key]
			ids := append(old.SiteIDs, site.ID)
			if priority(site.Mode) > priority(old.Mode) || old.Origin == "" {
				old = Policy{Origin: origin, Host: host, Port: port, Mode: site.Mode}
			}
			old.SiteIDs = ids
			policies.Items[key] = old
		}
		if !hasRoot {
			return lib, nil, errors.New("service root missing from its endpoints")
		}
	}
	if len(policies.Items) > 4096 {
		return lib, nil, errors.New("too many active endpoints")
	}
	keys := make([]string, 0, len(policies.Items))
	for key := range policies.Items {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	ordered := make([]Policy, 0, len(keys))
	for _, key := range keys {
		ordered = append(ordered, policies.Items[key])
	}
	data, _ := json.Marshal(ordered)
	sum := sha256.Sum256(data)
	policies.Digest = hex.EncodeToString(sum[:])
	return lib, policies, nil
}

type Sample struct {
	Requests int  `json:"requests"`
	OK       int  `json:"ok"`
	Failed   int  `json:"failed"`
	Status   int  `json:"status,omitempty"`
	Document bool `json:"document,omitempty"`
}
type Endpoint struct {
	Origin string   `json:"origin"`
	Types  []string `json:"types"`
	Direct Sample   `json:"direct"`
	Proxy  Sample   `json:"proxy"`
}
type Report struct {
	URL            string     `json:"url"`
	Title          string     `json:"title"`
	Engine         string     `json:"engine"`
	Endpoints      []Endpoint `json:"endpoints"`
	Partial        bool       `json:"partial"`
	DirectOK       bool       `json:"direct_ok"`
	ProxyOK        bool       `json:"proxy_ok"`
	Recommendation string     `json:"recommendation"`
	Warning        string     `json:"warning,omitempty"`
}

func Choose(direct, proxy Sample) string {
	if direct.OK > 0 && direct.Failed == 0 {
		return "direct"
	}
	if proxy.OK > 0 && proxy.Failed == 0 {
		return "proxy"
	}
	return "unknown"
}
