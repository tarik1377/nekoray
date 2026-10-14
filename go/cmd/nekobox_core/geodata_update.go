package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"math/rand/v2"
	"net/http"
	"net/netip"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/gofrs/uuid/v5"
)

const (
	geoDataSourceID       = "Loyalsoldier/geoip (MaxMind GeoLite2)"
	geoDataSourceURL      = "https://raw.githubusercontent.com/Loyalsoldier/geoip/release/text/ru.txt"
	geoDataManagedPath    = ".geodata/geoip-ru.srs"
	geoDataDailyInterval  = 24 * time.Hour
	geoDataManualThrottle = 30 * time.Second
)

type geoDataSettings struct {
	Enabled bool `json:"enabled"`
	Present bool `json:"-"`
}

type geoDataStatus struct {
	State       string    `json:"state"`
	CheckedAt   time.Time `json:"checked_at,omitempty"`
	UpdatedAt   time.Time `json:"updated_at,omitempty"`
	AppliedAt   time.Time `json:"applied_at,omitempty"`
	NextRetry   time.Time `json:"next_retry,omitempty"`
	SHA256      string    `json:"sha256,omitempty"`
	PrefixCount int       `json:"prefix_count"`
	Source      string    `json:"source"`
	Error       string    `json:"error,omitempty"`
	ETag        string    `json:"etag,omitempty"`
	LastAttempt time.Time `json:"last_attempt_at,omitempty"`
	LastRequest string    `json:"last_request_id,omitempty"`
	Failures    int       `json:"failures,omitempty"`
}

type geoDataCandidate struct {
	Prefixes    []netip.Prefix
	ETag        string
	NotModified bool
}

type geoDataFetch func(context.Context, string) (geoDataCandidate, error)
type geoDataApplied func(context.Context, []netip.Prefix) error

// All filesystem/network mutation lives on this instance's goroutine. The box
// context cancels pending HTTP/apply waits; no OS route/proxy/DNS API is used.
type geoDataManager struct {
	directory           string
	settings            geoDataSettings
	settingsFingerprint [32]byte
	settingsExisted     bool
	settingsModified    time.Time
	status              geoDataStatus
	fetch               geoDataFetch
	waitApplied         geoDataApplied
	now                 func() time.Time
	atomicWrite         func(string, []byte) error
	initialDelay        time.Duration
	pollInterval        time.Duration
	started             sync.Once
	done                chan struct{}
}

// extractGeoDataSettings removes our extension before strict sing-box parsing.
// Absent means no automatic scheduling; manual refresh remains available.
func extractGeoDataSettings(content []byte) ([]byte, geoDataSettings, error) {
	var root map[string]json.RawMessage
	if err := json.Unmarshal(content, &root); err != nil {
		return nil, geoDataSettings{}, err
	}
	var settings geoDataSettings
	if raw, exists := root["geodata_update"]; exists {
		if len(raw) == 0 || raw[0] != '{' {
			return nil, settings, errors.New("geodata_update must be an object")
		}
		if err := json.Unmarshal(raw, &settings); err != nil {
			return nil, settings, errors.New("invalid geodata_update settings")
		}
		delete(root, "geodata_update")
		settings.Present = true
	}
	clean, err := json.Marshal(root)
	return clean, settings, err
}

func geoDataHash(content []byte) string {
	hash := sha256.Sum256(content)
	return hex.EncodeToString(hash[:])
}

func readGeoDataFile(path string, maximum int64) ([]byte, error) {
	info, err := os.Lstat(path)
	if err != nil {
		return nil, err
	}
	if !info.Mode().IsRegular() || info.Size() > maximum {
		return nil, errors.New("invalid or oversized managed file")
	}
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	content, err := io.ReadAll(io.LimitReader(file, maximum+1))
	if err != nil || int64(len(content)) > maximum {
		return nil, errors.New("could not read bounded managed file")
	}
	return content, nil
}

