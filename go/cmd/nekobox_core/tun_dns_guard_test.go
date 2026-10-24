package main

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net"
	"reflect"
	"testing"
	"time"

	"github.com/miekg/dns"
	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/include"
	"github.com/sagernet/sing-box/option"
	R "github.com/sagernet/sing-box/route/rule"
	tun "github.com/sagernet/sing-tun"
	J "github.com/sagernet/sing/common/json"
	M "github.com/sagernet/sing/common/metadata"
)

func tunDNSGuardOptions(t *testing.T, ctx context.Context, addresses []string) option.Options {
	t.Helper()
	config := map[string]any{
		"log":       map[string]any{"disabled": true},
		"inbounds":  []any{map[string]any{"type": "tun", "tag": "owned-tun", "auto_route": true, "address": addresses}},
		"outbounds": []any{map[string]any{"type": "direct", "tag": "bypass"}},
		"route":     map[string]any{"final": "bypass", "rules": []any{map[string]any{"outbound": "bypass"}}},
	}
	raw, _ := json.Marshal(config)
	options, err := J.UnmarshalExtendedContext[option.Options](ctx, raw)
	if err != nil {
		t.Fatal(err)
	}
	return options
}

func TestTunDNSGuardPreservesUserPolicy(t *testing.T) {
	ctx := include.Context(context.Background())
	options := tunDNSGuardOptions(t, ctx, []string{"172.19.0.1/28", "172.20.0.1/24", "fdfe:dcba:9876::1/126", "fd00:1::1/64"})
	options.DNS = new(option.DNSOptions)
	options.DNS.Final = "existing-corporate-resolver"
	before, _ := json.Marshal(options)
	configureTunDNSGuard(&options)
	if len(options.Route.Rules) != 3 {
		t.Fatal("missing DNS guards")
	}
	guard := options.Route.Rules[0].DefaultOptions
	if !reflect.DeepEqual([]string(guard.IPCIDR), []string{"172.19.0.2/32", "fdfe:dcba:9876::2/128"}) {
		t.Fatalf("must reserve only first Windows DNS address per family: %v", guard.IPCIDR)
	}
	options.Route.Rules = options.Route.Rules[2:]
	after, _ := json.Marshal(options)
	if string(before) != string(after) {
		t.Fatal("user routes, DNS, inbounds or outbounds were modified")
	}
	for _, auto := range []bool{false, true} {
		empty := option.Options{}
		if auto {
			empty = tunDNSGuardOptions(t, ctx, []string{"172.19.0.1/28"})
			empty.Inbounds[0].Options.(*option.TunInboundOptions).AutoRoute = false
		}
		before, _ = json.Marshal(empty)
		configureTunDNSGuard(&empty)
		after, _ = json.Marshal(empty)
		if string(before) != string(after) {
			t.Fatal("non-managed/non-routing configuration changed")
		}
	}
	custom := tunDNSGuardOptions(t, ctx, []string{"10.9.8.1/24", "fd00:9::5/64"})
	configureTunDNSGuard(&custom)
	if !reflect.DeepEqual([]string(custom.Route.Rules[0].DefaultOptions.IPCIDR), []string{"10.9.8.2/32", "fd00:9::6/128"}) {
		t.Fatal("guard used hardcoded addresses instead of the configured adapter DNS")
	}
}

// Start only the real router/DNS and loopback fixtures. The TUN definition is
// consumed by the runtime guard then removed BEFORE box.New/Start: no adapter,
// firewall exception, OS route, system DNS change or installed client is used.
func tunDNSGuardRouter(t *testing.T, guarded bool, dnsPort uint16) adapter.Router {
	t.Helper()
	ctx := include.Context(context.Background())
	options := tunDNSGuardOptions(t, ctx, []string{"172.19.0.1/28", "fdfe:dcba:9876::1/126"})
	if dnsPort != 0 {
		config, _ := json.Marshal(map[string]any{"servers": []any{map[string]any{"type": "udp", "tag": "fixture", "server": "127.0.0.1", "server_port": dnsPort}}, "final": "fixture"})
		value, err := J.UnmarshalExtendedContext[option.DNSOptions](ctx, config)
		if err != nil {
			t.Fatal(err)
		}
		options.DNS = &value
	}
	if guarded {
		configureTunDNSGuard(&options)
	}
	options.Inbounds = nil
	instance, err := box.New(box.Options{Context: ctx, Options: options})
	if err != nil {
		t.Fatal(err)
	}
	if err = instance.Start(); err != nil {
		_ = instance.Close()
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = instance.Close() })
	return instance.Router()
}

