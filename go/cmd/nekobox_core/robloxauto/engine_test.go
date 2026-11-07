package robloxauto

import (
	"context"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func engine(t *testing.T) *Engine {
	t.Helper()
	e := New(context.Background())
	e.SetNetwork("network-a")
	t.Cleanup(e.Close)
	return e
}
func TestTCPWorkingChoiceAndMemory(t *testing.T) {
	for _, test := range []struct {
		name          string
		direct, proxy Health
		want          Path
		verified      bool
	}{
		{"direct", Working, Working, Direct, true}, {"blocked", Failed, Working, Proxy, true},
		{"both-down", Failed, Failed, Proxy, false}, {"ambiguous", Unknown, Unknown, Proxy, false},
		{"permission-is-not-a-block", Unknown, Working, Proxy, true},
	} {
		t.Run(test.name, func(t *testing.T) {
			e := engine(t)
			var count atomic.Int32
			probe := func(_ context.Context, path Path, _ string) Health {
				count.Add(1)
				if path == Direct {
					return test.direct
				}
				return test.proxy
			}
			for range 3 {
				if got := e.TCP(context.Background(), "users.roblox.com", probe); got != test.want {
					t.Fatalf("got %s", got)
				}
			}
			e.workers.Wait()
			_, snapshot := e.Snapshot()
			if count.Load() != 2 || snapshot["tcp:users.roblox.com"].Verified != test.verified {
				t.Fatal("cache/verification", count.Load(), snapshot)
			}
		})
	}
}
func TestSingleFlightAndConcurrencyLimit(t *testing.T) {
	e := engine(t)
	release := make(chan struct{})
	var started, active, max atomic.Int32
	probe := func(ctx context.Context, _ Path, _ string) Health {
		started.Add(1)
		value := active.Add(1)
		for old := max.Load(); value > old; old = max.Load() {
			if max.CompareAndSwap(old, value) {
				break
			}
		}
		defer active.Add(-1)
		select {
		case <-release:
			return Working
		case <-ctx.Done():
			return Failed
		}
	}
	var requests sync.WaitGroup
	for range 30 {
		requests.Add(1)
		go func() { defer requests.Done(); e.TCP(context.Background(), "apis.roblox.com", probe) }()
	}
	deadline := time.Now().Add(time.Second)
	for started.Load() < 2 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if started.Load() != 2 {
		t.Fatal("not deduplicated", started.Load())
	}
	close(release)
	requests.Wait()
	e.workers.Wait()
	if started.Load() != 2 || max.Load() > 4 {
		t.Fatal("probe burst", started.Load(), max.Load())
	}
}
func TestTCPRecoveryHysteresisAndNetworkFence(t *testing.T) {
	e := engine(t)
	now := time.Now()
	e.now = func() time.Time { return now }
	mode := Failed
	probe := func(_ context.Context, path Path, _ string) Health {
		if path == Direct {
			return mode
		}
		return Working
	}
	if e.TCP(context.Background(), "users.roblox.com", probe) != Proxy {
		t.Fatal("expected VPN")
	}
	e.workers.Wait()
	mode = Working
	now = now.Add(e.TTL + time.Second)
	e.TCP(context.Background(), "users.roblox.com", probe)
	e.workers.Wait()
	_, first := e.Snapshot()
	if first["tcp:users.roblox.com"].Path != Proxy {
		t.Fatal("flapped after one recovery")
	}
	now = now.Add(e.Retry + time.Second)
	e.TCP(context.Background(), "users.roblox.com", probe)
	e.workers.Wait()
	_, second := e.Snapshot()
	if second["tcp:users.roblox.com"].Path != Direct {
		t.Fatal("did not recover")
	}
	e.SetNetwork("network-b")
	_, fresh := e.Snapshot()
	if len(fresh) != 0 {
		t.Fatal("old network reused")
	}
	start, release := make(chan struct{}, 2), make(chan struct{})
	done := make(chan Path, 1)
	go func() {
		done <- e.TCP(context.Background(), "apis.roblox.com", func(ctx context.Context, _ Path, _ string) Health {
			start <- struct{}{}
			select {
			case <-release:
				return Working
			case <-ctx.Done():
				return Failed
			}
		})
	}()
	<-start
	<-start
	e.SetNetwork("network-c")
	close(release)
	<-done
	e.workers.Wait()
	_, fresh = e.Snapshot()
	if len(fresh) != 0 {
		t.Fatal("old probe wrote into a new network")
	}
}
func TestUDPActualRepliesFallbackAndStaleFlow(t *testing.T) {
	e := engine(t)
	key := "128.116.1.2:50000"
	path, generation := e.UDP(key)
	if path != Direct {
		t.Fatal("UDP should start directly")
	}
	e.ObserveUDP(key, generation, path, false)
	if p, _ := e.UDP(key); p != Direct {
		t.Fatal("one failure moved route")
	}
	e.ObserveUDP(key, generation, path, false)
	if p, _ := e.UDP(key); p != Proxy {
		t.Fatal("next flow did not try VPN")
	}
	e.ObserveUDP(key, generation, Proxy, true)
	_, snapshot := e.Snapshot()
	if !snapshot["udp:"+key].Verified || snapshot["udp:"+key].Path != Proxy {
		t.Fatal("reply not remembered")
	}
	e.SetNetwork("new-network")
	e.ObserveUDP(key, generation, Proxy, true)
	if p, _ := e.UDP(key); p != Direct {
		t.Fatal("old flow contaminated new network")
	}
	for i := 0; i < 300; i++ {
		e.ObserveUDP(string(rune(i))+"-destination", "new-network", Direct, true)
	}
	_, snapshot = e.Snapshot()
	if len(snapshot) > 128 {
		t.Fatal("unbounded destination cache")
	}
}
func TestCloseCancelsProbeAndStopsNewWork(t *testing.T) {
	e := engine(t)
	started := make(chan struct{}, 2)
	done := make(chan struct{})
	go func() {
		e.TCP(context.Background(), "apis.roblox.com", func(ctx context.Context, _ Path, _ string) Health { started <- struct{}{}; <-ctx.Done(); return Failed })
		close(done)
	}()
	<-started
	<-started
	e.Close()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("request survived shutdown")
	}
	if e.TCP(context.Background(), "users.roblox.com", func(context.Context, Path, string) Health { t.Error("probe after close"); return Working }) != Proxy {
		t.Fatal("wrong shutdown fallback")
	}
}