func writeGeoDataAtomic(path string, content []byte) error {
	if info, err := os.Lstat(path); err == nil && !info.Mode().IsRegular() {
		return errors.New("managed destination is not a regular file")
	} else if err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	temporary, err := os.CreateTemp(filepath.Dir(path), ".geodata-pending-")
	if err != nil {
		return err
	}
	name := temporary.Name()
	defer os.Remove(name)
	if err = temporary.Chmod(0600); err == nil {
		_, err = temporary.Write(content)
	}
	if err == nil {
		err = temporary.Sync()
	}
	closeErr := temporary.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return closeErr
	}
	return os.Rename(name, path)
}

// prepareGeoDataManager is deliberately offline and runs before box.Start.
// Valid cached rules win; a corrupt cache recovers from last-good, then bundle.
// Only .geodata is owned by this manager. The bundled/custom source is read-only.
func prepareGeoDataManager(root, seedPath string, settings geoDataSettings) (*geoDataManager, error) {
	directory, err := filepath.Abs(filepath.Join(root, ".geodata"))
	if err != nil {
		return nil, err
	}
	if err := os.MkdirAll(directory, 0700); err != nil {
		return nil, err
	}
	if info, err := os.Lstat(directory); err != nil || !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
		return nil, errors.New("invalid managed geodata directory")
	}
	m := &geoDataManager{directory: directory, settings: settings, now: time.Now,
		atomicWrite: writeGeoDataAtomic, initialDelay: 3 * time.Second,
		pollInterval: 2 * time.Second, done: make(chan struct{}),
		status: geoDataStatus{State: "idle", Source: geoDataSourceID}}
	if raw, err := readGeoDataFile(m.path("status.json"), 16384); err == nil {
		_ = json.Unmarshal(raw, &m.status)
		m.status.Source = geoDataSourceID
	}
	// An old process's settings file never overrides this config at startup.
	if raw, err := readGeoDataFile(m.path("settings.json"), 1024); err == nil {
		m.settingsExisted = true
		m.settingsFingerprint = sha256.Sum256(raw)
		if info, statErr := os.Stat(m.path("settings.json")); statErr == nil {
			m.settingsModified = info.ModTime()
		}
	}
	content, prefixes, err := prepareGeoDataCache(directory, seedPath)
	if err != nil {
		return nil, err
	}
	hash := geoDataHash(content)
	if m.status.SHA256 != hash {
		m.status.CheckedAt = time.Time{}
		m.status.UpdatedAt = time.Time{}
		m.status.AppliedAt = time.Time{}
		m.status.ETag = ""
		m.status.NextRetry = time.Time{}
	}
	m.status.SHA256 = hash
	m.status.PrefixCount = len(prefixes)
	m.status.Error = ""
	m.status.State = "idle"
	if !settings.Enabled {
		m.status.State = "disabled"
	}
	_ = m.saveStatus()
	return m, nil
}

// This helper is also used by config transformation. It never starts a manager,
// writes settings/status or performs HTTP, including URL-test configurations.
func prepareGeoDataCache(directory, seedPath string) ([]byte, []netip.Prefix, error) {
	var content []byte
	var prefixes []netip.Prefix
	for _, path := range []string{filepath.Join(directory, "geoip-ru.srs"), filepath.Join(directory, "geoip-ru.lastgood.srs"), seedPath} {
		if path == "" {
			continue
		}
		candidate, readErr := readGeoDataFile(path, geoDataMaxDownload)
		if readErr != nil {
			continue
		}
		parsed, parseErr := decodeGeoData(candidate)
		if parseErr != nil {
			continue
		}
		content, prefixes = candidate, parsed
		if path != filepath.Join(directory, "geoip-ru.srs") {
			if err := writeGeoDataAtomic(filepath.Join(directory, "geoip-ru.srs"), content); err != nil {
				return nil, nil, err
			}
		}
		break
	}
	if len(prefixes) == 0 {
		return nil, nil, errors.New("no valid offline RU country rule-set")
	}
	return content, prefixes, nil
}

func (m *geoDataManager) path(name string) string { return filepath.Join(m.directory, name) }