func TestTunDNSGuardStopsRepeatedConnectionsBeforeDial(t *testing.T) {
	for _, guarded := range []bool{false, true} {
		router := tunDNSGuardRouter(t, guarded, 0)
		for _, network := range []string{"tcp", "udp"} {
			for _, address := range []string{"172.19.0.2:853", "172.19.0.2:443", "[fdfe:dcba:9876::2]:853"} {
				// Well above sing-box's ordinary reject/drop threshold; every SYN
				// must be refused before the System stack creates a TCP session.
				for attempt := 0; attempt < 256; attempt++ {
					_, err := router.PreMatch(adapter.InboundContext{Inbound: "owned-tun", InboundType: C.TypeTun, Network: network,
						Source: M.ParseSocksaddr("172.19.0.1:60148"), Destination: M.ParseSocksaddr(address)}, nil, 0, false)
					if guarded != R.IsRejected(err) {
						t.Fatalf("guard=%v %s %s: %v", guarded, network, address, err)
					}
					if guarded && errors.Is(err, tun.ErrDrop) {
						t.Fatal("retry burst degraded into silent drops")
					}
				}
			}
			for _, address := range []string{"172.19.0.2:53", "[fdfe:dcba:9876::2]:53", "1.1.1.1:853", "10.77.56.11:53", "10.20.22.109:443", "192.168.1.1:853", "172.20.0.2:853"} {
				_, err := router.PreMatch(adapter.InboundContext{Inbound: "owned-tun", InboundType: C.TypeTun, Network: network, Destination: M.ParseSocksaddr(address)}, nil, 0, false)
				if R.IsRejected(err) {
					t.Fatalf("legitimate DNS/corporate/external traffic blocked: %s %s %v", network, address, err)
				}
			}
		}
	}
}

func TestTunDNSGuardPlaintextDNSStillWorks(t *testing.T) {
	packet, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	server := &dns.Server{PacketConn: packet, Handler: dns.HandlerFunc(func(w dns.ResponseWriter, q *dns.Msg) {
		a := new(dns.Msg).SetReply(q)
		a.Answer = []dns.RR{&dns.A{Hdr: dns.RR_Header{Name: q.Question[0].Name, Rrtype: dns.TypeA, Class: dns.ClassINET, Ttl: 60}, A: net.ParseIP("198.51.100.7")}}
		_ = w.WriteMsg(a)
	})}
	go func() { _ = server.ActivateAndServe() }()
	t.Cleanup(func() { _ = server.Shutdown() })
	router := tunDNSGuardRouter(t, true, uint16(packet.LocalAddr().(*net.UDPAddr).Port))
	for _, destination := range []string{"172.19.0.2:53", "[fdfe:dcba:9876::2]:53"} {
		t.Run(destination, func(t *testing.T) {
			client, connection := net.Pipe()
			defer client.Close()
			defer connection.Close()
			_ = client.SetDeadline(time.Now().Add(3 * time.Second))
			metadata := adapter.InboundContext{Inbound: "owned-tun", InboundType: C.TypeTun, Network: "tcp", Destination: M.ParseSocksaddr(destination)}
			go router.RouteConnectionEx(context.Background(), connection, metadata, nil)
			wire := &dns.Conn{Conn: client}
			q := new(dns.Msg).SetQuestion("guard-fixture.test.", dns.TypeA)
			if err := wire.WriteMsg(q); err != nil {
				t.Fatal(err)
			}
			a, err := wire.ReadMsg()
			if err != nil {
				t.Fatal(err)
			}
			if len(a.Answer) != 1 || a.Answer[0].(*dns.A).A.String() != "198.51.100.7" {
				t.Fatalf("DNS reply changed: %v", a)
			}
		})
		t.Run("udp/"+destination, func(t *testing.T) {
			inbound, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
			if err != nil {
				t.Fatal(err)
			}
			defer inbound.Close()
			client, err := net.DialUDP("udp4", nil, inbound.LocalAddr().(*net.UDPAddr))
			if err != nil {
				t.Fatal(err)
			}
			defer client.Close()
			_ = client.SetDeadline(time.Now().Add(3 * time.Second))
			metadata := adapter.InboundContext{Inbound: "owned-tun", InboundType: C.TypeTun, Network: "udp", Source: M.SocksaddrFromNet(client.LocalAddr()), Destination: M.ParseSocksaddr(destination)}
			go router.RoutePacketConnectionEx(context.Background(), &gameVoicePacketConn{conn: inbound, destination: metadata.Destination, peer: client.LocalAddr().(*net.UDPAddr)}, metadata, nil)
			q := new(dns.Msg).SetQuestion("udp-guard-fixture.test.", dns.TypeA)
			wire, err := q.Pack()
			if err != nil {
				t.Fatal(err)
			}
			if _, err = client.Write(wire); err != nil {
				t.Fatal(err)
			}
			buffer := make([]byte, 2048)
			n, err := client.Read(buffer)
			if err != nil {
				t.Fatal(err)
			}
			a := new(dns.Msg)
			if err = a.Unpack(buffer[:n]); err != nil {
				t.Fatal(err)
			}
			if a.Id != q.Id || len(a.Answer) != 1 || a.Answer[0].(*dns.A).A.String() != "198.51.100.7" {
				t.Fatalf("UDP DNS reply changed: %v", a)
			}
		})
	}
}

func TestTunDNSGuardTCPServiceRejectedWithoutConnection(t *testing.T) {
	router := tunDNSGuardRouter(t, true, 0)
	client, connection := net.Pipe()
	defer client.Close()
	_ = client.SetDeadline(time.Now().Add(time.Second))
	go router.RouteConnectionEx(context.Background(), connection, adapter.InboundContext{Inbound: "owned-tun", InboundType: C.TypeTun, Network: "tcp", Destination: M.ParseSocksaddr("172.19.0.2:853")}, nil)
	_, err := client.Read(make([]byte, 1))
	if err != io.EOF {
		t.Fatalf("self-DNS TLS must be closed promptly, got %v", err)
	}
}
