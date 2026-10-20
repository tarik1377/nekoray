package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"reflect"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/miekg/dns"
	"golang.org/x/net/proxy"
)

func TestDestinationIPResolvePlacement(t *testing.T) {
	for _, tc := range []struct {
		name string
		rule map[string]interface{}
		want bool
	}{
		{"legacy country", map[string]interface{}{"geoip": "ru"}, true},
		{"managed country", map[string]interface{}{"rule_set": "geoip-ru"}, true},
		{"IPv6 CIDR", map[string]interface{}{"ip_cidr": "2001:db8::/32"}, true},
		{"private destination", map[string]interface{}{"ip_is_private": true}, true},
		{"source IP only", map[string]interface{}{"source_ip_cidr": "10.0.0.0/8"}, false},
		{"source country only", map[string]interface{}{"source_geoip": "ru"}, false},
		{"source country set", map[string]interface{}{"rule_set": "geoip-ru", "rule_set_ip_cidr_match_source": true}, false},
		{"domain set", map[string]interface{}{"rule_set": "geosite-ru"}, false},
		{"domain only", map[string]interface{}{"domain_suffix": "example.com"}, false},
		{"logical IP", map[string]interface{}{"type": "logical", "mode": "and", "rules": []interface{}{map[string]interface{}{"rule_set": "geoip-ru"}}}, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			prefix := []interface{}{
				map[string]interface{}{"port": 53, "action": "hijack-dns"},
				map[string]interface{}{"domain": "explicit-vpn.test", "outbound": "proxy"},
				map[string]interface{}{"domain": "explicit-block.test", "action": "reject"},
				map[string]interface{}{"process_name": "custom.exe", "outbound": "proxy"},
			}
			config := map[string]interface{}{"dns": map[string]interface{}{"final": "office", "strategy": "ipv6_only"},
				"route": map[string]interface{}{"rules": append(append([]interface{}{}, prefix...), tc.rule), "final": "proxy"}}
			dnsBefore, _ := json.Marshal(config["dns"])
			resolveBeforeDestinationIPRules(config)
			rules := config["route"].(map[string]interface{})["rules"].([]interface{})
			if !reflect.DeepEqual(rules[:len(prefix)], prefix) {
				t.Fatal("explicit DNS/VPN/block priorities changed")
			}
			if (len(rules) == len(prefix)+2) != tc.want {
				t.Fatalf("unexpected insertion: %#v", rules)
			}
			if tc.want && !reflect.DeepEqual(rules[len(prefix)], map[string]interface{}{"action": "resolve"}) {
				t.Fatal("resolve must use existing split DNS and family policy")
			}
			before, _ := json.Marshal(config)
			resolveBeforeDestinationIPRules(config)
			after, _ := json.Marshal(config)
			if string(before) != string(after) {
				t.Fatal("repeated conversion added another resolve")
			}
			dnsAfter, _ := json.Marshal(config["dns"])
			if string(dnsBefore) != string(dnsAfter) {
				t.Fatal("DNS configuration changed")
			}
		})
	}
}

func TestDestinationIPExistingResolveAndInlineSets(t *testing.T) {
	explicit := map[string]interface{}{"action": "resolve", "server": "office", "strategy": "ipv6_only"}
	for _, previous := range []map[string]interface{}{explicit, {"action": "resolve", "inbound": "one-inbound"}} {
		config := map[string]interface{}{"route": map[string]interface{}{
			"rules":    []interface{}{previous, map[string]interface{}{"rule_set": "custom-ips", "outbound": "direct"}},
			"rule_set": []interface{}{map[string]interface{}{"tag": "custom-ips", "type": "inline", "rules": []interface{}{map[string]interface{}{"ip_cidr": "178.130.128.0/20"}}}},
		}}
		resolveBeforeDestinationIPRules(config)
		rules := config["route"].(map[string]interface{})["rules"].([]interface{})
		want := 3
		if unconditionalResolve(previous) {
			want = 2
		}
		if len(rules) != want || !reflect.DeepEqual(rules[0], previous) {
			t.Fatal("existing explicit/conditional resolver was replaced or incorrectly reused")
		}
	}
}