func (m *geoDataManager) saveStatus() error {
	raw, err := json.Marshal(m.status)
	if err != nil {
		return err
	}
	var fields map[string]json.RawMessage
	if err := json.Unmarshal(raw, &fields); err != nil {
		return err
	}
	for key, value := range map[string]time.Time{"checked_at": m.status.CheckedAt, "updated_at": m.status.UpdatedAt,
		"applied_at": m.status.AppliedAt, "next_retry": m.status.NextRetry, "last_attempt_at": m.status.LastAttempt} {
		if value.IsZero() {
			delete(fields, key)
		}
	}
	raw, err = json.MarshalIndent(fields, "", "  ")
	if err != nil {
		return err
	}
	return m.atomicWrite(m.path("status.json"), raw)
}

func (m *geoDataManager) readSettingsChange() {
	raw, err := readGeoDataFile(m.path("settings.json"), 1024)
	if err != nil {
		return
	}
	hash := sha256.Sum256(raw)
	info, err := os.Stat(m.path("settings.json"))
	if err != nil {
		return
	}
	if m.settingsExisted && hash == m.settingsFingerprint && info.ModTime().Equal(m.settingsModified) {
		return
	}
	var settings geoDataSettings
	if len(raw) == 0 || raw[0] != '{' || json.Unmarshal(raw, &settings) != nil {
		return
	}
	m.settingsFingerprint, m.settingsExisted = hash, true
	m.settingsModified = info.ModTime()
	m.settings = settings
	if !settings.Enabled && m.status.State != "checking" {
		m.status.State = "disabled"
	} else if settings.Enabled && m.status.State == "disabled" {
		m.status.State = "idle"
	}
	_ = m.saveStatus()
}

func (m *geoDataManager) pendingManual() string {
	raw, err := readGeoDataFile(m.path("refresh.request"), 128)
	if err != nil {
		return ""
	}
	request := strings.TrimSpace(string(raw))
	if _, err := uuid.FromString(request); err != nil || request == m.status.LastRequest {
		return ""
	}
	return request
}

func (m *geoDataManager) Start(ctx context.Context, fetch geoDataFetch, applied geoDataApplied) {
	m.started.Do(func() {
		m.fetch, m.waitApplied = fetch, applied
		go m.loop(ctx)
	})
}

func (m *geoDataManager) loop(ctx context.Context) {
	defer close(m.done)
	timer := time.NewTimer(m.initialDelay)
	defer timer.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-timer.C:
			m.tick(ctx)
			timer.Reset(m.pollInterval)
		}
	}
}

func (m *geoDataManager) tick(ctx context.Context) {
	m.readSettingsChange()
	request := m.pendingManual()
	now := m.now()
	if !m.status.LastAttempt.IsZero() && now.Sub(m.status.LastAttempt) < geoDataManualThrottle {
		return
	}
	if request == "" {
		if !m.settings.Enabled || (!m.status.NextRetry.IsZero() && now.Before(m.status.NextRetry)) {
			return
		}
		if m.status.Failures == 0 && !m.status.CheckedAt.IsZero() && now.Sub(m.status.CheckedAt) < geoDataDailyInterval {
			return
		}
	}
	if m.fetch == nil {
		return
	}
	m.status.LastRequest = requestOrPrevious(request, m.status.LastRequest)
	m.status.LastAttempt = now
	m.status.State, m.status.Error = "checking", ""
	_ = m.saveStatus()
	err := m.update(ctx)
	if ctx.Err() != nil {
		return
	}
	if err != nil {
		m.status.Failures++
		// Never include remote bodies, URLs, credentials or arbitrary source lines.
		m.status.State, m.status.Error = "error", safeGeoDataError(err)
		var unconfirmed *geoDataUnconfirmed
		if errors.As(err, &unconfirmed) {
			m.status.State = "unconfirmed"
		}
		backoff := 5 * time.Minute
		for i := 1; i < m.status.Failures && backoff < 6*time.Hour; i++ {
			backoff *= 2
		}
		backoff = min(backoff, 6*time.Hour)
		m.status.NextRetry = now.Add(backoff + time.Duration(rand.Int64N(int64(backoff/10+1))))
	} else {
		m.status.Failures = 0
		m.status.NextRetry = m.status.CheckedAt.Add(geoDataDailyInterval)
	}
	if err := m.saveStatus(); err != nil {
		log.Print("geodata: could not save refresh status")
	}
}