func TestFailedCachedConnectionRechecksAndIgnoresOldFeedback(t *testing.T) {
	e := engine(t)
	e.TCP(context.Background(), "users.roblox.com", func(context.Context, Path, string) Health { return Working })
	e.workers.Wait()
	e.InvalidateTCP("users.roblox.com", "old-network", Direct)
	_, cache := e.Snapshot()
	if !cache["tcp:users.roblox.com"].Verified {
		t.Fatal("old network invalidated current choice")
	}
	e.InvalidateTCP("users.roblox.com", "network-a", Direct)
	got := e.TCP(context.Background(), "users.roblox.com", func(_ context.Context, path Path, _ string) Health {
		if path == Direct {
			return Failed
		}
		return Working
	})
	if got != Proxy {
		t.Fatal("failed cached connection did not cause a fresh check", got)
	}
	e.ObserveUDP("128.116.1.2:50000", "network-a", Proxy, true)
	e.ObserveUDP("128.116.1.2:50000", "network-a", Direct, true)
	e.ObserveUDP("128.116.1.2:50000", "network-a", Direct, false)
	if path, _ := e.UDP("128.116.1.2:50000"); path != Proxy {
		t.Fatal("old UDP flow flapped a confirmed new selection")
	}
}

func TestGlobalProbeBudget(t *testing.T) {
	e := engine(t)
	release := make(chan struct{})
	var active, maximum atomic.Int32
	var waiting sync.WaitGroup
	probe := func(ctx context.Context, _ Path, _ string) Health {
		n := active.Add(1)
		for old := maximum.Load(); n > old; old = maximum.Load() {
			if maximum.CompareAndSwap(old, n) {
				break
			}
		}
		defer active.Add(-1)
		select {
		case <-release:
			return Working
		case <-ctx.Done():
			return Failed
		}
	}
	for _, host := range []string{"users.roblox.com", "apis.roblox.com"} {
		waiting.Add(1)
		go func(host string) { defer waiting.Done(); e.TCP(context.Background(), host, probe) }(host)
	}
	deadline := time.Now().Add(time.Second)
	for active.Load() < 4 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	for i := 0; i < 100; i++ {
		if e.TCP(context.Background(), "cdn-"+string(rune('a'+i))+".rbxcdn.com", probe) != Proxy {
			t.Fatal("busy fallback")
		}
	}
	if maximum.Load() != 4 {
		t.Fatal("more than two concurrent service checks", maximum.Load())
	}
	close(release)
	waiting.Wait()
}

func TestSequentialProbeBudget(t *testing.T) {
	e := engine(t)
	var calls atomic.Int32
	probe := func(context.Context, Path, string) Health { calls.Add(1); return Working }
	for i := 0; i < 20; i++ {
		e.TCP(context.Background(), "service-"+string(rune('a'+i))+".roblox.com", probe)
	}
	e.workers.Wait()
	if calls.Load() != 24 {
		t.Fatal("minute budget exceeded", calls.Load())
	}
}
func TestSlowCacheCannotDelayUDPAndShutdownIsBounded(t *testing.T) {
	e := engine(t)
	started, release := make(chan struct{}, 1), make(chan struct{})
	e.SetPersistence(func(string, map[string]Entry, bool) {
		select {
		case started <- struct{}{}:
		default:
		}
		<-release
	})
	e.ObserveUDP("first", "network-a", Direct, true)
	<-started
	done := make(chan struct{})
	go func() { e.ObserveUDP("second", "network-a", Direct, true); close(done) }()
	select {
	case <-done:
	case <-time.After(time.Second):
		close(release)
		t.Fatal("cache I/O blocked forwarding observation")
	}
	closed := make(chan struct{})
	go func() { e.Close(); close(closed) }()
	select {
	case <-closed:
	case <-time.After(3 * time.Second):
		close(release)
		t.Fatal("slow cache blocked core shutdown")
	}
	close(release)
}
