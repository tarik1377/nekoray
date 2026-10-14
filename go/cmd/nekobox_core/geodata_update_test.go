package main

import (
	"bytes"
	"compress/zlib"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/netip"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing-box/route/rule"
	"github.com/sagernet/sing/common/logger"
	M "github.com/sagernet/sing/common/metadata"
)

func geoTestPrefixes(values ...string) []netip.Prefix {
	var prefixes []netip.Prefix
	for _, value := range values {
		prefixes = append(prefixes, netip.MustParsePrefix(value))
	}
	return prefixes
}

func geoTestSRS(t *testing.T, prefixes []netip.Prefix) []byte {
	t.Helper()
	content, err := encodeGeoData(prefixes)
	if err != nil {
		t.Fatal(err)
	}
	return content
}

func geoTestManager(t *testing.T, enabled bool) *geoDataManager {
	t.Helper()
	root := t.TempDir()
	seed := filepath.Join(root, "geoip-ru-bootstrap.srs")
	if err := os.WriteFile(seed, geoTestSRS(t, geoTestPrefixes("77.88.8.0/24")), 0600); err != nil {
		t.Fatal(err)
	}
	m, err := prepareGeoDataManager(root, seed, geoDataSettings{Enabled: enabled, Present: true})
	if err != nil {
		t.Fatal(err)
	}
	return m
}

func TestGeoDataTextValidationAndCanonicalSRS(t *testing.T) {
	a, err := parseGeoDataText([]byte("\xef\xbb\xbf# publisher\n5.255.255.128/25\n77.88.8.0/24\n5.255.255.0/25\n77.88.8.0/24\n"))
	if err != nil {
		t.Fatal(err)
	}
	b := geoTestPrefixes("77.88.8.0/24", "5.255.255.0/24")
	encoded := geoTestSRS(t, a)
	if !bytes.Equal(encoded, geoTestSRS(t, b)) {
		t.Fatal("country rules depend on source ordering/duplicates")
	}
	decoded, err := decodeGeoData(encoded)
	if err != nil || !bytes.Equal(encoded, geoTestSRS(t, decoded)) {
		t.Fatalf("SRS round-trip failed: %v", err)
	}
	for _, bad := range []string{"", "# empty", "<html>blocked</html>", "5.255.255.0/24\npartial", "0.0.0.0/0", "10.0.0.0/8", "172.16.0.0/12", "127.0.0.1/32", "100.64.0.0/10", "224.0.0.0/4", "::/0", "fc00::/7", "::ffff:5.255.255.0/120", strings.Repeat("x", 257)} {
		if _, err := parseGeoDataText([]byte(bad)); err == nil {
			t.Fatalf("accepted invalid input %q", bad[:min(len(bad), 30)])
		}
	}
	if _, err := parseGeoDataText(bytes.Repeat([]byte{' '}, geoDataMaxDownload+1)); err == nil {
		t.Fatal("accepted oversized text")
	}
}

func geoCompressedBody(t *testing.T, body []byte) []byte {
	t.Helper()
	var out bytes.Buffer
	out.Write([]byte{'S', 'R', 'S', 3})
	z := zlib.NewWriter(&out)
	if _, err := z.Write(body); err != nil {
		t.Fatal(err)
	}
	if err := z.Close(); err != nil {
		t.Fatal(err)
	}
	return out.Bytes()
}

func TestGeoDataBinaryBoundsBeforeAllocation(t *testing.T) {
	valid := geoTestSRS(t, geoTestPrefixes("5.255.255.0/24"))
	for _, content := range [][]byte{nil, []byte("SRS"), []byte("SRS\xffgarbage"), valid[:len(valid)-1], append(append([]byte{}, valid...), 0)} {
		if _, err := decodeGeoData(content); err == nil {
			t.Fatal("accepted malformed/trailing binary")
		}
	}
	// Counts must be rejected before upstream's unbounded make(...) paths.
	var giant bytes.Buffer
	var raw [10]byte
	n := binary.PutUvarint(raw[:], ^uint64(0))
	giant.Write(raw[:n])
	if _, err := decodeGeoData(geoCompressedBody(t, giant.Bytes())); err == nil {
		t.Fatal("accepted giant rule count")
	}
	giant.Reset()
	giant.Write([]byte{1, 0, 6, 1})
	binary.Write(&giant, binary.BigEndian, ^uint64(0))
	if _, err := decodeGeoData(geoCompressedBody(t, giant.Bytes())); err == nil {
		t.Fatal("accepted giant range count")
	}
	if _, err := decodeGeoData(geoCompressedBody(t, bytes.Repeat([]byte{0}, geoDataMaxDecoded+1))); err == nil {
		t.Fatal("accepted decompression bomb")
	}
}