func requestOrPrevious(request, previous string) string {
	if request != "" {
		return request
	}
	return previous
}

func safeGeoDataError(err error) string {
	var unconfirmed *geoDataUnconfirmed
	if errors.As(err, &unconfirmed) {
		return unconfirmed.message
	}
	var typed *geoDataError
	if errors.As(err, &typed) {
		return typed.message
	}
	return "Country rules could not be refreshed; previous rules remain active"
}

type geoDataError struct{ message string }

func (e *geoDataError) Error() string { return e.message }

type geoDataUnconfirmed struct{ message string }

func (e *geoDataUnconfirmed) Error() string { return e.message }

func (m *geoDataManager) update(ctx context.Context) error {
	candidate, err := m.fetch(ctx, m.status.ETag)
	if err != nil {
		return err
	}
	old, err := readGeoDataFile(m.path("geoip-ru.srs"), geoDataMaxDownload)
	if err != nil {
		return err
	}
	oldPrefixes, err := decodeGeoData(old)
	if err != nil {
		return &geoDataError{"Previous country cache failed validation"}
	}
	if candidate.NotModified {
		if geoDataHash(old) != m.status.SHA256 {
			// A stale ETag cannot bless a changed/corrupt on-disk file.
			m.status.ETag = ""
			m.status.AppliedAt = time.Time{}
			return &geoDataUnconfirmed{"Country cache changed; a complete refresh will be retried"}
		}
		if err := m.confirmCurrent(ctx, oldPrefixes); err != nil {
			return err
		}
		m.status.CheckedAt = m.now()
		m.status.PrefixCount = len(oldPrefixes)
		m.status.State = "current"
		return nil
	}
	prefixes, err := normalizeGeoData(candidate.Prefixes)
	if err != nil {
		return &geoDataError{"Downloaded country prefixes failed validation"}
	}
	content, err := encodeGeoData(prefixes)
	if err != nil {
		return err
	}
	hash := geoDataHash(content)
	if referenceCount := max(len(oldPrefixes), m.status.PrefixCount); referenceCount >= 1000 && len(prefixes)*2 < referenceCount {
		return &geoDataError{"Country prefix list shrank unexpectedly; previous rules remain active"}
	}
	if hash == geoDataHash(old) {
		if err := m.confirmCurrent(ctx, prefixes); err != nil {
			return err
		}
		if m.status.SHA256 != hash {
			m.status.UpdatedAt = m.now()
		}
		m.status.SHA256, m.status.PrefixCount = hash, len(prefixes)
		m.status.CheckedAt, m.status.ETag = m.now(), candidate.ETag
		m.status.State = "current"
		return nil
	}
	if err := m.atomicWrite(m.path("geoip-ru.lastgood.srs"), old); err != nil {
		return err
	}
	if err := m.atomicWrite(m.path("geoip-ru.srs"), content); err != nil {
		return err
	}
	if m.waitApplied != nil {
		waitCtx, cancel := context.WithTimeout(ctx, 5*time.Second)
		err = m.waitApplied(waitCtx, prefixes)
		cancel()
		if err != nil {
			if restoreErr := m.atomicWrite(m.path("geoip-ru.srs"), old); restoreErr != nil {
				m.status.AppliedAt = time.Time{}
				return &geoDataUnconfirmed{"Country refresh was not confirmed; restore from last-good backup is pending"}
			}
			if ctx.Err() == nil {
				restoreCtx, restoreCancel := context.WithTimeout(ctx, 5*time.Second)
				restoreErr := m.waitApplied(restoreCtx, oldPrefixes)
				restoreCancel()
				if restoreErr != nil {
					m.status.AppliedAt = time.Time{}
					return &geoDataUnconfirmed{"Previous country rules are restored on disk; active rules could not be confirmed"}
				}
			}
			return &geoDataError{"Country refresh was not confirmed; previous rules were restored"}
		}
	}
	now := m.now()
	m.status.CheckedAt, m.status.UpdatedAt, m.status.AppliedAt = now, now, now
	m.status.SHA256, m.status.PrefixCount = hash, len(prefixes)
	m.status.ETag, m.status.State = candidate.ETag, "updated"
	return nil
}

