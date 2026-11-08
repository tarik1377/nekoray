package main

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net"
	"net/netip"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/adapter/outbound"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	localrule "github.com/sagernet/sing-box/route/rule"
	M "github.com/sagernet/sing/common/metadata"
	"github.com/sagernet/sing/service"
	"nekobox_core/robloxauto"
	"nekobox_core/siteauto"
	"nekobox_core/sitebook"
)

const siteServicesType = "greenrhythm-site-services"
const siteServicesTag = "site-services"

var activeSiteServices atomic.Pointer[siteServicesOutbound]

type siteServicesOptions struct {
	robloxAutoOptions
	Library  string `json:"library"`
	Fallback string `json:"fallback"`
}
type expiredSitePolicy struct {
	Policy sitebook.Policy
	Path   robloxauto.Path
	Until  time.Time
}
type sitePolicySnapshot struct {
	Active  *sitebook.Policies
	Removed map[string]expiredSitePolicy
}
type siteDecision struct {
	At     time.Time `json:"at"`
	SiteID string    `json:"site_id"`
	Origin string    `json:"origin"`
	From   string    `json:"from"`
	Mode   string    `json:"mode"`
	Reason string    `json:"reason"`
}
type siteScanJob struct {
	ID      string           `json:"id"`
	Scope   string           `json:"scope"`
	URL     string           `json:"url"`
	State   string           `json:"state"`
	Report  *sitebook.Report `json:"report,omitempty"`
	Error   string           `json:"error,omitempty"`
	Created time.Time        `json:"at"`
	cancel  context.CancelFunc
	done    chan struct{}
}
type siteServicesOutbound struct {
	*robloxAutoOutbound
	router            adapter.Router
	dns               adapter.DNSRouter
	fallbackTag       string
	policy            atomic.Pointer[sitePolicySnapshot]
	configurationHash atomic.Value
	applyMu           sync.Mutex
	historyMu         sync.Mutex
	history           []siteDecision
	previous          map[string]robloxauto.Entry
	jobMu             sync.Mutex
	jobs              map[string]*siteScanJob
}