// Real sing-box routing over loopback HTTP/SOCKS and UDP DNS, with no TUN,
// public endpoints, OS routes, installed client files, or subscription secrets.
func TestDestinationIPRoutingOverHTTPAndSOCKS(t *testing.T) {
	var publicQueries, officeQueries atomic.Int32
	newDNS := func(office bool) int {
		packet, err := net.ListenPacket("udp4", "127.0.0.1:0")
		if err != nil {
			t.Fatal(err)
		}
		server := &dns.Server{PacketConn: packet, Handler: dns.HandlerFunc(func(w dns.ResponseWriter, q *dns.Msg) {
			answer := new(dns.Msg).SetReply(q)
			if office {
				officeQueries.Add(1)
			} else {
				publicQueries.Add(1)
			}
			for _, question := range q.Question {
				if question.Qtype == dns.TypeAAAA {
					if strings.HasPrefix(question.Name, "ipv6-ru.") {
						answer.Answer = append(answer.Answer, &dns.AAAA{Hdr: dns.RR_Header{Name: question.Name, Rrtype: dns.TypeAAAA, Class: dns.ClassINET, Ttl: 60}, AAAA: net.ParseIP("2001:db8::1")})
					}
					continue
				}
				if question.Qtype != dns.TypeA || strings.HasPrefix(question.Name, "ipv6-ru.") {
					continue
				}
				ip := "178.130.128.26"
				if strings.HasPrefix(question.Name, "foreign.") {
					ip = "8.8.8.8"
				}
				if strings.HasSuffix(question.Name, ".office.test.") {
					if !office {
						answer.Rcode = dns.RcodeNameError
						continue
					}
					ip = "10.20.22.109"
				}
				answer.Answer = append(answer.Answer, &dns.A{Hdr: dns.RR_Header{Name: question.Name, Rrtype: dns.TypeA, Class: dns.ClassINET, Ttl: 60}, A: net.ParseIP(ip)})
			}
			_ = w.WriteMsg(answer)
		})}
		go func() { _ = server.ActivateAndServe() }()
		t.Cleanup(func() { _ = server.Shutdown() })
		return packet.LocalAddr().(*net.UDPAddr).Port
	}
	publicPort, officePort := newDNS(false), newDNS(true)
	newTarget := func(label string) *httptest.Server {
		s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { _, _ = io.WriteString(w, label) }))
		t.Cleanup(s.Close)
		return s
	}
	direct, tunnel := newTarget("direct"), newTarget("proxy")
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := listener.Addr().(*net.TCPAddr).Port
	_ = listener.Close()
	config := map[string]interface{}{
		"log": map[string]interface{}{"disabled": true},
		"dns": map[string]interface{}{"strategy": "prefer_ipv4", "final": "public", "servers": []interface{}{
			map[string]interface{}{"type": "udp", "tag": "public", "server": "127.0.0.1", "server_port": publicPort},
			map[string]interface{}{"type": "udp", "tag": "office", "server": "127.0.0.1", "server_port": officePort}},
			"rules": []interface{}{map[string]interface{}{"domain_suffix": "office.test", "server": "office"}}},
		"inbounds":  []interface{}{map[string]interface{}{"type": "mixed", "listen": "127.0.0.1", "listen_port": port}},
		"outbounds": []interface{}{map[string]interface{}{"type": "direct", "tag": "direct"}, map[string]interface{}{"type": "direct", "tag": "proxy"}},
		"route": map[string]interface{}{"final": "proxy", "rules": []interface{}{
			map[string]interface{}{"domain": "force-vpn.test", "outbound": "proxy"},
			map[string]interface{}{"domain": "force-block.test", "action": "reject"},
			map[string]interface{}{"ip_is_private": true, "outbound": "direct"},
			map[string]interface{}{"rule_set": "geoip-ru", "outbound": "direct"},
			map[string]interface{}{"outbound": "proxy"}},
			"rule_set": []interface{}{map[string]interface{}{"type": "inline", "tag": "geoip-ru", "rules": []interface{}{map[string]interface{}{"ip_cidr": []string{"178.130.128.0/20", "2001:db8::/32"}}}}}},
	}
	// Override only the final dial target, after route matching has selected an
	// outbound. Requests never leave loopback, even when the fixture resolves
	// its test hostname to a public RU/foreign address.
	for _, rawRule := range config["route"].(map[string]interface{})["rules"].([]interface{}) {
		rule := rawRule.(map[string]interface{})
		if tag, ok := rule["outbound"].(string); ok {
			target := tunnel
			if tag == "direct" {
				target = direct
			}
			rule["override_address"] = "127.0.0.1"
			rule["override_port"] = target.Listener.Addr().(*net.TCPAddr).Port
		}
	}
	raw, _ := json.Marshal(config)
	instance, cancel, err := nekoCreate(raw)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { cancel(); _ = instance.Close() })
	proxyURL, _ := url.Parse(fmt.Sprintf("http://127.0.0.1:%d", port))
	for _, mode := range []string{"HTTP", "SOCKS"} {
		t.Run(mode, func(t *testing.T) {
			transport := &http.Transport{Proxy: http.ProxyURL(proxyURL), DisableKeepAlives: true}
			if mode == "SOCKS" {
				dialer, err := proxy.SOCKS5("tcp", proxyURL.Host, nil, &net.Dialer{Timeout: 3 * time.Second})
				if err != nil {
					t.Fatal(err)
				}
				transport.Proxy = nil
				transport.DialContext = func(ctx context.Context, network, address string) (net.Conn, error) {
					return dialer.(proxy.ContextDialer).DialContext(ctx, network, address)
				}
			}
			t.Cleanup(transport.CloseIdleConnections)
			client := &http.Client{Transport: transport, Timeout: 5 * time.Second}
			for _, tc := range []struct{ host, want string }{{"force-vpn.test", "proxy"}, {"ru-site.test", "direct"}, {"another-ru.test", "direct"}, {"ipv6-ru.test", "direct"}, {"178.130.128.26", "direct"}, {"foreign.test", "proxy"}, {"gitlab.office.test", "direct"}} {
				t.Run(tc.host, func(t *testing.T) {
					before := publicQueries.Load() + officeQueries.Load()
					response, err := client.Get("http://" + tc.host + "/")
					if err != nil {
						t.Fatal(err)
					}
					body, err := io.ReadAll(response.Body)
					_ = response.Body.Close()
					if err != nil || string(body) != tc.want {
						t.Fatalf("%s: got %q, err=%v; want %s", tc.host, body, err, tc.want)
					}
					if (tc.host == "force-vpn.test" || tc.host == "178.130.128.26") && before != publicQueries.Load()+officeQueries.Load() {
						t.Fatal("explicit VPN/IP-literal request unexpectedly forced DNS")
					}
				})
			}
			response, err := client.Get("http://force-block.test/")
			if err == nil {
				_ = response.Body.Close()
				if response.StatusCode < 400 {
					t.Fatal("explicit block was bypassed by the RU rule")
				}
			}
		})
	}
	if publicQueries.Load() == 0 || officeQueries.Load() == 0 {
		t.Fatal("public/split DNS was not exercised")
	}
}
