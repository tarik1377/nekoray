package sitebook

import (
	"context"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestSiteBrowserParentExitHelper(t *testing.T) {
	ready := os.Getenv("GREENRHYTHM_BROWSER_PARENT_READY")
	if ready == "" {
		return
	}
	_, _, err := startBrowser(context.Background(), FindBrowser(), "http://127.0.0.1:9")
	if err != nil {
		t.Fatal(err)
	}
	if os.WriteFile(ready, []byte("ready"), 0600) != nil {
		os.Exit(3)
	}
	os.Exit(0) // Deliberately bypass normal cleanup, as a crashed core would.
}
func TestBrowserGuardianCleansAfterParentExit(t *testing.T) {
	if FindBrowser() == "" {
		t.Skip("Chromium browser not installed")
	}
	directory := t.TempDir()
	for _, key := range []string{"TEMP", "TMP", "TMPDIR"} {
		t.Setenv(key, directory)
	}
	ready := filepath.Join(directory, "ready")
	command := exec.Command(os.Args[0], "-test.run=^TestSiteBrowserParentExitHelper$")
	prepareBrowser(command)
	command.Env = append(os.Environ(), "GREENRHYTHM_BROWSER_PARENT_READY="+ready)
	if result, err := command.CombinedOutput(); err != nil {
		t.Fatalf("owned parent failed: %v %s", err, result)
	}
	if _, err := os.Stat(ready); err != nil {
		t.Fatal("parent never started browser")
	}
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		matches, _ := filepath.Glob(filepath.Join(directory, "greenrhythm-site-scan-*"))
		if len(matches) == 0 {
			return
		}
		time.Sleep(50 * time.Millisecond)
	}
	t.Fatal("guardian left a browser profile after its parent exited")
}

func TestRealBrowserCapturesJSXHRFramesAndCleansItsProfile(t *testing.T) {
	browser := FindBrowser()
	if browser == "" {
		t.Skip("Chromium browser not installed")
	}
	privateTemp := t.TempDir()
	for _, key := range []string{"TEMP", "TMP", "TMPDIR"} {
		t.Setenv(key, privateTemp)
	}
	sentinel := exec.Command(os.Args[0], "-test.run=TestSiteBrowserSentinelHelper")
	sentinel.Env = append(os.Environ(), "GREENRHYTHM_SENTINEL_HELPER=1")
	if err := sentinel.Start(); err != nil {
		t.Fatal(err)
	}
	sentinelDone := make(chan error, 1)
	go func() { sentinelDone <- sentinel.Wait() }()
	defer func() {
		sentinel.Process.Kill()
		select {
		case <-sentinelDone:
		case <-time.After(time.Second):
		}
	}()
	handler := func(vpn bool) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Access-Control-Allow-Origin", "*")
			switch r.URL.Path {
			case "/":
				w.Header().Set("Content-Type", "text/html")
				fmt.Fprint(w, `<html><head><title>Owned fixture</title><link rel="stylesheet" href="http://assets.example.test:8080/style.css"></head><body><iframe src="http://frame.example.test:8080/frame"></iframe><script>fetch('http://api.example.test:8080/data').catch(()=>{});setTimeout(()=>{let s=document.createElement('script');s.src='http://late.example.test:8080/late.js';document.body.appendChild(s)},500)</script></body></html>`)
			case "/style.css":
				w.Header().Set("Content-Type", "text/css")
				io.WriteString(w, "body{color:green}")
			case "/data":
				w.Header().Set("Content-Type", "application/json")
				if !vpn {
					w.WriteHeader(503)
				}
				io.WriteString(w, `{"ok":true}`)
			case "/frame":
				w.Header().Set("Content-Type", "text/html")
				io.WriteString(w, "<html>frame</html>")
			case "/late.js":
				w.Header().Set("Content-Type", "application/javascript")
				io.WriteString(w, "window.fixtureLoaded=true")
			default:
				w.WriteHeader(404)
			}
		})
	}
	direct, proxy := httptest.NewServer(handler(false)), httptest.NewServer(handler(true))
	defer direct.Close()
	defer proxy.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 40*time.Second)
	defer cancel()
	report, err := Discover(ctx, browser, "http://page.example.test:8080/?token=never-keep", func(ctx context.Context, path, network, address string) (net.Conn, error) {
		target := direct.Listener.Addr().String()
		if path == "proxy" {
			target = proxy.Listener.Addr().String()
		}
		var dialer net.Dialer
		return dialer.DialContext(ctx, network, target)
	}, func(context.Context, string) ([]netip.Addr, error) {
		return []netip.Addr{netip.MustParseAddr("203.0.113.9")}, nil
	}, nil, 6*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	endpoints := map[string]Endpoint{}
	for _, entry := range report.Endpoints {
		endpoints[entry.Origin] = entry
		if strings.Contains(entry.Origin, "token") || strings.Contains(entry.Origin, "/data") {
			t.Fatal("path/query persisted", entry.Origin)
		}
	}
	if report.Recommendation != "proxy" {
		t.Fatal("root 200 hid critical API failure", report.Recommendation)
	}
	for _, host := range []string{"page", "assets", "frame", "api", "late"} {
		key := "http://" + host + ".example.test:8080"
		if _, ok := endpoints[key]; !ok {
			t.Fatalf("missing %s: %+v", key, report)
		}
	}
	if !report.DirectOK || !report.ProxyOK || report.URL != "http://page.example.test:8080" {
		t.Fatal("public root loads not recognized", report)
	}
	if Choose(endpoints["http://api.example.test:8080"].Direct, endpoints["http://api.example.test:8080"].Proxy) != "proxy" {
		t.Fatal("dependent API path not compared", report)
	}
	select {
	case <-sentinelDone:
		t.Fatal("cleanup killed unrelated process")
	default:
	}
	files, _ := os.ReadDir(privateTemp)
	for _, file := range files {
		if strings.HasPrefix(file.Name(), "greenrhythm-site-scan-") {
			t.Fatal("temporary browser profile retained", file.Name())
		}
	}
}
func TestSiteBrowserSentinelHelper(t *testing.T) {
	if os.Getenv("GREENRHYTHM_SENTINEL_HELPER") != "1" {
		return
	}
	time.Sleep(time.Minute)
}
