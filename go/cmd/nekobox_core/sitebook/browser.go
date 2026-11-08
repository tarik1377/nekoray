package sitebook

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/coder/websocket"
)

func FindBrowser() string {
	var candidates []string
	if runtime.GOOS == "windows" {
		for _, root := range []string{os.Getenv("ProgramFiles"), os.Getenv("ProgramFiles(x86)"), os.Getenv("LOCALAPPDATA")} {
			if root != "" {
				for _, path := range []string{"Microsoft/Edge/Application/msedge.exe", "Google/Chrome/Application/chrome.exe", "Yandex/YandexBrowser/Application/browser.exe"} {
					candidates = append(candidates, filepath.Join(root, path))
				}
			}
		}
	} else if runtime.GOOS == "darwin" {
		candidates = []string{"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge", "/Applications/Chromium.app/Contents/MacOS/Chromium", "/Applications/Yandex.app/Contents/MacOS/Yandex"}
	} else {
		candidates = []string{"/usr/bin/google-chrome", "/usr/bin/google-chrome-stable", "/usr/bin/chromium", "/usr/bin/chromium-browser"}
	}
	for _, path := range candidates {
		if info, err := os.Stat(path); err == nil && !info.IsDir() {
			return path
		}
	}
	return ""
}

type cdpMessage struct {
	ID      int64           `json:"id"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params"`
	Result  json.RawMessage `json:"result"`
	Error   json.RawMessage `json:"error"`
	Session string          `json:"sessionId,omitempty"`
}
type cdp struct {
	conn        *websocket.Conn
	ctx         context.Context
	cancel      context.CancelFunc
	next        atomic.Int64
	mu, sending sync.Mutex
	pending     map[int64]chan cdpMessage
	events      chan cdpMessage
	overflow    atomic.Bool
}

