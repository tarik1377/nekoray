package main

import (
	"context"
	"crypto/tls"
	"encoding/json"
	"errors"
	"net"
	"net/http"
	"net/netip"
	"os"
	"path/filepath"
	"sync"
	"time"

	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/option"
	M "github.com/sagernet/sing/common/metadata"
)

var geoDataShutdown struct {
	sync.Mutex
	pending <-chan struct{}
}

// Stop RPCs remain bounded even on a malfunctioning filesystem/transport. A
// timed-out writer prevents the next instance from starting until it is gone,
// so an old rollback can never run after the next instance has published rules.
func cancelGeoDataAndWait(cancel context.CancelFunc, done <-chan struct{}, timeout time.Duration) {
	cancel()
	timer := time.NewTimer(timeout)
	defer timer.Stop()
	select {
	case <-done:
	case <-timer.C:
		geoDataShutdown.Lock()
		geoDataShutdown.pending = done
		geoDataShutdown.Unlock()
	}
}

func previousGeoDataStopped() bool {
	geoDataShutdown.Lock()
	defer geoDataShutdown.Unlock()
	if geoDataShutdown.pending == nil {
		return true
	}
	select {
	case <-geoDataShutdown.pending:
		geoDataShutdown.pending = nil
		return true
	default:
		return false
	}
}

func geoDataBootstrapPath() string {
	// A distinct reserved filename prevents a package upgrade from replacing
	// a user's old/custom geoip-ru.srs. Legacy is only an old-package fallback.
	if _, err := os.Stat("geoip-ru-bootstrap.srs"); err == nil {
		return "geoip-ru-bootstrap.srs"
	}
	return "geoip-ru.srs"
}

func managedGeoDataPath() (string, error) {
	directory, err := filepath.Abs(".geodata")
	if err != nil {
		return "", err
	}
	if err := os.MkdirAll(directory, 0700); err != nil {
		return "", err
	}
	if info, err := os.Lstat(directory); err != nil || !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
		return "", errors.New("invalid managed geodata directory")
	}
	if _, _, err := prepareGeoDataCache(directory, geoDataBootstrapPath()); err != nil {
		return "", err
	}
	return geoDataManagedPath, nil
}

func usesManagedGeoData(options option.Options) bool {
	if options.Route == nil {
		return false
	}
	for _, set := range options.Route.RuleSet {
		if set.Tag != "geoip-ru" || set.Type != "local" {
			continue
		}
		path, err := filepath.Abs(set.LocalOptions.Path)
		managed, managedErr := filepath.Abs(geoDataManagedPath)
		if err == nil && managedErr == nil && path == managed {
			return true
		}
	}
	return false
}

func geoDataLiveWaiter(set adapter.RuleSet) geoDataApplied {
	return func(ctx context.Context, prefixes []netip.Prefix) error {
		wanted, err := encodeGeoData(prefixes)
		if err != nil {
			return err
		}
		matches := func() bool {
			var live []netip.Prefix
			for _, ipSet := range set.ExtractIPSet() {
				live = append(live, ipSet.Prefixes()...)
			}
			encoded, err := encodeGeoData(live)
			return err == nil && geoDataHash(encoded) == geoDataHash(wanted)
		}
		if matches() {
			return nil
		}
		changed := make(chan struct{}, 1)
		registration := set.RegisterCallback(func(adapter.RuleSet) {
			select {
			case changed <- struct{}{}:
			default:
			}
		})
		defer set.UnregisterCallback(registration)
		for {
			if matches() {
				return nil
			}
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-changed:
			}
		}
	}
}

func startGeoDataManager(ctx context.Context, instance *box.Box, manager *geoDataManager) bool {
	set, exists := instance.Router().RuleSet("geoip-ru")
	if !exists {
		return false
	}
	outbound, exists := instance.Outbound().Outbound("proxy")
	var fetch geoDataFetch
	if !exists {
		fetch = func(context.Context, string) (geoDataCandidate, error) {
			return geoDataCandidate{}, &geoDataError{"VPN download outbound is unavailable; previous country rules remain active"}
		}
	} else {
		transport := &http.Transport{
			ForceAttemptHTTP2:     true,
			TLSHandshakeTimeout:   5 * time.Second,
			ResponseHeaderTimeout: 5 * time.Second,
			IdleConnTimeout:       30 * time.Second,
			MaxConnsPerHost:       2,
			TLSClientConfig:       &tls.Config{MinVersion: tls.VersionTLS12, RootCAs: adapter.RootPoolFromContext(ctx)},
			DialContext: func(ctx context.Context, network, address string) (net.Conn, error) {
				return outbound.DialContext(ctx, network, M.ParseSocksaddr(address))
			},
		}
		fetch = fetchGeoDataText(&http.Client{Transport: transport, Timeout: 30 * time.Second})
		go func() { <-ctx.Done(); transport.CloseIdleConnections() }()
	}
	manager.Start(ctx, fetch, geoDataLiveWaiter(set))
	return true
}

func writeUnusedGeoDataStatus() {
	directory, err := filepath.Abs(".geodata")
	if err != nil || os.MkdirAll(directory, 0700) != nil {
		return
	}
	if info, err := os.Lstat(directory); err != nil || !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
		return
	}
	m := &geoDataManager{directory: directory, atomicWrite: writeGeoDataAtomic}
	if raw, err := readGeoDataFile(m.path("status.json"), 16384); err == nil {
		_ = json.Unmarshal(raw, &m.status)
	}
	if pending := m.pendingManual(); pending != "" {
		m.status.LastRequest = pending
	}
	m.status.State, m.status.Source, m.status.Error = "unused", geoDataSourceID, ""
	_ = m.saveStatus()
}
