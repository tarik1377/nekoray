package grpc_server

import (
	"context"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	releaseauth "greenrhythm_release"
	"grpc_server/gen"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

type outageTransport func(*http.Request) (*http.Response, error)

func (f outageTransport) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }
func outageNetwork(t *testing.T, client *http.Client) updateNetwork {
	t.Helper()
	options := defaultUpdateNetwork(client, nil)
	options.verify = func(m *releaseauth.Manifest, platform string) error {
		return releaseauth.VerifyWithKeys(m, platform, outageKeys())
	}
	updateMu.Lock()
	verifiedUpdate = nil
	update_download_url, update_expected_sha = "", ""
	updateMu.Unlock()
	options.archivePath = filepath.Join(t.TempDir(), "update.zip")
	options.checkTimeout = 40 * time.Millisecond
	options.headerTimeout = 100 * time.Millisecond
	options.idleTimeout = 150 * time.Millisecond
	return options
}
func outageResponse(r *http.Request, status int, body string) *http.Response {
	return &http.Response{StatusCode: status, Header: make(http.Header), Body: io.NopCloser(strings.NewReader(body)), Request: r}
}

type updateCandidate struct{ downloadURL, sha256, body string }
type updateState struct{}

func outageKeys() map[string]string {
	private := ed25519.NewKeyFromSeed(make([]byte, ed25519.SeedSize))
	return map[string]string{"outage-fixture": base64.StdEncoding.EncodeToString(private.Public().(ed25519.PublicKey))}
}
func outageManifest(body string) *releaseauth.Manifest {
	sum := sha256.Sum256([]byte(body))
	m := &releaseauth.Manifest{VersionCode: 990000, Version: "99.0.0", URL: "https://verdantvibe.ru/downloads/fixture.zip",
		SHA256: hex.EncodeToString(sum[:]), SizeBytes: int64(len(body)), Platform: manifestPlatform(), Algorithm: releaseauth.Algorithm, KeyID: "outage-fixture"}
	payload, err := releaseauth.Payload(m)
	if err != nil {
		panic(err)
	}
	private := ed25519.NewKeyFromSeed(make([]byte, ed25519.SeedSize))
	m.Signature = base64.StdEncoding.EncodeToString(ed25519.Sign(private, payload))
	return m
}
func outageCandidate(body string) updateCandidate {
	m := outageManifest(body)
	return updateCandidate{downloadURL: m.URL, sha256: m.SHA256, body: body}
}
func (s *updateState) store(candidate updateCandidate) {
	updateMu.Lock()
	defer updateMu.Unlock()
	update_download_url, update_expected_sha = candidate.downloadURL, candidate.sha256
	verifiedUpdate = outageManifest(candidate.body)
}
func (s *updateState) snapshot() updateCandidate {
	updateMu.Lock()
	defer updateMu.Unlock()
	return updateCandidate{downloadURL: update_download_url, sha256: update_expected_sha}
}
func checkedManifest() string {
	body, err := json.Marshal(outageManifest("fixture package"))
	if err != nil {
		panic(err)
	}
	return string(body)
}
func outageDownload(t *testing.T, options updateNetwork) *gen.UpdateResp {
	t.Helper()
	response, err := (&BaseServer{}).updateWithNetwork(context.Background(), &gen.UpdateReq{Action: gen.UpdateAction_Download}, options)
	if err != nil {
		t.Fatalf("RPC method returned unexpected error: %v", err)
	}
	return response
}
func TestUpdateCheckOutageIsAnErrorWithoutFilesystemChanges(t *testing.T) {
	cases := []struct {
		name      string
		transport outageTransport
	}{
		{"DNS", func(r *http.Request) (*http.Response, error) {
			return nil, &net.DNSError{Name: "fixture.invalid", IsNotFound: true, Err: "fixture DNS unavailable"}
		}},
		{"refused", func(r *http.Request) (*http.Response, error) { return nil, errors.New("fixture connection refused") }},
		{"TLS", func(r *http.Request) (*http.Response, error) { return nil, errors.New("fixture TLS handshake failure") }},
		{"silent", func(r *http.Request) (*http.Response, error) { <-r.Context().Done(); return nil, r.Context().Err() }},
		{"503", func(r *http.Request) (*http.Response, error) { return outageResponse(r, 503, "unavailable"), nil }},
		{"HTML", func(r *http.Request) (*http.Response, error) {
			return outageResponse(r, 200, "<html>unavailable</html>"), nil
		}},
		{"truncated_JSON", func(r *http.Request) (*http.Response, error) { return outageResponse(r, 200, `{"version":`), nil }},
	}
	for _, fixture := range cases {
		t.Run(fixture.name, func(t *testing.T) {
			network := outageNetwork(t, &http.Client{Transport: fixture.transport})
			state := &updateState{}
			result, err := (&BaseServer{}).updateWithNetwork(context.Background(), &gen.UpdateReq{Action: gen.UpdateAction_Check}, network)
			if err != nil || result.Error == "" || result.DownloadUrl != "" {
				t.Fatalf("outage was mistaken for a successful check: %v %v", result, err)
			}
			if state.snapshot().downloadURL != "" {
				t.Fatal("failed check created a download candidate")
			}
			if _, err := os.Stat(network.archivePath); !os.IsNotExist(err) {
				t.Fatal("failed check touched the archive")
			}
		})
	}
}
func TestUpdateFallbackGetsFreshCheckContext(t *testing.T) {
	primary := &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) { <-r.Context().Done(); return nil, r.Context().Err() })}
	network := outageNetwork(t, primary)
	var fallbackCalls atomic.Int32
	network.fallback = &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) {
		fallbackCalls.Add(1)
		if r.Context().Err() != nil {
			return nil, r.Context().Err()
		}
		return outageResponse(r, 200, checkedManifest()), nil
	})}
	state := &updateState{}
	result, err := (&BaseServer{}).updateWithNetwork(context.Background(), &gen.UpdateReq{Action: gen.UpdateAction_Check}, network)
	if err != nil || result.Error != "" || result.DownloadUrl == "" || fallbackCalls.Load() != 1 {
		t.Fatalf("healthy fallback did not recover: %v %v", result, err)
	}
	if state.snapshot().downloadURL != result.DownloadUrl {
		t.Fatal("recovered manifest was not cached")
	}
}
func TestUpdateParentCancellationSkipsFallback(t *testing.T) {
	network := outageNetwork(t, &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) { return nil, r.Context().Err() })})
	var attempts atomic.Int32
	network.fallback = &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) {
		attempts.Add(1)
		return outageResponse(r, 200, checkedManifest()), nil
	})}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	result, _ := (&BaseServer{}).updateWithNetwork(ctx, &gen.UpdateReq{Action: gen.UpdateAction_Check}, network)
	if result.Error == "" || attempts.Load() != 0 {
		t.Fatal("cancelled caller started a fallback request")
	}
}
func TestUpdateDownloadSilentHeadersAreBounded(t *testing.T) {
	var accepted atomic.Bool
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { accepted.Store(true); <-r.Context().Done() }))
	defer server.Close()
	client := server.Client()
	client.Transport = redirectTo(server.URL)
	network := outageNetwork(t, client)
	state := &updateState{}
	state.store(outageCandidate("fixture package"))
	started := time.Now()
	result := outageDownload(t, network)
	if !accepted.Load() || !strings.Contains(result.Error, "не ответил") || time.Since(started) > time.Second {
		t.Fatalf("silent headers did not fail on their own deadline: %v", result)
	}
	if _, err := os.Stat(network.archivePath); !os.IsNotExist(err) {
		t.Fatal("silent headers created an archive")
	}
}
func TestUpdateDownloadFallbackHasItsOwnHeaderBudget(t *testing.T) {
	body := "fixture package"
	stalled := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { <-r.Context().Done() }))
	defer stalled.Close()
	healthy := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, body) }))
	defer healthy.Close()
	client := stalled.Client()
	client.Transport = redirectTo(stalled.URL)
	fallback := healthy.Client()
	fallback.Transport = redirectTo(healthy.URL)
	network := outageNetwork(t, client)
	network.fallback = fallback
	state := &updateState{}
	state.store(outageCandidate(body))
	result := outageDownload(t, network)
	got, err := os.ReadFile(network.archivePath)
	if result.Error != "" || err != nil || string(got) != body {
		t.Fatalf("download fallback failed: %v %v", result, err)
	}
}
func TestUpdateSlowBodyOutlivesHeaderBudgetAndClientTimeout(t *testing.T) {
	body := strings.Repeat("x", 8)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(200)
		w.(http.Flusher).Flush()
		for range body {
			time.Sleep(40 * time.Millisecond)
			io.WriteString(w, "x")
			w.(http.Flusher).Flush()
		}
	}))
	defer server.Close()
	client := server.Client()
	client.Transport = redirectTo(server.URL)
	client.Timeout = 10 * time.Millisecond
	network := outageNetwork(t, client)
	state := &updateState{}
	state.store(outageCandidate(body))
	started := time.Now()
	result := outageDownload(t, network)
	if result.Error != "" || time.Since(started) <= network.headerTimeout {
		t.Fatalf("valid slow body was aborted: %v", result)
	}
	if client.Timeout != 10*time.Millisecond {
		t.Fatal("shared client's timeout was modified")
	}
	got, _ := os.ReadFile(network.archivePath)
	manifestBytes, _ := os.ReadFile(network.archivePath + ".manifest.json")
	var signed releaseauth.Manifest
	_ = json.Unmarshal(manifestBytes, &signed)
	digest := []byte(signed.SHA256)
	if string(got) != body || string(digest) != state.snapshot().sha256 {
		t.Fatal("valid slow body/checksum not preserved")
	}
}
func TestUpdateStalledBodyIsRemovedAndCanRetry(t *testing.T) {
	body := "fixture package"
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "fixture")
		w.(http.Flusher).Flush()
		<-r.Context().Done()
	}))
	defer server.Close()
	client := server.Client()
	client.Transport = redirectTo(server.URL)
	network := outageNetwork(t, client)
	state := &updateState{}
	state.store(outageCandidate(body))
	result := outageDownload(t, network)
	if !strings.Contains(result.Error, "перестал отвечать") {
		t.Fatalf("stalled body not recognized: %v", result)
	}
	if _, err := os.Stat(network.archivePath); !os.IsNotExist(err) {
		t.Fatal("partial archive remains after body failure")
	}
	network.direct = &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) { return outageResponse(r, 200, body), nil })}
	if retry := outageDownload(t, network); retry.Error != "" {
		t.Fatalf("failure blocked retry: %v", retry)
	}
}
func TestUpdateDownloadFailuresNeverProduceAnInstallableArchive(t *testing.T) {
	for _, status := range []int{403, 404, 503} {
		t.Run(http.StatusText(status), func(t *testing.T) {
			client := &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) { return outageResponse(r, status, "fixture error"), nil })}
			network := outageNetwork(t, client)
			state := &updateState{}
			state.store(outageCandidate("package"))
			result := outageDownload(t, network)
			if result.Error == "" {
				t.Fatal("HTTP failure was accepted")
			}
			if _, err := os.Stat(network.archivePath); !os.IsNotExist(err) {
				t.Fatal("HTTP failure created an archive")
			}
		})
	}
}
func TestUpdateDownloadUsesOneCandidateAndRejectsOverlap(t *testing.T) {
	body := "first fixture package"
	entered := make(chan struct{})
	release := make(chan struct{})
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(200)
		w.(http.Flusher).Flush()
		close(entered)
		<-release
		io.WriteString(w, body)
	}))
	defer server.Close()
	client := server.Client()
	client.Transport = redirectTo(server.URL)
	network := outageNetwork(t, client)
	network.idleTimeout = time.Second
	state := &updateState{}
	first := outageCandidate(body)
	state.store(first)
	done := make(chan *gen.UpdateResp, 1)
	go func() {
		result, _ := (&BaseServer{}).updateWithNetwork(context.Background(), &gen.UpdateReq{Action: gen.UpdateAction_Download}, network)
		done <- result
	}()
	<-entered
	checkDuringDownload, _ := (&BaseServer{}).updateWithNetwork(context.Background(), &gen.UpdateReq{Action: gen.UpdateAction_Check}, network)
	if !strings.Contains(checkDuringDownload.Error, "уже выполняется") {
		t.Fatal("a check was admitted while a download held the verified candidate")
	}
	overlap := outageDownload(t, network)
	close(release)
	result := <-done
	if !strings.Contains(overlap.Error, "уже выполняется") {
		t.Fatalf("overlapping download was admitted: %v", overlap)
	}
	if result.Error != "" {
		t.Fatalf("background manifest replacement invalidated the in-flight package: %v", result)
	}
	manifestBytes, _ := os.ReadFile(network.archivePath + ".manifest.json")
	var signed releaseauth.Manifest
	_ = json.Unmarshal(manifestBytes, &signed)
	digest := []byte(signed.SHA256)
	if string(digest) != first.sha256 {
		t.Fatal("archive checked against another release's digest")
	}
}
func TestUpdateUnknownActionAndMissingCandidateFailSafely(t *testing.T) {
	network := outageNetwork(t, &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) {
		t.Error("unexpected network request")
		return nil, errors.New("unexpected")
	})})

	for _, request := range []*gen.UpdateReq{nil, {Action: gen.UpdateAction(99)}, {Action: gen.UpdateAction_Download}} {
		result, _ := (&BaseServer{}).updateWithNetwork(context.Background(), request, network)
		if result.Error == "" {
			t.Fatal("invalid state was accepted")
		}
	}
}

func TestUpdateProductionVerifierRejectsUnsignedAndUnknownKeys(t *testing.T) {
	unsigned := outageManifest("fixture package")
	unsigned.Signature = ""
	unsignedBytes, _ := json.Marshal(unsigned)
	for _, body := range []string{string(unsignedBytes), checkedManifest()} {
		network := outageNetwork(t, &http.Client{Transport: outageTransport(func(r *http.Request) (*http.Response, error) { return outageResponse(r, 200, body), nil })})
		network.verify = releaseauth.Verify
		result, _ := (&BaseServer{}).updateWithNetwork(context.Background(), &gen.UpdateReq{Action: gen.UpdateAction_Check}, network)
		if result.Error == "" || verifiedUpdate != nil {
			t.Fatal("production verifier accepted unsigned or unknown-key test data")
		}
	}
}