func newSiteServices(ctx context.Context, router adapter.Router, logger log.ContextLogger, tag string, options siteServicesOptions) (adapter.Outbound, error) {
	_, policies, err := sitebook.Parse(options.Library)
	if err != nil {
		return nil, err
	}
	catalog, _ := siteauto.New([]string{"full:site-services-disabled.invalid"})
	child, err := newAvailabilityOutbound(ctx, logger, tag, options.robloxAutoOptions, catalog)
	if err != nil {
		return nil, err
	}
	base := child.(*robloxAutoOutbound)
	s := &siteServicesOutbound{robloxAutoOutbound: base, router: router, dns: service.FromContext[adapter.DNSRouter](ctx), fallbackTag: options.Fallback, previous: map[string]robloxauto.Entry{}, jobs: map[string]*siteScanJob{}}
	s.policy.Store(&sitePolicySnapshot{Active: policies, Removed: map[string]expiredSitePolicy{}})
	initialHash := sha256.Sum256([]byte(options.Library))
	s.configurationHash.Store(hex.EncodeToString(initialHash[:]))
	base.Adapter = outbound.NewAdapter(siteServicesType, tag, []string{"tcp"}, []string{options.Direct, options.Proxy})
	base.knownHost = func(origin string) bool {
		host, port := sitebook.Address(origin)
		p, ok := s.policy.Load().Active.Items[sitebook.Key(host, port)]
		return ok && p.Origin == origin
	}
	base.extraIdentity = func() string { return s.policy.Load().Active.Digest }
	if base.store != nil {
		base.store.Directory = filepath.Join(".", ".site-services", "cache")
		base.store.AllowHost = base.knownHost
	}
	base.engine.SetPersistence(func(network string, entries map[string]robloxauto.Entry, force bool) {
		if base.store != nil {
			_ = base.store.Save(network, entries, force)
		}
		s.recordDecisions(entries)
	})
	return s, nil
}
func (s *siteServicesOutbound) Start() error {
	if err := s.robloxAutoOutbound.Start(); err != nil {
		return err
	}
	file := filepath.Join(".site-services", "history-"+s.options.Scope[:16]+".json")
	if info, err := os.Stat(file); err == nil && info.Size() < 128<<10 {
		data, _ := os.ReadFile(file)
		var history []siteDecision
		if json.Unmarshal(data, &history) == nil && len(history) <= 200 {
			s.history = history
		}
	}
	return nil
}
func (s *siteServicesOutbound) Close() error {
	s.jobMu.Lock()
	var pending []<-chan struct{}
	for _, job := range s.jobs {
		if job.cancel != nil {
			job.cancel()
			pending = append(pending, job.done)
		}
	}
	s.jobMu.Unlock()
	for _, done := range pending {
		select {
		case <-done:
		case <-time.After(3 * time.Second):
		}
	}
	return s.robloxAutoOutbound.Close()
}
func serviceHeadlessRules(policies *sitebook.Policies) ([]option.HeadlessRule, error) {
	type group struct {
		Port  uint16
		Field string
		Hosts []string
	}
	groups := map[string]*group{}
	for _, entry := range policies.Items {
		field, host := "domain", entry.Host
		if ip, err := netip.ParseAddr(host); err == nil {
			field, host = "ip_cidr", netip.PrefixFrom(ip, ip.BitLen()).String()
		}
		key := sitebook.Key(field, entry.Port)
		if groups[key] == nil {
			groups[key] = &group{Port: entry.Port, Field: field}
		}
		groups[key].Hosts = append(groups[key].Hosts, host)
	}
	if len(groups) == 0 {
		groups["disabled"] = &group{65535, "domain", []string{"site-services-disabled.invalid"}}
	}
	keys := make([]string, 0, len(groups))
	for key := range groups {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	var rules []option.HeadlessRule
	for _, key := range keys {
		entry := groups[key]
		sort.Strings(entry.Hosts)
		data, _ := json.Marshal(map[string]any{entry.Field: entry.Hosts, "port": []uint16{entry.Port}})
		var rule option.HeadlessRule
		if err := json.Unmarshal(data, &rule); err != nil {
			return nil, err
		}
		rules = append(rules, rule)
	}
	return rules, nil
}

func (s *siteServicesOutbound) apply(scope, library, scanID string) (any, error) {
	if s.ctx.Err() != nil {
		return nil, errors.New("profile_changed")
	}
	if scope != s.options.Scope {
		return nil, errors.New("profile_changed")
	}
	_, policies, err := sitebook.Parse(library)
	if err != nil {
		return nil, err
	}
	rules, err := serviceHeadlessRules(policies)
	if err != nil {
		return nil, err
	}
	set, ok := s.router.RuleSet(siteServicesTag)
	if !ok {
		return nil, errors.New("service_matcher_missing")
	}
	local, ok := set.(*localrule.LocalRuleSet)
	if !ok {
		return nil, errors.New("service_matcher_unsupported")
	}
	s.applyMu.Lock()
	defer s.applyMu.Unlock()
	old := s.policy.Load()
	_, entries := s.engine.Snapshot()
	removed := map[string]expiredSitePolicy{}
	for key, prior := range old.Removed {
		if time.Now().Before(prior.Until) {
			removed[key] = prior
		}
	}
	for key, prior := range old.Active.Items {
		if _, kept := policies.Items[key]; !kept {
			path := entries["tcp:"+prior.Origin].Path
			if path != "direct" && path != "proxy" {
				path = robloxauto.Proxy
			}
			removed[key] = expiredSitePolicy{prior, path, time.Now().Add(30 * time.Second)}
		}
	}
	s.policy.Store(&sitePolicySnapshot{policies, removed})
	if err = local.ReplaceServiceRules(rules); err != nil {
		s.policy.Store(old)
		return nil, err
	}
	if s.ctx.Err() != nil {
		return nil, errors.New("profile_changed")
	}
	s.robloxAutoOutbound.mu.Lock()
	s.checked = time.Time{}
	s.robloxAutoOutbound.mu.Unlock()
	s.refreshNetwork()
	if scanID != "" {
		s.seedScan(scanID)
	}
	hash := sha256.Sum256([]byte(library))
	s.configurationHash.Store(hex.EncodeToString(hash[:]))
	return map[string]any{"applied": true, "scope": s.options.Scope, "count": len(policies.Items), "digest": policies.Digest}, nil
}
func (s *siteServicesOutbound) seedScan(id string) {
	s.jobMu.Lock()
	job := s.jobs[id]
	var report *sitebook.Report
	if job != nil && job.State == "completed" && job.Scope == s.options.Scope {
		report = job.Report
	}
	s.jobMu.Unlock()
	if report == nil {
		return
	}
	snapshot := s.policy.Load()
	entries := map[string]robloxauto.Entry{}
	for _, sample := range report.Endpoints {
		host, port := sitebook.Address(sample.Origin)
		policy, ok := snapshot.Active.Items[sitebook.Key(host, port)]
		if !ok || policy.Mode != "auto" {
			continue
		}
		path := sitebook.Choose(sample.Direct, sample.Proxy)
		if path == "unknown" {
			continue
		}
		entries["tcp:"+sample.Origin] = robloxauto.Entry{Path: robloxauto.Path(path), Verified: true, Expires: time.Now().Add(10 * time.Minute), Reason: "anonymous_browser_load"}
	}
	s.engine.Restore(s.engine.Generation(), entries)
	s.engine.NotifyChanges()
}
func (s *siteServicesOutbound) choose(ctx context.Context, destination M.Socksaddr) (robloxauto.Path, string, string) {
	host := autoHost(ctx, destination)
	key := sitebook.Key(host, destination.Port)
	snapshot := s.policy.Load()
	entry, ok := snapshot.Active.Items[key]
	if !ok {
		if prior, ok := snapshot.Removed[key]; ok && time.Now().Before(prior.Until) {
			return prior.Path, "", ""
		}
		return robloxauto.Proxy, "", ""
	}
	if robloxauto.Local(destination.Addr.String()) {
		return robloxauto.Direct, entry.Origin, ""
	}
	if metadata := adapter.ContextFrom(ctx); metadata != nil {
		for _, address := range metadata.DestinationAddresses {
			if !sitebook.PublicIP(address) {
				return robloxauto.Direct, entry.Origin, ""
			}
		}
	}
	if entry.Mode == "direct" {
		return robloxauto.Direct, entry.Origin, ""
	}
	if entry.Mode == "proxy" {
		return robloxauto.Proxy, entry.Origin, ""
	}
	s.refreshNetwork()
	if !strings.HasPrefix(entry.Origin, "https://") {
		return robloxauto.Proxy, entry.Origin, s.engine.Generation()
	}
	probe := robloxauto.WebsiteProbe(func(ctx context.Context, path robloxauto.Path, network, address string) (net.Conn, error) {
		return s.detour(path).DialContext(ctx, network, M.ParseSocksaddr(address))
	}, func(origin string) string {
		if !s.known(origin) {
			return ""
		}
		h, p := sitebook.Address(origin)
		if p == 443 && robloxauto.KnownHost(h) {
			return robloxauto.ProbeURL(h)
		}
		return origin + "/"
	})
	path := s.engine.TCP(ctx, entry.Origin, probe)
	return path, entry.Origin, s.engine.Generation()
}
func (s *siteServicesOutbound) SelectConnection(ctx context.Context, metadata adapter.InboundContext) (adapter.Outbound, error) {
	ctx = adapter.WithContext(ctx, &metadata)
	path, key, generation := s.choose(ctx, metadata.Destination)
	child := s.detour(path)
	return &siteSelected{Adapter: outbound.NewAdapter(child.Type(), child.Tag(), []string{"tcp"}, nil), parent: s, path: path, key: key, generation: generation}, nil
}
func (s *siteServicesOutbound) DialContext(ctx context.Context, network string, destination M.Socksaddr) (net.Conn, error) {
	path, key, generation := s.choose(ctx, destination)
	return (&siteSelected{parent: s, path: path, key: key, generation: generation}).DialContext(ctx, network, destination)
}

type siteSelected struct {
	outbound.Adapter
	parent          *siteServicesOutbound
	path            robloxauto.Path
	key, generation string
}

func (s *siteSelected) DialContext(ctx context.Context, network string, destination M.Socksaddr) (net.Conn, error) {
	conn, err := s.parent.detour(s.path).DialContext(ctx, network, destination)
	if s.generation != "" {
		if err != nil {
			s.parent.engine.InvalidateTCP(s.key, s.generation, s.path)
		} else {
			conn = robloxauto.TrackTCP(conn, 4*time.Second, func() { s.parent.engine.InvalidateTCP(s.key, s.generation, s.path) })
		}
	}
	return conn, err
}
func (s *siteSelected) ListenPacket(ctx context.Context, destination M.Socksaddr) (net.PacketConn, error) {
	return nil, errors.New("site services only handle TCP web endpoints")
}
func (s *siteServicesOutbound) status() any {
	s.historyMu.Lock()
	history := append([]siteDecision{}, s.history...)
	s.historyMu.Unlock()
	network, entries := s.engine.Snapshot()
	effective := map[string]any{}
	for _, value := range s.policy.Load().Active.Items {
		effective[value.Origin] = map[string]any{"mode": value.Mode, "site_ids": value.SiteIDs}
	}
	return map[string]any{"effective": effective, "scope": s.options.Scope, "configuration_hash": s.configurationHash.Load(), "digest": s.policy.Load().Active.Digest, "network": network, "entries": entries, "history": history, "browser_available": sitebook.FindBrowser() != ""}
}
func (s *siteServicesOutbound) recordDecisions(entries map[string]robloxauto.Entry) {
	snapshot := s.policy.Load()
	s.historyMu.Lock()
	defer s.historyMu.Unlock()
	changed := false
	for key, value := range entries {
		if !strings.HasPrefix(key, "tcp:") || !value.Verified {
			continue
		}
		origin := strings.TrimPrefix(key, "tcp:")
		host, port := sitebook.Address(origin)
		policy, ok := snapshot.Active.Items[sitebook.Key(host, port)]
		if !ok {
			continue
		}
		old := s.previous[key]
		if old.Path == value.Path && old.Verified {
			continue
		}
		for _, id := range policy.SiteIDs {
			s.history = append(s.history, siteDecision{time.Now().UTC(), id, origin, string(old.Path), string(value.Path), value.Reason})
		}
		s.previous[key] = value
		changed = true
	}
	if len(s.history) > 200 {
		s.history = s.history[len(s.history)-200:]
	}
	if changed {
		_ = atomicSiteJSON(filepath.Join(".site-services", "history-"+s.options.Scope[:16]+".json"), s.history)
	}
}
func atomicSiteJSON(path string, value any) error {
	data, err := json.Marshal(value)
	if err != nil {
		return err
	}
	if err = os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return err
	}
	file, err := os.CreateTemp(filepath.Dir(path), ".site-*.tmp")
	if err != nil {
		return err
	}
	name := file.Name()
	defer os.Remove(name)
	_ = file.Chmod(0600)
	_, err = file.Write(data)
	if err == nil {
		err = file.Sync()
	}
	closeErr := file.Close()
	if err == nil {
		err = closeErr
	}
	if err != nil {
		return err
	}
	return os.Rename(name, path)
}
func (s *siteServicesOutbound) startScan(scope, raw string) (any, error) {
	if scope != s.options.Scope {
		return nil, errors.New("profile_changed")
	}
	_, origin, err := sitebook.Normalize(raw)
	if err != nil {
		return nil, err
	}
	browser := sitebook.FindBrowser()
	if browser == "" {
		return nil, errors.New("browser_missing")
	}
	s.jobMu.Lock()
	defer s.jobMu.Unlock()
	for _, job := range s.jobs {
		if job.State == "direct" || job.State == "proxy" || job.State == "starting" {
			return nil, errors.New("scan_busy")
		}
	}
	if len(s.jobs) >= 4 {
		var oldest *siteScanJob
		for _, job := range s.jobs {
			if oldest == nil || job.Created.Before(oldest.Created) {
				oldest = job
			}
		}
		delete(s.jobs, oldest.ID)
	}
	random := make([]byte, 12)
	if _, err = rand.Read(random); err != nil {
		return nil, err
	}
	id := hex.EncodeToString(random)
	ctx, cancel := context.WithTimeout(s.ctx, 55*time.Second)
	job := &siteScanJob{ID: id, Scope: scope, URL: origin, State: "starting", Created: time.Now().UTC(), cancel: cancel, done: make(chan struct{})}
	s.jobs[id] = job
	go func() {
		defer close(job.done)
		defer cancel()
		report, err := sitebook.Discover(ctx, browser, raw, func(ctx context.Context, path, network, address string) (net.Conn, error) {
			return s.detour(robloxauto.Path(path)).DialContext(ctx, network, M.ParseSocksaddr(address))
		}, func(ctx context.Context, host string) ([]netip.Addr, error) {
			if s.dns == nil {
				return nil, errors.New("DNS unavailable")
			}
			return s.dns.Lookup(ctx, host, adapter.DNSQueryOptions{})
		}, func(state string) { s.jobMu.Lock(); job.State = state; s.jobMu.Unlock() }, 12*time.Second)
		s.jobMu.Lock()
		defer s.jobMu.Unlock()
		if err != nil {
			job.State = "failed"
			job.Error = "scan_failed"
			if ctx.Err() != nil {
				job.State = "cancelled"
				job.Error = "scan_cancelled"
			}
		} else {
			job.State = "completed"
			job.Report = &report
		}
	}()
	return map[string]any{"id": id, "scope": scope, "state": "starting"}, nil
}
func (s *siteServicesOutbound) scanStatus(id string, cancel bool) (any, error) {
	s.jobMu.Lock()
	defer s.jobMu.Unlock()
	job := s.jobs[id]
	if job == nil {
		return nil, errors.New("scan_missing")
	}
	if cancel {
		job.cancel()
	}
	copy := *job
	return copy, nil
}