func TestGeoDataOfflineBootstrapRecoveryAndPreservation(t *testing.T) {
	m := geoTestManager(t, false)
	original, err := os.ReadFile(filepath.Join(filepath.Dir(m.directory), "geoip-ru-bootstrap.srs"))
	if err != nil {
		t.Fatal(err)
	}
	current, err := os.ReadFile(m.path("geoip-ru.srs"))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(original, current) {
		t.Fatal("offline bootstrap differs")
	}
	good := geoTestSRS(t, geoTestPrefixes("5.255.255.0/24"))
	os.WriteFile(m.path("geoip-ru.lastgood.srs"), good, 0600)
	os.WriteFile(m.path("geoip-ru.srs"), []byte("damaged"), 0600)
	prepared, err := prepareGeoDataManager(filepath.Dir(m.directory), "", geoDataSettings{})
	if err != nil {
		t.Fatal(err)
	}
	restored, _ := os.ReadFile(prepared.path("geoip-ru.srs"))
	if !bytes.Equal(restored, good) {
		t.Fatal("corrupt cache did not recover last-good")
	}
	unchanged, _ := os.ReadFile(filepath.Join(filepath.Dir(m.directory), "geoip-ru-bootstrap.srs"))
	if !bytes.Equal(unchanged, original) {
		t.Fatal("bootstrap source was rewritten")
	}
	raw, _ := os.ReadFile(m.path("status.json"))
	if bytes.Contains(raw, []byte("0001-")) {
		t.Fatal("zero status time serialized as a successful check")
	}
}

func TestGeoDataCommitRollbackAndUnchanged(t *testing.T) {
	for _, failure := range []string{"backup", "publish", "apply", "none"} {
		t.Run(failure, func(t *testing.T) {
			m := geoTestManager(t, true)
			before, _ := os.ReadFile(m.path("geoip-ru.srs"))
			m.fetch = func(context.Context, string) (geoDataCandidate, error) {
				return geoDataCandidate{Prefixes: geoTestPrefixes("5.255.255.0/24"), ETag: "new"}, nil
			}
			m.atomicWrite = func(path string, content []byte) error {
				if (failure == "backup" && filepath.Base(path) == "geoip-ru.lastgood.srs") || (failure == "publish" && filepath.Base(path) == "geoip-ru.srs") {
					return errors.New("injected write failure")
				}
				return writeGeoDataAtomic(path, content)
			}
			m.waitApplied = func(context.Context, []netip.Prefix) error {
				if failure == "apply" {
					return errors.New("injected reload failure")
				}
				return nil
			}
			err := m.update(context.Background())
			after, _ := os.ReadFile(m.path("geoip-ru.srs"))
			if failure != "none" {
				if err == nil || !bytes.Equal(before, after) {
					t.Fatal("failure destroyed previous data")
				}
				return
			}
			if err != nil || bytes.Equal(before, after) || m.status.State != "updated" || m.status.AppliedAt.IsZero() {
				t.Fatalf("candidate was not confirmed: %v", err)
			}
			writes := 0
			m.atomicWrite = func(string, []byte) error { writes++; return errors.New("unexpected rewrite") }
			if err := m.update(context.Background()); err != nil || writes != 0 || m.status.State != "current" {
				t.Fatalf("identical candidate caused replacement: %v", err)
			}
		})
	}
}

