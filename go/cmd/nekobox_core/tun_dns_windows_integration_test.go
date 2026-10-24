//go:build windows

package main

import (
	"context"
	"encoding/json"
	"errors"
	"net"
	"os"
	"os/exec"
	"strings"
	"testing"
	"time"

	"github.com/miekg/dns"
	"golang.org/x/sys/windows"
)

// Deliberately unavailable in ordinary local runs, including go test ./....
// Only a disposable Actions Windows runner may create this test adapter. The
// test routes only its benchmark /30; no user's PC or profile is involved.
func TestWindowsRealTunDNSLoopGuard(t *testing.T) {
	if os.Getenv("GREENRHYTHM_TEST_REAL_TUN") != "1" {
		t.Skip("disposable Windows CI only")
	}
	if os.Getenv("GITHUB_ACTIONS") != "true" {
		t.Fatal("real adapter tests require a disposable CI runner")
	}
	if exe, err := os.Executable(); err == nil {
		t.Logf("test program: %s; argv0: %s", exe, os.Args[0])
	}
	// Windows may retry a refused connect for a few seconds, even on loopback
	// (see net/dial_test.go in Go). Check the native refusal baseline and require
	// WSAECONNREFUSED below; a one-second deadline mistakes that retry for a drop.
	closed, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	closedAddress := closed.Addr().String()
	_ = closed.Close()
	started := time.Now()
	baselineConn, baselineErr := net.DialTimeout("tcp4", closedAddress, 5*time.Second)
	if baselineConn != nil {
		_ = baselineConn.Close()
	}
	if !errors.Is(baselineErr, windows.WSAECONNREFUSED) {
		t.Fatalf("native closed-port refusal failed: %v", baselineErr)
	}
	t.Logf("native Windows refusal baseline: %s", time.Since(started))
	packet, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	server := &dns.Server{PacketConn: packet, Handler: dns.HandlerFunc(func(w dns.ResponseWriter, q *dns.Msg) {
		a := new(dns.Msg).SetReply(q)
		for _, question := range q.Question {
			if question.Qtype == dns.TypeA {
				a.Answer = append(a.Answer, &dns.A{Hdr: dns.RR_Header{Name: question.Name, Rrtype: dns.TypeA, Class: dns.ClassINET, Ttl: 60}, A: net.ParseIP("198.51.100.7")})
			}
		}
		_ = w.WriteMsg(a)
	})}
	go func() { _ = server.ActivateAndServe() }()
	defer server.Shutdown()
	config := map[string]any{
		"log": map[string]any{"level": "trace"},
		"inbounds": []any{map[string]any{"type": "tun", "tag": "test-tun", "interface_name": "gr-ci-dns", "stack": "system", "mtu": 1500,
			"address": []string{"198.18.253.1/30"}, "auto_route": true, "route_address": []string{"198.18.253.0/30"}}},
		"outbounds": []any{map[string]any{"type": "direct", "tag": "bypass"}},
		"route":     map[string]any{"auto_detect_interface": true, "final": "bypass", "rules": []any{map[string]any{"outbound": "bypass"}}},
		"dns": map[string]any{"final": "system", "servers": []any{
			map[string]any{"type": "udp", "tag": "fixture", "server": "127.0.0.1", "server_port": packet.LocalAddr().(*net.UDPAddr).Port},
			map[string]any{"type": "local", "tag": "system"}},
			"rules": []any{map[string]any{"domain_suffix": "guard-fixture.invalid", "server": "fixture"}}},
	}
	raw, _ := json.Marshal(config)
	instance, cancel, err := nekoCreate(raw)
	if err != nil {
		t.Fatal(err)
	}
	defer func() { cancel(); _ = instance.Close() }()
	defer func() {
		if !t.Failed() {
			return
		}
		ctx, stop := context.WithTimeout(context.Background(), 15*time.Second)
		defer stop()
		output, err := exec.CommandContext(ctx, "powershell.exe", "-NoProfile", "-Command",
			"Get-NetTCPConnection | Where-Object { $_.LocalAddress -like '198.18.253.*' -or $_.RemoteAddress -like '198.18.253.*' } | Format-Table -AutoSize | Out-String -Width 160; Get-NetRoute -InterfaceAlias gr-ci-dns | Format-Table -AutoSize | Out-String -Width 160; Get-NetIPInterface -InterfaceAlias gr-ci-dns | Format-List; Get-NetFirewallRule -DisplayName 'sing-tun (*)' | Get-NetFirewallApplicationFilter | Format-List").CombinedOutput()
		t.Logf("runner network state: %s (%v)", output, err)
	}()
	// Restart capture after the NIC exists so Packet Monitor can see the newly
	// registered virtual adapter. This is still restricted to disposable CI.
	_ = exec.Command("pktmon", "stop").Run()
	if output, err := exec.Command("pktmon", "start", "--capture", "--trace", "--provider", "Microsoft-Windows-TCPIP", "--pkt-size", "128", "--file-size", "16", "--file-name", "tun-packets.etl").CombinedOutput(); err != nil {
		t.Logf("packet trace unavailable: %s %v", output, err)
	}
	q := new(dns.Msg).SetQuestion("real.guard-fixture.invalid.", dns.TypeA)
	for _, network := range []string{"udp", "tcp"} {
		a, _, err := (&dns.Client{Net: network, Timeout: 3 * time.Second}).Exchange(q, "198.18.253.2:53")
		if err != nil || len(a.Answer) != 1 || a.Answer[0].(*dns.A).A.String() != "198.51.100.7" {
			t.Errorf("real %s TUN DNS failed: %v %v", network, a, err)
		}
	}
	// A burst above the default 50-reject flood threshold, with real Windows
	// SYN/RST exchanges through Wintun/System. Require an actual refusal rather
	// than accepting a timeout, cancellation or local resource exhaustion.
	type refusalResult struct {
		attempt int
		elapsed time.Duration
		err     error
	}
	results := make(chan refusalResult, 64)
	for attempt := 0; attempt < 64; attempt++ {
		go func(attempt int) {
			started := time.Now()
			conn, err := net.DialTimeout("tcp4", "198.18.253.2:853", 5*time.Second)
			if conn != nil {
				_ = conn.Close()
			}
			results <- refusalResult{attempt, time.Since(started), err}
		}(attempt)
	}
	var slowest time.Duration
	for range 64 {
		result := <-results
		if !errors.Is(result.err, windows.WSAECONNREFUSED) {
			t.Errorf("self-DNS TLS attempt %d: expected connection refused, got %v after %s", result.attempt, result.err, result.elapsed)
		}
		if result.elapsed > slowest {
			slowest = result.elapsed
		}
	}
	t.Logf("64 concurrent self-DNS TLS refusal attempts, slowest: %s", slowest)
	// SYNs rejected by the guard must not leave a chain of established or
	// TIME_WAIT connections to the synthetic resolver (the original symptom).
	output, err := exec.Command("powershell.exe", "-NoProfile", "-Command",
		"@(Get-NetTCPConnection -RemoteAddress 198.18.253.2 -RemotePort 853 -ErrorAction SilentlyContinue | Where-Object { $_.State -in 'Established','TimeWait','FinWait1','FinWait2','CloseWait' }).Count").CombinedOutput()
	if err != nil || strings.TrimSpace(string(output)) != "0" {
		t.Fatalf("self-DNS connections remain: %s %v", output, err)
	}
	t.Log("real System TUN: UDP/TCP DNS passed, 64 TLS attempts refused, no connection chain")
}
