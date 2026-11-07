package main

import (
	"context"
	"encoding/json"
	"io"
	"net"
	"strings"
	"testing"
	"time"

	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/adapter/outbound"
	"github.com/sagernet/sing-box/log"
	M "github.com/sagernet/sing/common/metadata"
	"nekobox_core/robloxauto"
)

type ownedRobloxDetour struct {
	outbound.Adapter
	tcp, udp string
}

func (s *ownedRobloxDetour) DialContext(ctx context.Context, network string, _ M.Socksaddr) (net.Conn, error) {
	var d net.Dialer
	target := s.tcp
	if strings.HasPrefix(network, "udp") {
		target = s.udp
	}
	return d.DialContext(ctx, network, target)
}
func (s *ownedRobloxDetour) ListenPacket(_ context.Context, destination M.Socksaddr) (net.PacketConn, error) {
	conn, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		return nil, err
	}
	target, err := net.ResolveUDPAddr("udp4", s.udp)
	if err != nil {
		conn.Close()
		return nil, err
	}
	return &ownedRobloxPacket{PacketConn: conn, target: target, origin: destination.UDPAddr()}, nil
}

type ownedRobloxPacket struct {
	net.PacketConn
	target, origin net.Addr
}

func (p *ownedRobloxPacket) WriteTo(b []byte, _ net.Addr) (int, error) {
	return p.PacketConn.WriteTo(b, p.target)
}
func (p *ownedRobloxPacket) ReadFrom(b []byte) (int, net.Addr, error) {
	n, _, err := p.PacketConn.ReadFrom(b)
	return n, p.origin, err
}

func ownedRobloxEcho(t *testing.T, label string) *ownedRobloxDetour {
	t.Helper()
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { listener.Close() })
	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			go func() {
				defer conn.Close()
				_ = conn.SetDeadline(time.Now().Add(3 * time.Second))
				b := make([]byte, 1)
				for {
					if _, err := io.ReadFull(conn, b); err != nil {
						return
					}
					if _, err := conn.Write([]byte(label)); err != nil {
						return
					}
				}
			}()
		}
	}()
	udp, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { udp.Close() })
	go func() {
		b := make([]byte, 128)
		for {
			_, peer, err := udp.ReadFrom(b)
			if err != nil {
				return
			}
			_, _ = udp.WriteTo([]byte(label), peer)
		}
	}()
	return &ownedRobloxDetour{Adapter: outbound.NewAdapter("owned", label, []string{"tcp", "udp"}, nil), tcp: listener.Addr().String(), udp: udp.LocalAddr().String()}
}

