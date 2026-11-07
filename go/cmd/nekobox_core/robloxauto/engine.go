package robloxauto

import (
	"context"
	"sync"
	"time"
)

type Path string

const (
	Direct Path = "direct"
	Proxy  Path = "proxy"
)

type Health int

const (
	Unknown Health = iota
	Working
	Failed
)

type Probe func(context.Context, Path, string) Health

type Entry struct {
	Path           Path      `json:"path"`
	Verified       bool      `json:"verified"`
	Expires        time.Time `json:"expires"`
	DirectFailures int       `json:"direct_failures,omitempty"`
	ProxyFailures  int       `json:"proxy_failures,omitempty"`
	Recovery       int       `json:"recovery,omitempty"`
	Reason         string    `json:"reason"`
}
type flight struct {
	done    chan struct{}
	network string
}
type Engine struct {
	mu             sync.Mutex
	persistSignal  chan struct{}
	persistWorkers sync.WaitGroup
	entries        map[string]Entry
	flights        map[string]*flight
	gate           chan struct{}
	network        string
	ctx            context.Context
	cancel         context.CancelFunc
	workers        sync.WaitGroup
	now            func() time.Time
	TTL            time.Duration
	Retry          time.Duration
	Timeout        time.Duration
	checkWindow    time.Time
	checks         int
	persist        func(string, map[string]Entry, bool)
}

func New(parent context.Context) *Engine {
	ctx, cancel := context.WithCancel(parent)
	e := &Engine{entries: make(map[string]Entry), flights: make(map[string]*flight), gate: make(chan struct{}, 2), persistSignal: make(chan struct{}, 1),
		ctx: ctx, cancel: cancel, now: time.Now, TTL: 10 * time.Minute, Retry: 30 * time.Second, Timeout: 3 * time.Second}
	e.persistWorkers.Add(1)
	go func() {
		defer e.persistWorkers.Done()
		for {
			select {
			case <-ctx.Done():
				e.save(true)
				return
			case <-e.persistSignal:
				if ctx.Err() != nil {
					continue
				}
				e.save(false)
			}
		}
	}()
	return e
}
func (e *Engine) SetPersistence(fn func(string, map[string]Entry, bool)) {
	e.mu.Lock()
	e.persist = fn
	e.mu.Unlock()
}
func (e *Engine) save(force bool) {
	e.mu.Lock()
	fn := e.persist
	network, entries := e.snapshotLocked()
	e.mu.Unlock()
	if fn != nil {
		fn(network, entries, force)
	}
}
func (e *Engine) Close() {
	e.cancel()
	e.mu.Lock()
	e.mu.Unlock()
	e.workers.Wait()
	done := make(chan struct{})
	go func() { e.persistWorkers.Wait(); close(done) }()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
	}
}
func (e *Engine) SetNetwork(value string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.network != value {
		e.network = value
		e.entries = make(map[string]Entry)
	}
}
func (e *Engine) Snapshot() (string, map[string]Entry) {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.snapshotLocked()
}
func (e *Engine) Generation() string { e.mu.Lock(); defer e.mu.Unlock(); return e.network }
func (e *Engine) InvalidateTCP(host, network string, path Path) {
	e.mu.Lock()
	defer e.mu.Unlock()
	key := "tcp:" + host
	entry, exists := e.entries[key]
	if network != e.network || !exists || entry.Path != path || e.ctx.Err() != nil {
		return
	}
	entry.Expires = e.now()
	entry.Verified = false
	entry.Reason = "connection_failed_recheck"
	e.entries[key] = entry
	e.publish()
}
func (e *Engine) snapshotLocked() (string, map[string]Entry) {
	copy := make(map[string]Entry, len(e.entries))
	for k, v := range e.entries {
		copy[k] = v
	}
	return e.network, copy
}
func (e *Engine) publish() {
	select {
	case e.persistSignal <- struct{}{}:
	default:
	}
}
func (e *Engine) putLocked(key string, entry Entry) {
	_, exists := e.entries[key]
	if !exists && len(e.entries) >= 128 { // Bounded even when CDN/UDP destinations rotate.
		oldest := ""
		var at time.Time
		for k, v := range e.entries {
			if oldest == "" || v.Expires.Before(at) {
				oldest = k
				at = v.Expires
			}
		}
		delete(e.entries, oldest)
	}
	e.entries[key] = entry
}