func TestGeoDataManualDisabledThrottleAndDailyBackoff(t *testing.T) {
	m := geoTestManager(t, false)
	now := time.Date(2026, 10, 14, 12, 0, 0, 0, time.UTC)
	m.now = func() time.Time { return now }
	calls := 0
	m.fetch = func(context.Context, string) (geoDataCandidate, error) {
		calls++
		return geoDataCandidate{}, errors.New("do not leak https://user:secret@invalid/body")
	}
	m.tick(context.Background())
	if calls != 0 {
		t.Fatal("disabled schedule fetched without manual request")
	}
	os.WriteFile(m.path("refresh.request"), []byte("b3f99d50-2f7d-4cca-9fac-b1723b0ea181\n"), 0600)
	m.tick(context.Background())
	if calls != 1 || m.status.State != "error" || m.status.NextRetry.Before(now.Add(5*time.Minute)) || strings.Contains(m.status.Error, "secret") {
		t.Fatal("manual/failed check status incorrect")
	}
	os.WriteFile(m.path("refresh.request"), []byte("749b8457-ea41-412b-8d61-96aee2b6bbe5\n"), 0600)
	now = now.Add(29 * time.Second)
	m.tick(context.Background())
	if calls != 1 {
		t.Fatal("manual request bypassed throttle")
	}
	now = now.Add(time.Second)
	m.tick(context.Background())
	if calls != 2 {
		t.Fatal("queued manual request was lost")
	}
	m.settings.Enabled = true
	now = m.status.NextRetry.Add(-time.Second)
	m.tick(context.Background())
	if calls != 2 {
		t.Fatal("failure backoff was ignored")
	}
	now = m.status.NextRetry
	m.tick(context.Background())
	if calls != 3 {
		t.Fatal("scheduled retry did not run")
	}
	m.fetch = func(context.Context, string) (geoDataCandidate, error) {
		calls++
		return geoDataCandidate{NotModified: true}, nil
	}
	now = m.status.NextRetry
	m.tick(context.Background())
	if calls != 4 || m.status.CheckedAt.IsZero() || m.status.Failures != 0 {
		t.Fatal("successful check did not reset backoff")
	}
	now = now.Add(23 * time.Hour)
	m.tick(context.Background())
	if calls != 4 {
		t.Fatal("daily schedule checked too early")
	}
	now = now.Add(time.Hour)
	m.tick(context.Background())
	if calls != 5 {
		t.Fatal("daily schedule did not check")
	}
}

func TestGeoDataSettingsChangesDoNotReplayPreviousProcess(t *testing.T) {
	m := geoTestManager(t, true)
	settings := m.path("settings.json")
	os.WriteFile(settings, []byte(`{"enabled":false}`), 0600)
	m2, err := prepareGeoDataManager(filepath.Dir(m.directory), "", geoDataSettings{Enabled: true, Present: true})
	if err != nil {
		t.Fatal(err)
	}
	m2.readSettingsChange()
	if !m2.settings.Enabled {
		t.Fatal("previous process settings overrode config")
	}
	// Rewriting the same content is a new UI action, distinguished by mtime.
	info, _ := os.Stat(settings)
	os.Chtimes(settings, info.ModTime().Add(time.Second), info.ModTime().Add(time.Second))
	m2.readSettingsChange()
	if m2.settings.Enabled {
		t.Fatal("new identical-content UI action was ignored")
	}
}

type geoTestTransport func(*http.Request) (*http.Response, error)

func (f geoTestTransport) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }

func TestGeoDataBoundedHTTPAndConditionalRefresh(t *testing.T) {
	for _, scenario := range []string{"not-modified", "HTTP-error", "oversized", "truncated", "html", "small", "good", "cancelled"} {
		t.Run(scenario, func(t *testing.T) {
			client := &http.Client{Transport: geoTestTransport(func(request *http.Request) (*http.Response, error) {
				if request.URL.String() != geoDataSourceURL || request.Header.Get("If-None-Match") != "old" {
					t.Fatal("unexpected URL or conditional header")
				}
				response := &http.Response{StatusCode: 200, Header: make(http.Header), Body: io.NopCloser(strings.NewReader("5.255.255.0/24\n"))}
				switch scenario {
				case "not-modified":
					response.StatusCode = 304
				case "HTTP-error":
					response.StatusCode = 502
				case "oversized":
					response.ContentLength = geoDataMaxDownload + 1
				case "truncated":
					response.Body = io.NopCloser(strings.NewReader("5.255.255.0/24\n77.88."))
				case "html":
					response.Body = io.NopCloser(strings.NewReader("<html>blocked</html>"))
				case "good":
					var body strings.Builder
					for i := 0; i < 1500; i++ {
						fmt.Fprintf(&body, "93.%d.%d.0/24\n", i/128, (i%128)*2)
					}
					response.Body = io.NopCloser(strings.NewReader(body.String()))
					response.Header.Set("ETag", "new")
				case "cancelled":
					<-request.Context().Done()
					return nil, request.Context().Err()
				}
				return response, nil
			})}
			ctx, cancel := context.WithCancel(context.Background())
			if scenario == "cancelled" {
				cancel()
			}
			defer cancel()
			candidate, err := fetchGeoDataText(client)(ctx, "old")
			if scenario == "not-modified" {
				if err != nil || !candidate.NotModified {
					t.Fatal("304 rejected")
				}
				return
			}
			if scenario == "good" {
				if err != nil || len(candidate.Prefixes) != 1500 || candidate.ETag != "new" {
					t.Fatalf("valid text not accepted: %v", err)
				}
				return
			}
			if err == nil {
				t.Fatal("invalid response accepted")
			}
		})
	}
	if !validGeoDataSource() {
		t.Fatal("production source URL was broadened")
	}
}

func TestGeoDataLiveBinaryReloadWithoutRestart(t *testing.T) {
	m := geoTestManager(t, true)
	set, err := rule.NewLocalRuleSet(context.Background(), logger.NOP(), option.RuleSet{
		Type: C.RuleSetTypeLocal, Tag: "geoip-ru", Format: C.RuleSetFormatBinary,
		LocalOptions: option.LocalRuleSet{Path: m.path("geoip-ru.srs")},
	})
	if err != nil {
		t.Fatal(err)
	}
	defer set.Close()
	if err := set.StartContext(context.Background(), nil); err != nil {
		t.Fatal(err)
	}
	m.waitApplied = geoDataLiveWaiter(set)
	m.fetch = func(context.Context, string) (geoDataCandidate, error) {
		return geoDataCandidate{Prefixes: geoTestPrefixes("5.255.255.0/24")}, nil
	}
	match := func(address string) bool {
		return set.Match(&adapter.InboundContext{Destination: M.Socksaddr{Addr: netip.MustParseAddr(address), Port: 443}})
	}
	if !match("77.88.8.8") || match("5.255.255.77") {
		t.Fatal("incorrect initial route match")
	}
	var stopped atomic.Bool
	done := make(chan struct{})
	go func() {
		defer close(done)
		for !stopped.Load() {
			match("5.255.255.77")
			set.Metadata()
			_ = set.String()
		}
	}()
	err = m.update(context.Background())
	stopped.Store(true)
	<-done
	if err != nil || !match("5.255.255.77") || match("77.88.8.8") {
		t.Fatalf("live binary rules did not reload: %v", err)
	}
	if m.status.State != "updated" || m.status.AppliedAt.IsZero() {
		t.Fatal("unconfirmed apply status")
	}
}

