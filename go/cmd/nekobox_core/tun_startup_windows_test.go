package main

import (
	"context"
	"encoding/json"
	"net"
	"os"
	"testing"
	"time"
)

func TestWindowsRealTunStartupRecovery(t *testing.T) {
	if os.Getenv("GREENRHYTHM_TEST_REAL_TUN") != "1" {
		t.Skip("disposable Windows CI only")
	}
	if os.Getenv("GITHUB_ACTIONS") != "true" {
		t.Fatal("real adapter tests require a disposable CI runner")
	}
	const name = "gr-ci-startup"
	config, err := json.Marshal(map[string]any{
		"inbounds": []any{map[string]any{
			"type": "tun", "tag": "tun-in", "interface_name": name,
			"stack": "system", "mtu": 1500, "auto_route": true,
			"address": []string{"198.18.254.1/30"}, "route_address": []string{"198.18.254.0/30"},
		}},
		"outbounds": []any{map[string]any{"type": "direct", "tag": "bypass"}},
		"route":     map[string]any{"auto_detect_interface": true, "final": "bypass"},
		"dns": map[string]any{"servers": []any{
			map[string]any{"type": "local", "tag": "system"},
		}},
	})
	if err != nil {
		t.Fatal(err)
	}
	for _, scenario := range []struct {
		name        string
		failInitial bool
		wantChecks  int
	}{
		{"healthy System", false, 1},
		{"System timeout with gVisor recovery", true, 2},
	} {
		t.Run(scenario.name, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 40*time.Second)
			defer cancel()
			checks := 0
			instance, stop, err := startWithTunRecovery(ctx, config, func(ctx context.Context, target tunProbeTarget) error {
				checks++
				if scenario.failInitial && checks == 1 {
					return context.DeadlineExceeded
				}
				return probeTunTCP(ctx, target)
			})
			if err != nil {
				t.Fatal(err)
			}
			t.Cleanup(func() {
				stop()
				if err := instance.Close(); err != nil {
					t.Error(err)
				}
				if _, err := net.InterfaceByName(name); err == nil {
					t.Error("closed instance left the TUN adapter active")
				}
			})
			if checks != scenario.wantChecks {
				t.Fatalf("got %d TCP checks, expected %d", checks, scenario.wantChecks)
			}
		})
	}
}