func TestRobloxAutoRegisteredWithoutChangingRoutes(t *testing.T) {
	config := map[string]any{"log": map[string]any{"disabled": true}, "outbounds": []any{
		map[string]any{"type": "direct", "tag": "bypass"}, map[string]any{"type": "direct", "tag": "proxy"},
		map[string]any{"type": robloxAutoType, "tag": "roblox-auto", "direct": "bypass", "proxy": "proxy", "cache_scope": strings.Repeat("a", 64), "cache": false},
	}, "route": map[string]any{"final": "proxy", "rules": []any{map[string]any{"process_name": []string{"RobloxPlayerBeta.exe", "RobloxStudioBeta.exe"}, "outbound": "roblox-auto"}}}}
	raw, _ := json.Marshal(config)
	box, cancel, err := nekoCreate(raw)
	if err != nil {
		t.Fatal(err)
	}
	defer cancel()
	defer box.Close()
	item, ok := box.Outbound().Outbound("roblox-auto")
	if !ok {
		t.Fatal("not registered")
	}
	auto, ok := item.(*robloxAutoOutbound)
	if !ok || auto.store != nil {
		t.Fatal("owned fixture attempted persistent cache")
	}
	if box.Outbound().Default().Tag() != "proxy" {
		t.Fatal("default outbound changed")
	}
	if auto.direct.Tag() != "bypass" || auto.proxy.Tag() != "proxy" {
		t.Fatal("detours changed")
	}
	if _, err = newRobloxAuto(context.Background(), nil, nil, "self", robloxAutoOptions{Direct: "self", Proxy: "proxy", Scope: strings.Repeat("a", 64)}); err == nil {
		t.Fatal("recursive outbound accepted")
	}
}
func TestRobloxAutoTCPUDPAndLiveFlowPinning(t *testing.T) {
	direct, proxy := ownedRobloxEcho(t, "D"), ownedRobloxEcho(t, "P")
	logger, err := log.New(log.Options{})
	if err != nil {
		t.Fatal(err)
	}
	engine := robloxauto.New(context.Background())
	t.Cleanup(engine.Close)
	auto := &robloxAutoOutbound{direct: direct, proxy: proxy, engine: engine, logger: logger.Logger()}
	auto.refreshNetwork()
	network, _ := engine.Snapshot()
	engine.Restore(network, map[string]robloxauto.Entry{"tcp:users.roblox.com": {Path: robloxauto.Direct, Verified: true, Expires: time.Now().Add(time.Minute)}})
	destination := M.ParseSocksaddr("users.roblox.com:443")
	old, err := auto.DialContext(context.Background(), "tcp", destination)
	if err != nil {
		t.Fatal(err)
	}
	defer old.Close()
	expect := func(conn net.Conn, label byte) {
		t.Helper()
		_ = conn.SetDeadline(time.Now().Add(time.Second))
		if _, e := conn.Write([]byte{1}); e != nil {
			t.Fatal(e)
		}
		b := make([]byte, 1)
		if _, e := io.ReadFull(conn, b); e != nil || b[0] != label {
			t.Fatalf("got %q, %v; want %c", b, e, label)
		}
	}
	expect(old, 'D')
	engine.Restore(network, map[string]robloxauto.Entry{"tcp:users.roblox.com": {Path: robloxauto.Proxy, Verified: true, Expires: time.Now().Add(time.Minute)}})
	newConn, err := auto.DialContext(context.Background(), "tcp", destination)
	if err != nil {
		t.Fatal(err)
	}
	defer newConn.Close()
	expect(newConn, 'P')
	expect(old, 'D') // Existing flow is still open on DIRECT.
	pinned, err := auto.SelectConnection(context.Background(), adapter.InboundContext{Network: "tcp", Destination: destination})
	if err != nil || pinned.Tag() != proxy.Tag() {
		t.Fatal("accounting did not receive the actual selected detour", err)
	}
	engine.Restore(network, map[string]robloxauto.Entry{"tcp:users.roblox.com": {Path: robloxauto.Direct, Verified: true, Expires: time.Now().Add(time.Minute)}})
	stillProxy, err := pinned.DialContext(context.Background(), "tcp", destination)
	if err != nil {
		t.Fatal(err)
	}
	defer stillProxy.Close()
	expect(stillProxy, 'P') // A choice is pinned even between tracking and dialing.
	udpDestination := M.ParseSocksaddr("128.116.1.2:50000")
	packet, err := auto.ListenPacket(context.Background(), udpDestination)
	if err != nil {
		t.Fatal(err)
	}
	defer packet.Close()
	udpExpect := func(packet net.PacketConn, label byte) {
		t.Helper()
		_ = packet.SetDeadline(time.Now().Add(time.Second))
		if _, e := packet.WriteTo([]byte{1}, udpDestination.UDPAddr()); e != nil {
			t.Fatal(e)
		}
		b := make([]byte, 16)
		n, _, e := packet.ReadFrom(b)
		if e != nil || n != 1 || b[0] != label {
			t.Fatalf("got %q, %v; want %c", b[:n], e, label)
		}
	}
	udpExpect(packet, 'D')
	// Simulate two separate no-reply sessions, not a fabricated STUN success.
	engine.ObserveUDP(udpDestination.String(), network, robloxauto.Direct, false)
	engine.ObserveUDP(udpDestination.String(), network, robloxauto.Direct, false)
	other, err := auto.ListenPacket(context.Background(), udpDestination)
	if err != nil {
		t.Fatal(err)
	}
	defer other.Close()
	udpExpect(other, 'P')
	udpExpect(packet, 'D')
	_, snapshot := engine.Snapshot()
	if snapshot["udp:"+udpDestination.String()].Path != robloxauto.Proxy {
		t.Fatal("confirmed VPN UDP was not cached")
	}
	// Local Studio traffic never becomes a remote availability probe.
	if auto.chooseTCP(context.Background(), M.ParseSocksaddr("127.0.0.1:443")) != robloxauto.Direct {
		t.Fatal("local Studio changed route")
	}
}