func TestGeoDataLifecycleCancellationAndConfigBoundary(t *testing.T) {
	for _, input := range []string{`{"geodata_update":true}`, `{"geodata_update":null}`, `{"geodata_update":{"enabled":"yes"}}`} {
		if _, _, err := extractGeoDataSettings([]byte(input)); err == nil {
			t.Fatal("malformed extension accepted")
		}
	}
	clean, settings, err := extractGeoDataSettings([]byte(`{"geodata_update":{"enabled":false},"outbounds":[]}`))
	if err != nil || !settings.Present || settings.Enabled || bytes.Contains(clean, []byte("geodata_update")) {
		t.Fatal("extension boundary incorrect")
	}
	m := geoTestManager(t, true)
	var calls atomic.Int32
	ctx, cancel := context.WithCancel(context.Background())
	m.Start(ctx, func(ctx context.Context, _ string) (geoDataCandidate, error) {
		calls.Add(1)
		<-ctx.Done()
		return geoDataCandidate{}, ctx.Err()
	}, nil)
	cancel()
	select {
	case <-m.done:
	case <-time.After(time.Second):
		t.Fatal("manager did not stop with instance")
	}
	if calls.Load() != 0 {
		t.Fatal("network started before post-start delay")
	}
	raw, _ := os.ReadFile(m.path("status.json"))
	var fields map[string]interface{}
	if json.Unmarshal(raw, &fields) != nil || fields["source"] != geoDataSourceID {
		t.Fatal("status schema invalid")
	}
}

func TestGeoDataBundledReservedAsset(t *testing.T) {
	content, err := os.ReadFile(filepath.Join("..", "..", "..", "res", "geodata", "geoip-ru-bootstrap.srs"))
	if err != nil {
		t.Fatal(err)
	}
	prefixes, err := decodeGeoData(content)
	if err != nil {
		t.Fatal(err)
	}
	if len(prefixes) < 1000 {
		t.Fatal("country bootstrap unexpectedly small")
	}
	found := false
	for _, prefix := range prefixes {
		if prefix.Contains(netip.MustParseAddr("5.255.255.77")) {
			found = true
		}
	}
	if !found {
		t.Fatal("approved fresh bootstrap does not contain the public RU reference prefix")
	}
}

func TestGeoDataUnconfirmedRestoreDoesNotClaimActiveRules(t *testing.T) {
	for _, confirmRestore := range []bool{false, true} {
		t.Run(fmt.Sprint(confirmRestore), func(t *testing.T) {
			m := geoTestManager(t, true)
			before, _ := os.ReadFile(m.path("geoip-ru.srs"))
			m.status.AppliedAt = time.Now().Add(-time.Hour)
			m.fetch = func(context.Context, string) (geoDataCandidate, error) {
				return geoDataCandidate{Prefixes: geoTestPrefixes("5.255.255.0/24")}, nil
			}
			calls := 0
			m.waitApplied = func(context.Context, []netip.Prefix) error {
				calls++
				if confirmRestore && calls == 2 {
					return nil
				}
				return errors.New("reload not confirmed")
			}
			m.tick(context.Background())
			after, _ := os.ReadFile(m.path("geoip-ru.srs"))
			if !bytes.Equal(before, after) {
				t.Fatal("last-good disk content lost")
			}
			if confirmRestore {
				if m.status.State != "error" || !strings.Contains(m.status.Error, "were restored") {
					t.Fatal("confirmed restore was not reported correctly")
				}
			} else {
				if m.status.State != "unconfirmed" || !m.status.AppliedAt.IsZero() || strings.Contains(m.status.Error, "were restored") {
					t.Fatal("unconfirmed active routing reported as restored")
				}
			}
		})
	}
}

func TestGeoDataSameCandidateRepairsIncompleteRollbackState(t *testing.T) {
	m := geoTestManager(t, true)
	next := geoTestPrefixes("5.255.255.0/24")
	m.fetch = func(context.Context, string) (geoDataCandidate, error) {
		return geoDataCandidate{Prefixes: next, ETag: "candidate"}, nil
	}
	publishes := 0
	m.atomicWrite = func(path string, content []byte) error {
		if filepath.Base(path) == "geoip-ru.srs" {
			publishes++
			if publishes == 2 {
				return errors.New("restore write failed")
			}
		}
		return writeGeoDataAtomic(path, content)
	}
	m.waitApplied = func(context.Context, []netip.Prefix) error { return errors.New("live apply not confirmed") }
	if err := m.update(context.Background()); err == nil {
		t.Fatal("incomplete rollback was accepted")
	}
	wantedHash := geoDataHash(geoTestSRS(t, next))
	if m.status.SHA256 == wantedHash || !m.status.AppliedAt.IsZero() {
		t.Fatal("failed transaction was reported applied")
	}
	m.atomicWrite = writeGeoDataAtomic
	confirmed := 0
	m.waitApplied = func(context.Context, []netip.Prefix) error { confirmed++; return nil }
	if err := m.update(context.Background()); err != nil {
		t.Fatal(err)
	}
	if confirmed != 1 || m.status.SHA256 != wantedHash || m.status.PrefixCount != len(next) || m.status.AppliedAt.IsZero() {
		t.Fatal("same-file recovery did not synchronize live/status identity")
	}
	m.fetch = func(context.Context, string) (geoDataCandidate, error) {
		return geoDataCandidate{NotModified: true}, nil
	}
	if err := m.update(context.Background()); err != nil || confirmed != 2 {
		t.Fatalf("304 did not confirm recovered active rules: %v", err)
	}
	m.waitApplied = func(context.Context, []netip.Prefix) error { return errors.New("active snapshot differs") }
	if err := m.update(context.Background()); err == nil || !m.status.AppliedAt.IsZero() {
		t.Fatal("304 blessed unconfirmed live data")
	}
}