func (m *geoDataManager) confirmCurrent(ctx context.Context, prefixes []netip.Prefix) error {
	if m.waitApplied != nil {
		waitCtx, cancel := context.WithTimeout(ctx, 5*time.Second)
		err := m.waitApplied(waitCtx, prefixes)
		cancel()
		if err != nil {
			m.status.AppliedAt = time.Time{}
			return &geoDataUnconfirmed{"Country cache is valid; active rules could not be confirmed"}
		}
	}
	m.status.AppliedAt = m.now()
	return nil
}

// A dedicated client routes requests through an explicit box outbound. This
// downloader never reads HTTP_PROXY, subscription insecure flags or system proxy.
func fetchGeoDataText(client *http.Client) geoDataFetch {
	return func(ctx context.Context, etag string) (geoDataCandidate, error) {
		ctx, cancel := context.WithTimeout(ctx, 30*time.Second)
		defer cancel()
		clientCopy := *client
		clientCopy.Timeout = 30 * time.Second
		clientCopy.CheckRedirect = func(request *http.Request, via []*http.Request) error {
			if len(via) >= 3 || request.URL.Scheme != "https" || request.URL.Host != "raw.githubusercontent.com" {
				return errors.New("country source redirect rejected")
			}
			return nil
		}
		request, err := http.NewRequestWithContext(ctx, http.MethodGet, geoDataSourceURL, nil)
		if err != nil {
			return geoDataCandidate{}, err
		}
		request.Header.Set("Accept", "text/plain")
		if etag != "" {
			request.Header.Set("If-None-Match", etag)
		}
		response, err := clientCopy.Do(request)
		if err != nil {
			return geoDataCandidate{}, &geoDataError{"Country source is temporarily unreachable"}
		}
		defer response.Body.Close()
		if response.StatusCode == http.StatusNotModified && etag != "" {
			return geoDataCandidate{NotModified: true, ETag: etag}, nil
		}
		if response.StatusCode != http.StatusOK {
			return geoDataCandidate{}, &geoDataError{fmt.Sprintf("Country source returned HTTP %d", response.StatusCode)}
		}
		if response.ContentLength > geoDataMaxDownload {
			return geoDataCandidate{}, &geoDataError{"Country download exceeds the size limit"}
		}
		content, err := io.ReadAll(io.LimitReader(response.Body, geoDataMaxDownload+1))
		if err != nil || len(content) > geoDataMaxDownload {
			return geoDataCandidate{}, &geoDataError{"Country download was incomplete or oversized"}
		}
		prefixes, err := parseGeoDataText(content)
		if err != nil {
			return geoDataCandidate{}, &geoDataError{"Downloaded country prefixes failed validation"}
		}
		if len(prefixes) < 1000 {
			return geoDataCandidate{}, &geoDataError{"Downloaded country prefix list is unexpectedly small"}
		}
		etag = response.Header.Get("ETag")
		if len(etag) > 1024 {
			etag = ""
		}
		return geoDataCandidate{Prefixes: prefixes, ETag: etag}, nil
	}
}

// Retained as a small boundary test: production's URL is fixed, with no caller-
// supplied URL, credentials, query parameters or alternative hosts.
func validGeoDataSource() bool {
	parsed, err := url.Parse(geoDataSourceURL)
	return err == nil && parsed.Scheme == "https" && parsed.Host == "raw.githubusercontent.com" &&
		parsed.User == nil && parsed.RawQuery == "" && !bytes.ContainsRune([]byte(parsed.Path), '\\')
}
