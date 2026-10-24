package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"net/netip"
	"strings"
	"testing"
	"time"
)

func exclusionFixture(entries any) []byte {
	raw, _ := json.Marshal(map[string]any{
		"inbounds":       []any{map[string]any{"type": "tun", "tag": "test", "address": []string{"172.19.0.1/28"}, "route_exclude_address": entries}},
		"outbounds":      []any{map[string]any{"type": "vless", "uuid": "fixture-secret-do-not-rewrite"}},
		"route":          map[string]any{"final": "proxy", "rules": []any{map[string]any{"process_name": "Discord.exe", "outbound": "proxy"}}},
		"future_unknown": json.Number("9007199254740993"),
	})
	return raw
}
func fixtureExclusions(t *testing.T, raw []byte) []string {
	t.Helper()
	var root map[string]json.RawMessage
	_ = json.Unmarshal(raw, &root)
	var inbounds []map[string]json.RawMessage
	_ = json.Unmarshal(root["inbounds"], &inbounds)
	list, err := exclusionStrings(inbounds[0]["route_exclude_address"])
	if err != nil {
		t.Fatal(err)
	}
	return list
}
func TestTunExclusionsRetainLegacyBytesAndDoNotQueryDNS(t *testing.T) {
	for _, entry := range []any{[]string{"10.0.0.0/8", "212.164.137.29/32", "fd00::/8"}, "212.164.137.29/32"} {
		raw := exclusionFixture(entry)
		result, err := prepareTunRouteExclusions(context.Background(), raw, func(context.Context, string, string) ([]netip.Addr, error) {
			t.Fatal("CIDR-only config requested DNS")
			return nil, nil
		})
		if err != nil || !bytes.Equal(raw, result) {
			t.Fatalf("legacy config changed: %v", err)
		}
	}
}
func TestTunExclusionsResolvePerStartAndPreserveOtherSettings(t *testing.T) {
	raw := exclusionFixture([]string{"10.0.0.0/8", "MKS-RACCEESS.RTK-IT.RU.", "mks-racceess.rtk-it.ru", "212.164.137.29/32"})
	calls := 0
	lookup := func(ctx context.Context, network, host string) ([]netip.Addr, error) {
		calls++
		if network != "ip" || host != "mks-racceess.rtk-it.ru" {
			t.Fatal(network, host)
		}
		if _, ok := ctx.Deadline(); !ok {
			t.Fatal("unbounded system DNS lookup")
		}
		if calls == 1 {
			return []netip.Addr{netip.MustParseAddr("212.164.137.29"), netip.MustParseAddr("2001:db8::29"), netip.MustParseAddr("212.164.137.29")}, nil
		}
		return []netip.Addr{netip.MustParseAddr("203.0.113.30")}, nil
	}
	first, err := prepareTunRouteExclusions(context.Background(), raw, lookup)
	if err != nil || calls != 1 {
		t.Fatalf("first DNS: calls=%d, %v", calls, err)
	}
	if got := strings.Join(fixtureExclusions(t, first), ","); got != "10.0.0.0/8,212.164.137.29/32,2001:db8::29/128" {
		t.Fatal(got)
	}
	second, err := prepareTunRouteExclusions(context.Background(), raw, lookup)
	if err != nil || calls != 2 {
		t.Fatalf("reconnect DNS: calls=%d, %v", calls, err)
	}
	if got := strings.Join(fixtureExclusions(t, second), ","); got != "10.0.0.0/8,203.0.113.30/32,212.164.137.29/32" {
		t.Fatal("changed DNS or explicit old IP lost:", got)
	}
	var before, after map[string]json.RawMessage
	_ = json.Unmarshal(raw, &before)
	_ = json.Unmarshal(first, &after)
	for _, key := range []string{"route", "outbounds", "future_unknown"} {
		if !bytes.Equal(before[key], after[key]) {
			t.Fatalf("unrelated %s modified", key)
		}
	}
}
func TestTunExclusionsDNSErrorsDoNotProducePartialConfig(t *testing.T) {
	for _, scenario := range []string{"nxdomain", "no-addresses", "synthetic", "own-tun", "loopback", "multicast", "linklocal"} {
		t.Run(scenario, func(t *testing.T) {
			raw := exclusionFixture([]string{"10.0.0.0/8", "gateway.invalid"})
			before := bytes.Clone(raw)
			result, err := prepareTunRouteExclusions(context.Background(), raw, func(context.Context, string, string) ([]netip.Addr, error) {
				switch scenario {
				case "nxdomain":
					return nil, errors.New("fixture NXDOMAIN")
				case "no-addresses":
					return nil, nil
				default:
					address := map[string]string{"synthetic": "198.18.0.7", "own-tun": "172.19.0.2", "loopback": "127.0.0.1", "multicast": "224.0.0.1", "linklocal": "fe80::1"}[scenario]
					return []netip.Addr{netip.MustParseAddr(address)}, nil
				}
			})
			if err == nil || result != nil || !bytes.Equal(before, raw) {
				t.Fatalf("unsafe partial exclusion accepted: %s %v", result, err)
			}
		})
	}
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Millisecond)
	defer cancel()
	started := time.Now()
	_, err := prepareTunRouteExclusions(ctx, exclusionFixture("gateway.invalid"), func(ctx context.Context, _, _ string) ([]netip.Addr, error) { <-ctx.Done(); return nil, ctx.Err() })
	if !errors.Is(err, context.DeadlineExceeded) || time.Since(started) > time.Second {
		t.Fatal("DNS timeout was not propagated promptly", err)
	}
}
func TestTunExclusionsParseHostIPAndRejectInvalidInputs(t *testing.T) {
	for _, input := range []string{"*.example.ru", "https://user:secret@example.ru", "bad host", "10.0.0.1/99", "a..b", "host:443"} {
		_, err := prepareTunRouteExclusions(context.Background(), exclusionFixture(input), func(context.Context, string, string) ([]netip.Addr, error) {
			t.Fatal("malformed destination queried DNS")
			return nil, nil
		})
		if err == nil {
			t.Fatalf("accepted %q", input)
		}
	}
	result, err := prepareTunRouteExclusions(context.Background(), exclusionFixture([]string{"212.164.137.29", "2001:db8::29"}), nil)
	if err != nil || strings.Join(fixtureExclusions(t, result), ",") != "212.164.137.29/32,2001:db8::29/128" {
		t.Fatal("plain IP normalization failed", err)
	}
}

func TestTunExclusionsHonorStartRequestCancellation(t *testing.T) {
	request, cancel := context.WithCancel(context.Background())
	started := make(chan struct{})
	lookup := requestScopedTunLookup(request, func(ctx context.Context, _, _ string) ([]netip.Addr, error) {
		close(started)
		<-ctx.Done()
		return nil, ctx.Err()
	})
	go func() { <-started; cancel() }()
	before := time.Now()
	result, err := prepareTunRouteExclusions(context.Background(), exclusionFixture("gateway.invalid"), lookup)
	if !errors.Is(err, context.Canceled) || result != nil || time.Since(before) > time.Second {
		t.Fatal("abandoned Start RPC continued gateway resolution", err)
	}
}
