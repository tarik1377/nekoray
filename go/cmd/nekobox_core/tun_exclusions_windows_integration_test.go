//go:build windows

package main

import (
	"context"
	"encoding/json"
	"fmt"
	"net/netip"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"testing"
)

// No VMware/NGate installation or certificate is claimed here. The actual
// Windows destination route is what host processes and VMware NAT both use.
// Only disposable CI may install this isolated benchmark TUN.
func TestWindowsRealTunGatewayExclusion(t *testing.T) {
	if os.Getenv("GREENRHYTHM_TEST_REAL_TUN") != "1" {
		t.Skip("disposable Windows CI only")
	}
	if os.Getenv("GITHUB_ACTIONS") != "true" {
		t.Fatal("real adapter tests require a disposable CI runner")
	}
	bestInterface := func(address string) int {
		t.Helper()
		parsed := netip.MustParseAddr(address)
		out, err := exec.Command("powershell.exe", "-NoProfile", "-Command", fmt.Sprintf("Find-NetRoute -RemoteIPAddress '%s' | Select-Object -ExpandProperty InterfaceIndex -Unique", parsed)).CombinedOutput()
		value, parseErr := strconv.Atoi(strings.TrimSpace(string(out)))
		if err != nil || parseErr != nil {
			t.Fatalf("best route query: %s, %v, %v", out, err, parseErr)
		}
		return value
	}
	baseline := bestInterface("198.51.100.29")
	resolutions := 0
	for _, gatewayIP := range []string{"198.51.100.29", "198.51.100.30"} {
		func() {
			config := map[string]any{
				"log": map[string]any{"level": "error"},
				"inbounds": []any{map[string]any{"type": "tun", "tag": "test-tun", "interface_name": "gr-ci-gateway", "stack": "system", "mtu": 1500,
					"address": []string{"198.18.253.1/30"}, "auto_route": true, "route_address": []string{"198.51.100.0/24"}, "route_exclude_address": []string{"gateway-audit.invalid"}}},
				"outbounds": []any{map[string]any{"type": "direct", "tag": "bypass"}},
				"route":     map[string]any{"auto_detect_interface": true, "final": "bypass"},
			}
			raw, _ := json.Marshal(config)
			instance, cancel, err := nekoCreateWithTunLookup(raw, func(context.Context, string, string) ([]netip.Addr, error) {
				resolutions++
				return []netip.Addr{netip.MustParseAddr(gatewayIP)}, nil
			})
			if err != nil {
				t.Fatal(err)
			}
			defer func() { cancel(); _ = instance.Close() }()
			if got := bestInterface(gatewayIP); got != baseline {
				t.Errorf("excluded gateway route = %d, baseline physical route = %d", got, baseline)
			}
			other := "198.51.100.30"
			if gatewayIP == other {
				other = "198.51.100.29"
			}
			if got := bestInterface(other); got == baseline {
				t.Errorf("unexcluded destination escaped TUN: %s on %d", other, got)
			}
			t.Logf("gateway %s bypasses actual System TUN via original interface %d; neighbouring IP remains captured", gatewayIP, baseline)
		}()
		if got := bestInterface(gatewayIP); got != baseline {
			t.Fatalf("old TUN route remained after close: %d", got)
		}
	}
	if resolutions != 2 {
		t.Fatalf("DNS was not refreshed at each reconnect: %d", resolutions)
	}
}