// TCP evaluates a service over the two existing detours. Initial requests wait
// at most Timeout; expired successful choices remain usable during recheck.
// One flight per host and two service checks globally prevent DNS/port bursts.
func (e *Engine) TCP(ctx context.Context, host string, probe Probe) Path {
	key := "tcp:" + host
	e.mu.Lock()
	old, exists := e.entries[key]
	if exists && e.now().Before(old.Expires) {
		e.mu.Unlock()
		return old.Path
	}
	f := e.flights[key]
	if f == nil || f.network != e.network {
		if e.now().Sub(e.checkWindow) >= time.Minute {
			e.checkWindow = e.now()
			e.checks = 0
		}
		if e.checks >= 12 {
			e.mu.Unlock()
			if exists && old.Verified {
				return old.Path
			}
			return Proxy
		}
		select {
		case e.gate <- struct{}{}:
			if e.ctx.Err() != nil {
				<-e.gate
				e.mu.Unlock()
				return Proxy
			}
			f = &flight{done: make(chan struct{}), network: e.network}
			e.checks++
			e.flights[key] = f
			e.workers.Add(1)
			go e.check(key, host, f, probe)
		default:
			e.mu.Unlock()
			if exists {
				return old.Path
			}
			return Proxy
		}
	}
	e.mu.Unlock()
	if exists && old.Verified {
		return old.Path
	}
	select {
	case <-f.done:
		e.mu.Lock()
		defer e.mu.Unlock()
		if f.network == e.network {
			if v, ok := e.entries[key]; ok {
				return v.Path
			}
		}
		return Proxy
	case <-ctx.Done():
		return Proxy
	case <-e.ctx.Done():
		return Proxy
	}
}
func (e *Engine) check(key, host string, f *flight, probe Probe) {
	defer e.workers.Done()
	defer func() { <-e.gate }()
	ctx, cancel := context.WithTimeout(e.ctx, e.Timeout)
	defer cancel()
	type result struct {
		path   Path
		health Health
	}
	results := make(chan result, 2)
	// The probe must honor its context; the HTTP implementation also has its
	// own deadlines. No application traffic is copied to the alternate route.
	var probes sync.WaitGroup
	for _, path := range []Path{Direct, Proxy} {
		probes.Add(1)
		go func(p Path) { defer probes.Done(); results <- result{p, probe(ctx, p, host)} }(path)
	}
	health := map[Path]Health{Direct: Unknown, Proxy: Unknown}
	for i := 0; i < 2; i++ {
		select {
		case value := <-results:
			health[value.path] = value.health
		case <-ctx.Done():
			i = 2
		}
	}
	cancel()
	probes.Wait()
	e.mu.Lock()
	if e.network == f.network && e.ctx.Err() == nil {
		old, exists := e.entries[key]
		chosen := Entry{Path: Proxy, Expires: e.now().Add(e.Retry), Reason: "both_unconfirmed"}
		if exists {
			chosen = old
			chosen.Expires = e.now().Add(e.Retry)
		}
		if health[Direct] == Working {
			// A previously confirmed VPN route needs two successful recovery
			// checks before moving new connections back to DIRECT.
			if exists && old.Verified && old.Path == Proxy && old.Recovery < 1 && health[Proxy] == Working {
				chosen.Recovery++
				chosen.Reason = "direct_recovery_pending"
			} else {
				chosen = Entry{Path: Direct, Verified: true, Expires: e.now().Add(e.TTL), Reason: "direct_https_works"}
			}
		} else if health[Proxy] == Working && health[Direct] == Failed {
			chosen = Entry{Path: Proxy, Verified: true, Expires: e.now().Add(e.TTL), Reason: "direct_failed_vpn_https_works"}
		} else if health[Proxy] == Working {
			chosen = Entry{Path: Proxy, Verified: true, Expires: e.now().Add(e.Retry), Reason: "direct_ambiguous_vpn_works"}
		} else {
			chosen.Verified = false
			chosen.Reason = "both_unconfirmed"
		}
		e.putLocked(key, chosen)
	}
	if e.flights[key] == f {
		delete(e.flights, key)
	}
	close(f.done)
	e.mu.Unlock()
	e.publish()
}

// UDP decisions use real replies from the actual destination, not a generic
// STUN ping. Two failed sessions may try the other path on a NEW flow only.
// Existing sockets are never closed, migrated, replayed or duplicated.
func (e *Engine) UDP(key string) (Path, string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	if value, ok := e.entries["udp:"+key]; ok && e.now().Before(value.Expires) {
		return value.Path, e.network
	}
	return Direct, e.network
}
func (e *Engine) ObserveUDP(key, network string, path Path, reply bool) {
	e.mu.Lock()
	if network != e.network || e.ctx.Err() != nil {
		e.mu.Unlock()
		return
	}
	key = "udp:" + key
	value, exists := e.entries[key]
	// Feedback from an older flow on the other path must not flap a newer,
	// already confirmed selection or shorten its TTL.
	if exists && value.Verified && value.Path != path && e.now().Before(value.Expires) {
		e.mu.Unlock()
		return
	}
	if !exists || !e.now().Before(value.Expires) {
		value = Entry{Path: path}
	}
	if reply {
		value = Entry{Path: path, Verified: true, Expires: e.now().Add(e.TTL), Reason: "destination_udp_reply"}
	} else {
		if path == Direct {
			value.DirectFailures++
		} else {
			value.ProxyFailures++
		}
		value.Expires = e.now().Add(e.Retry)
		value.Reason = "udp_no_reply"
		if value.DirectFailures >= 2 && value.ProxyFailures >= 2 {
			value.Reason = "both_udp_unconfirmed"
		} else if path == value.Path && (path == Direct && value.DirectFailures >= 2 || path == Proxy && value.ProxyFailures >= 2) {
			if path == Direct {
				value.Path = Proxy
			} else {
				value.Path = Direct
			}
			value.Verified = false
			value.Reason = "try_alternative_next_udp_flow"
		}
	}
	e.putLocked(key, value)
	e.mu.Unlock()
	e.publish()
}