func newCDP(parent context.Context, address string) (*cdp, error) {
	ctx, cancel := context.WithCancel(parent)
	conn, _, err := websocket.Dial(ctx, address, &websocket.DialOptions{HTTPClient: &http.Client{Transport: &http.Transport{Proxy: nil}, Timeout: 2 * time.Second}})
	if err != nil {
		cancel()
		return nil, errors.New("browser protocol connection failed")
	}
	conn.SetReadLimit(1 << 20)
	c := &cdp{conn: conn, ctx: ctx, cancel: cancel, pending: map[int64]chan cdpMessage{}, events: make(chan cdpMessage, 1024)}
	go func() {
		defer cancel()
		for {
			_, data, err := conn.Read(ctx)
			if err != nil {
				return
			}
			var message cdpMessage
			if json.Unmarshal(data, &message) != nil {
				continue
			}
			if message.ID != 0 {
				c.mu.Lock()
				ch := c.pending[message.ID]
				c.mu.Unlock()
				if ch != nil {
					select {
					case ch <- message:
					default:
					}
				}
			} else {
				select {
				case c.events <- message:
				default:
					c.overflow.Store(true)
				}
			}
		}
	}()
	return c, nil
}
func (c *cdp) close() { c.cancel(); c.conn.CloseNow() }
func (c *cdp) call(method string, params any, session string) (json.RawMessage, error) {
	timeout := 2 * time.Second
	if method == "Page.navigate" {
		timeout = 15 * time.Second
	}
	ctx, cancel := context.WithTimeout(c.ctx, timeout)
	defer cancel()
	id := c.next.Add(1)
	ch := make(chan cdpMessage, 1)
	c.mu.Lock()
	c.pending[id] = ch
	c.mu.Unlock()
	defer func() { c.mu.Lock(); delete(c.pending, id); c.mu.Unlock() }()
	command := map[string]any{"id": id, "method": method, "params": params}
	if session != "" {
		command["sessionId"] = session
	}
	data, _ := json.Marshal(command)
	c.sending.Lock()
	err := c.conn.Write(ctx, websocket.MessageText, data)
	c.sending.Unlock()
	if err != nil {
		return nil, err
	}
	select {
	case result := <-ch:
		if len(result.Error) > 0 {
			return nil, errors.New("browser protocol command unavailable")
		}
		return result.Result, nil
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

type Capture struct {
	Endpoints  map[string]*Endpoint
	Loaded     bool
	Partial    bool
	Title      string
	root       string
	requests   map[string]string
	main       map[string]bool
	mainFailed bool
	mainStatus int
	types      map[string]map[string]bool
}

func newCapture(origin string) *Capture {
	return &Capture{Endpoints: map[string]*Endpoint{}, root: origin, requests: map[string]string{}, main: map[string]bool{}, types: map[string]map[string]bool{}}
}
func (c *Capture) event(message cdpMessage, rootFrame string) {
	key := message.Session + ":"
	switch message.Method {
	case "Network.requestWillBeSent":
		var p struct {
			RequestID, Type, FrameID string
			Request                  struct{ URL string }
			RedirectResponse         struct {
				URL    string
				Status int
			}
		}
		if json.Unmarshal(message.Params, &p) != nil {
			return
		}
		if p.RedirectResponse.URL != "" {
			c.response(p.RedirectResponse.URL, p.RedirectResponse.Status, false)
		}
		_, origin, err := Normalize(p.Request.URL)
		if err != nil {
			return
		}
		if len(c.Endpoints) >= 128 && c.Endpoints[origin] == nil {
			c.Partial = true
			return
		}
		if c.Endpoints[origin] == nil {
			c.Endpoints[origin] = &Endpoint{Origin: origin}
			c.types[origin] = map[string]bool{}
		}
		e := c.Endpoints[origin]
		e.Direct.Requests++
		c.types[origin][p.Type] = true
		c.requests[key+p.RequestID] = origin
		if p.Type == "Document" && p.FrameID == rootFrame && message.Session == "" {
			c.main[key+p.RequestID] = true
			e.Direct.Document = true
		}
	case "Network.responseReceived":
		var p struct {
			RequestID string
			Response  struct {
				URL    string
				Status int
			}
		}
		if json.Unmarshal(message.Params, &p) != nil {
			return
		}
		c.response(p.Response.URL, p.Response.Status, c.main[key+p.RequestID])
	case "Network.loadingFailed":
		var p struct{ RequestID string }
		if json.Unmarshal(message.Params, &p) != nil {
			return
		}
		if e := c.Endpoints[c.requests[key+p.RequestID]]; e != nil {
			e.Direct.Failed++
		}
		if c.main[key+p.RequestID] {
			c.mainFailed = true
		}
	case "Network.webSocketCreated":
		var p struct{ URL string }
		if json.Unmarshal(message.Params, &p) != nil {
			return
		}
		_, origin, err := Normalize(p.URL)
		if err == nil && c.Endpoints[origin] == nil && len(c.Endpoints) < 128 {
			c.Endpoints[origin] = &Endpoint{Origin: origin}
			c.types[origin] = map[string]bool{"WebSocket": true}
		}
	case "Page.loadEventFired":
		if message.Session == "" {
			c.Loaded = true
		}
	}
}
func (c *Capture) response(raw string, status int, main bool) {
	_, origin, err := Normalize(raw)
	if err != nil {
		return
	}
	e := c.Endpoints[origin]
	if e == nil {
		return
	}
	e.Direct.Status = status
	if status >= 200 && status < 400 {
		e.Direct.OK++
	} else if status >= 400 {
		e.Direct.Failed++
	}
	if main {
		c.mainStatus = status
	}
}
func (c *Capture) success() bool {
	return c.Loaded && !c.mainFailed && c.mainStatus >= 200 && c.mainStatus < 400
}

func launchBrowser(ctx context.Context, browser, proxy string) (string, func(), error) {
	directory, err := os.MkdirTemp("", "greenrhythm-site-scan-")
	if err != nil {
		return "", nil, err
	}
	_ = os.Chmod(directory, 0700)
	cleanupDir := func() {
		for i := 0; i < 4; i++ {
			if os.RemoveAll(directory) == nil {
				return
			}
			time.Sleep(100 * time.Millisecond)
		}
	}
	args := []string{"--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check", "--disable-extensions", "--disable-background-networking", "--disable-component-update", "--disable-sync", "--disable-default-apps", "--mute-audio", "--disable-quic", "--renderer-process-limit=2", "--disk-cache-size=1", "--media-cache-size=1", "--remote-debugging-address=127.0.0.1", "--remote-debugging-port=0", "--user-data-dir=" + directory, "--proxy-server=" + proxy, "--proxy-bypass-list=<-loopback>", "about:blank"}
	command := exec.CommandContext(ctx, browser, args...)
	prepareBrowser(command)
	// Nil connects both streams to the null device, without copy goroutines
	// waiting on handles inherited by renderer descendants during shutdown.
	command.Stdout = nil
	command.Stderr = nil
	for _, pair := range os.Environ() {
		name := strings.ToLower(strings.SplitN(pair, "=", 2)[0])
		if name != "http_proxy" && name != "https_proxy" && name != "all_proxy" && name != "no_proxy" {
			command.Env = append(command.Env, pair)
		}
	}
	if err = command.Start(); err != nil {
		cleanupDir()
		return "", nil, errors.New("browser launch failed")
	}
	contain, err := containBrowser(command)
	if err != nil {
		command.Process.Kill()
		command.Wait()
		cleanupDir()
		return "", nil, errors.New("cannot isolate browser process tree")
	}
	cleanup := func() { contain(); command.Process.Kill(); command.Wait(); cleanupDir() }
	port := 0
	deadline := time.Now().Add(7 * time.Second)
	for time.Now().Before(deadline) {
		data, e := os.ReadFile(filepath.Join(directory, "DevToolsActivePort"))
		if e == nil {
			lines := strings.Split(string(data), "\n")
			if len(lines) > 0 {
				port, _ = strconv.Atoi(strings.TrimSpace(lines[0]))
				if port > 0 && port <= 65535 {
					break
				}
			}
		}
		select {
		case <-ctx.Done():
			cleanup()
			return "", nil, ctx.Err()
		case <-time.After(50 * time.Millisecond):
		}
	}
	if port == 0 {
		cleanup()
		return "", nil, errors.New("browser did not expose its private debugging endpoint")
	}
	client := &http.Client{Transport: &http.Transport{Proxy: nil}, Timeout: 2 * time.Second}
	response, err := client.Get("http://127.0.0.1:" + strconv.Itoa(port) + "/json/list")
	if err != nil {
		cleanup()
		return "", nil, errors.New("browser target unavailable")
	}
	defer response.Body.Close()
	var targets []struct {
		Type   string `json:"type"`
		Socket string `json:"webSocketDebuggerUrl"`
	}
	if json.NewDecoder(io.LimitReader(response.Body, 1<<20)).Decode(&targets) != nil {
		cleanup()
		return "", nil, errors.New("invalid browser target list")
	}
	for _, target := range targets {
		if target.Type == "page" && strings.HasPrefix(target.Socket, "ws://127.0.0.1:"+strconv.Itoa(port)+"/devtools/page/") {
			return target.Socket, cleanup, nil
		}
	}
	cleanup()
	return "", nil, errors.New("browser page target missing")
}

func CapturePage(ctx context.Context, browser, navigation, origin string, proxy *ScanProxy, window time.Duration) (*Capture, error) {
	connection, cleanup, err := startBrowser(ctx, browser, proxy.Address)
	if err != nil {
		return nil, err
	}
	defer cleanup()
	setup := func(session string) {
		connection.call("Network.enable", map[string]any{}, session)
		connection.call("Network.setCacheDisabled", map[string]any{"cacheDisabled": true}, session)
		connection.call("Fetch.enable", map[string]any{"handleAuthRequests": true, "patterns": []any{map[string]any{"urlPattern": "*"}}}, session)
		connection.call("Target.setAutoAttach", map[string]any{"autoAttach": true, "waitForDebuggerOnStart": true, "flatten": true}, session)
	}
	setup("")
	connection.call("Page.enable", map[string]any{}, "")
	connection.call("Browser.setDownloadBehavior", map[string]any{"behavior": "deny"}, "")
	frame, _ := connection.call("Page.getFrameTree", map[string]any{}, "")
	var tree struct {
		FrameTree struct {
			Frame struct {
				ID string `json:"id"`
			} `json:"frame"`
		} `json:"frameTree"`
	}
	json.Unmarshal(frame, &tree)
	rootFrame := tree.FrameTree.Frame.ID
	// Fetch auth/request events must be processed while navigation is pending.
	// Waiting for navigate before draining them would deadlock every proxy load.
	navigationDone := make(chan error, 1)
	go func() {
		_, err := connection.call("Page.navigate", map[string]any{"url": navigation}, "")
		navigationDone <- err
	}()
	captured := newCapture(origin)
	started, last := time.Now(), time.Now()
	timer := time.NewTimer(window)
	defer timer.Stop()
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()
	auth := map[string]int{}
loop:
	for {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-connection.ctx.Done():
			captured.Partial = true
			break loop
		case err := <-navigationDone:
			if err != nil {
				captured.Partial = true
			}
			navigationDone = nil
		case <-timer.C:
			captured.Partial = captured.Partial || !captured.Loaded
			break loop
		case <-ticker.C:
			if captured.Loaded && time.Since(started) >= 4*time.Second && time.Since(last) > 1500*time.Millisecond {
				break loop
			}
		case event := <-connection.events:
			switch event.Method {
			case "Target.attachedToTarget":
				var p struct {
					Session string `json:"sessionId"`
				}
				json.Unmarshal(event.Params, &p)
				setup(p.Session)
				connection.call("Runtime.runIfWaitingForDebugger", map[string]any{}, p.Session)
			case "Fetch.requestPaused":
				var p struct {
					ID string `json:"requestId"`
				}
				json.Unmarshal(event.Params, &p)
				connection.call("Fetch.continueRequest", map[string]any{"requestId": p.ID}, event.Session)
			case "Fetch.authRequired":
				var p struct {
					ID        string `json:"requestId"`
					Challenge struct {
						Source string `json:"source"`
					} `json:"authChallenge"`
				}
				json.Unmarshal(event.Params, &p)
				answer := map[string]any{"response": "CancelAuth"}
				key := event.Session + ":" + p.ID
				if p.Challenge.Source == "Proxy" && auth[key] < 2 {
					answer = map[string]any{"response": "ProvideCredentials", "username": proxy.User, "password": proxy.Password}
					auth[key]++
				}
				connection.call("Fetch.continueWithAuth", map[string]any{"requestId": p.ID, "authChallengeResponse": answer}, event.Session)
			default:
				captured.event(event, rootFrame)
				if strings.HasPrefix(event.Method, "Network.") {
					last = time.Now()
				}
			}
		}
	}
	captured.Partial = captured.Partial || connection.overflow.Load()
	title, err := connection.call("Runtime.evaluate", map[string]any{"expression": "document.title", "returnByValue": true}, "")
	if err == nil {
		var value struct {
			Result struct {
				Value string `json:"value"`
			}
		}
		if json.Unmarshal(title, &value) == nil {
			captured.Title = strings.TrimSpace(value.Result.Value)
			if len(captured.Title) > 120 {
				captured.Title = captured.Title[:120]
			}
		}
	}
	for origin, endpoint := range captured.Endpoints {
		for kind := range captured.types[origin] {
			endpoint.Types = append(endpoint.Types, kind)
		}
		sort.Strings(endpoint.Types)
	}
	return captured, nil
}

func Discover(ctx context.Context, browser, raw string, dial Dial, resolve Resolve, progress func(string), window time.Duration) (Report, error) {
	navigation, origin, err := Normalize(raw)
	if err != nil {
		return Report{}, err
	}
	if browser == "" {
		return Report{}, errors.New("browser_missing: install Edge, Chrome or Chromium for JavaScript dependency discovery")
	}
	report := Report{URL: origin, Engine: "chromium", Endpoints: []Endpoint{}, Recommendation: "unknown"}
	union := map[string]*Endpoint{origin: {Origin: origin}}
	for _, path := range []string{"direct", "proxy"} {
		if progress != nil {
			progress(path)
		}
		proxy, err := NewScanProxy(ctx, path, dial, resolve, false)
		if err != nil {
			return report, err
		}
		captured, err := CapturePage(ctx, browser, navigation, origin, proxy, window)
		proxy.Close()
		if err != nil {
			if ctx.Err() != nil {
				return report, ctx.Err()
			}
			report.Partial = true
			report.Warning = "browser_capture_failed"
			continue
		}
		report.Partial = report.Partial || captured.Partial
		if path == "direct" {
			report.DirectOK = captured.success()
		} else {
			report.ProxyOK = captured.success()
		}
		if report.Title == "" && captured.success() {
			report.Title = captured.Title
		}
		for key, value := range captured.Endpoints {
			if union[key] == nil {
				if len(union) >= 128 {
					report.Partial = true
					continue
				}
				union[key] = &Endpoint{Origin: key}
			}
			endpoint := union[key]
			if path == "direct" {
				endpoint.Direct = value.Direct
			} else {
				endpoint.Proxy = value.Direct
			}
			kinds := map[string]bool{}
			for _, kind := range append(endpoint.Types, value.Types...) {
				kinds[kind] = true
			}
			endpoint.Types = nil
			for kind := range kinds {
				endpoint.Types = append(endpoint.Types, kind)
			}
			sort.Strings(endpoint.Types)
		}
	}
	if report.Title == "" {
		host, _ := Address(origin)
		report.Title = host
	}
	keys := make([]string, 0, len(union))
	for key := range union {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	for _, key := range keys {
		report.Endpoints = append(report.Endpoints, *union[key])
	}
	// A root 200 alone does not mean its API/scripts work. Compare observed
	// critical dependency failures too; individual origins retain their choices.
	directFailures, proxyFailures := 0, 0
	directOnly, proxyOnly := false, false
	for _, endpoint := range report.Endpoints {
		critical := endpoint.Origin == origin
		for _, kind := range endpoint.Types {
			switch kind {
			case "Document", "Script", "Stylesheet", "XHR", "Fetch", "Font":
				critical = true
			}
		}
		if !critical {
			continue
		}
		d, p := endpoint.Direct, endpoint.Proxy
		if d.Requests > 0 && (d.OK == 0 || d.Failed > 0) {
			directFailures++
		}
		if p.Requests > 0 && (p.OK == 0 || p.Failed > 0) {
			proxyFailures++
		}
		if d.OK > 0 && d.Failed == 0 && p.Failed > 0 {
			directOnly = true
		}
		if p.OK > 0 && p.Failed == 0 && d.Failed > 0 {
			proxyOnly = true
		}
	}
	if report.DirectOK && report.ProxyOK && directOnly && proxyOnly {
		report.Recommendation = "mixed"
	} else if report.ProxyOK && (!report.DirectOK || proxyFailures < directFailures) {
		report.Recommendation = "proxy"
	} else if report.DirectOK {
		report.Recommendation = "direct"
	} else if report.ProxyOK {
		report.Recommendation = "proxy"
	}

	return report, nil
}