func TestRobloxAutoFlowSelectionPrecedesAccounting(t *testing.T) {
	echo := ownedRobloxEcho(t, "D")
	config := map[string]any{"log": map[string]any{"disabled": true}, "outbounds": []any{
		map[string]any{"type": "direct", "tag": "bypass"}, map[string]any{"type": "direct", "tag": "proxy"},
		map[string]any{"type": robloxAutoType, "tag": "roblox-auto", "direct": "bypass", "proxy": "proxy", "cache_scope": strings.Repeat("a", 64)},
	}, "experimental": map[string]any{"clash_api": map[string]any{"external_controller": "127.0.0.1:0"}},
		"route": map[string]any{"final": "proxy", "rules": []any{map[string]any{"process_name": []string{"RobloxPlayerBeta.exe"}, "outbound": "roblox-auto"}}}}
	raw, _ := json.Marshal(config)
	box, cancel, err := nekoCreate(raw)
	if err != nil {
		t.Fatal(err)
	}
	defer cancel()
	defer box.Close()
	if trafficManager.Load() == nil {
		t.Skip("Clash accounting build tag is required")
	}
	metadata := adapter.InboundContext{Network: "tcp", Destination: M.ParseSocksaddr(echo.tcp), ProcessInfo: &adapter.ConnectionOwner{ProcessPath: "C:/owned/RobloxPlayerBeta.exe"}}
	client, server := net.Pipe()
	defer client.Close()
	_ = client.SetDeadline(time.Now().Add(time.Second))
	done := make(chan error, 1)
	go func() { done <- box.Router().RouteConnection(context.Background(), server, metadata) }()
	_, _ = client.Write([]byte{1})
	body := make([]byte, 1)
	if _, err = io.ReadFull(client, body); err != nil || body[0] != 'D' {
		t.Fatal("owned route failed", err)
	}
	client.Close()
	select {
	case err = <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(time.Second):
		t.Fatal("TCP router did not return")
	}
	waitForTag := func(network string) {
		t.Helper()
		deadline := time.Now().Add(time.Second)
		for time.Now().Before(deadline) {
			for _, item := range closedConnections(time.Now(), time.Minute, 20) {
				if item.Process == "RobloxPlayerBeta.exe" && item.Network == network {
					if item.Tag != "bypass" {
						t.Fatal("table reports guessed auto instead of pinned detour", item.Tag)
					}
					return
				}
			}
			time.Sleep(5 * time.Millisecond)
		}
		t.Fatal("missing owned connection accounting", network)
	}
	waitForTag("tcp")
	metadata.Network = "udp"
	metadata.Destination = M.ParseSocksaddr(echo.udp)
	inbound, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer inbound.Close()
	udp, err := net.DialUDP("udp4", nil, inbound.LocalAddr().(*net.UDPAddr))
	if err != nil {
		t.Fatal(err)
	}
	defer udp.Close()
	_ = udp.SetDeadline(time.Now().Add(time.Second))
	packetDone := make(chan error, 1)
	box.Router().RoutePacketConnectionEx(context.Background(), &gameVoicePacketConn{conn: inbound, destination: metadata.Destination, peer: udp.LocalAddr().(*net.UDPAddr)}, metadata, func(err error) { packetDone <- err })
	_, err = udp.Write([]byte{1})
	if err != nil {
		t.Fatal(err)
	}
	body = make([]byte, 1)
	if _, err = udp.Read(body); err != nil || body[0] != 'D' {
		t.Fatal("owned UDP failed", err)
	}
	inbound.Close()
	select {
	case <-packetDone:
	case <-time.After(time.Second):
		t.Fatal("owned UDP router did not stop")
	}
	waitForTag("udp")
}