func TestGeoDataBoundedShutdownBlocksNewInstanceUntilWriterStops(t *testing.T) {
	done := make(chan struct{})
	cancelled := false
	start := time.Now()
	cancelGeoDataAndWait(func() { cancelled = true }, done, time.Millisecond)
	if !cancelled || time.Since(start) > time.Second {
		t.Fatal("stop was not bounded/cancelled")
	}
	if previousGeoDataStopped() {
		t.Fatal("new instance could race timed-out old writer")
	}
	instance, cancel, err := nekoCreate([]byte(`{}`))
	if err == nil || instance != nil || cancel != nil {
		t.Fatal("core startup did not reject pending old writer")
	}
	close(done)
	if !previousGeoDataStopped() {
		t.Fatal("finished old writer still prevents startup")
	}
}

func TestGeoDataTransformOfflineFallbackAndExplicitOwnership(t *testing.T) {
	for _, cacheBlocked := range []bool{false, true} {
		t.Run(fmt.Sprint(cacheBlocked), func(t *testing.T) {
			t.Chdir(t.TempDir())
			seed := geoTestSRS(t, geoTestPrefixes("77.88.8.0/24"))
			if err := os.WriteFile("geoip-ru-bootstrap.srs", seed, 0600); err != nil {
				t.Fatal(err)
			}
			if cacheBlocked {
				if err := os.WriteFile(".geodata", []byte("not a directory"), 0600); err != nil {
					t.Fatal(err)
				}
			}
			out := mustTransform(t, map[string]interface{}{"route": map[string]interface{}{"rules": []interface{}{map[string]interface{}{"geoip": []interface{}{"ru"}, "outbound": "direct"}}}})
			sets := out["route"].(map[string]interface{})["rule_set"].([]interface{})
			set := sets[0].(map[string]interface{})
			wanted := geoDataManagedPath
			if cacheBlocked {
				wanted = "geoip-ru-bootstrap.srs"
			}
			if set["type"] != "local" || set["path"] != wanted || set["url"] != nil {
				t.Fatalf("offline bootstrap was lost: %#v", set)
			}
		})
	}
	t.Run("explicit-custom-rule-set", func(t *testing.T) {
		t.Chdir(t.TempDir())
		custom := map[string]interface{}{"type": "local", "tag": "geoip-ru", "format": "binary", "path": "user-custom.srs"}
		out := mustTransform(t, map[string]interface{}{"route": map[string]interface{}{"rules": []interface{}{map[string]interface{}{"geoip": []interface{}{"ru"}, "outbound": "direct"}}, "rule_set": []interface{}{custom}}})
		sets := out["route"].(map[string]interface{})["rule_set"].([]interface{})
		if len(sets) != 1 || sets[0].(map[string]interface{})["path"] != "user-custom.srs" {
			t.Fatal("explicit rule-set was replaced/duplicated")
		}
		if _, err := os.Stat(".geodata"); !errors.Is(err, os.ErrNotExist) {
			t.Fatal("custom rule-set caused managed cache creation")
		}
	})
}
