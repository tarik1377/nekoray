package sitebook

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/base64"
	"errors"
	"io"
	"net"
	"net/http"
	"net/netip"
	"strconv"
	"sync"
	"sync/atomic"
	"time"
)

type Dial func(context.Context, string, string, string) (net.Conn, error)
type Resolve func(context.Context, string) ([]netip.Addr, error)
type ScanProxy struct {
	Address, User, Password string
	server                  *http.Server
	listener                net.Listener
	path                    string
	dial                    Dial
	resolve                 Resolve
	allowPrivate            bool // owned loopback tests only; not an RPC option
	ctx                     context.Context
	gate                    chan struct{}
	count                   atomic.Int32
	budget                  atomic.Int64
	mu                      sync.Mutex
	connections             map[net.Conn]bool
}

func NewScanProxy(ctx context.Context, path string, dial Dial, resolve Resolve, allowPrivate bool) (*ScanProxy, error) {
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		return nil, err
	}
	secret := make([]byte, 24)
	if _, err = rand.Read(secret); err != nil {
		listener.Close()
		return nil, err
	}
	p := &ScanProxy{Address: "http://" + listener.Addr().String(), User: "site-scan", Password: base64.RawURLEncoding.EncodeToString(secret), listener: listener, path: path, dial: dial, resolve: resolve, allowPrivate: allowPrivate, ctx: ctx, gate: make(chan struct{}, 24), connections: map[net.Conn]bool{}}
	p.budget.Store(32 << 20)
	p.server = &http.Server{Handler: p, ReadHeaderTimeout: 3 * time.Second, IdleTimeout: 4 * time.Second, MaxHeaderBytes: 16384}
	go p.server.Serve(listener)
	return p, nil
}
func (p *ScanProxy) track(conn net.Conn, add bool) {
	p.mu.Lock()
	if add {
		p.connections[conn] = true
	} else {
		delete(p.connections, conn)
	}
	p.mu.Unlock()
}
func (p *ScanProxy) Close() {
	p.server.Close()
	p.mu.Lock()
	connections := make([]net.Conn, 0, len(p.connections))
	for conn := range p.connections {
		connections = append(connections, conn)
	}
	p.mu.Unlock()
	for _, conn := range connections {
		conn.Close()
	}
}
func (p *ScanProxy) pinned(ctx context.Context, network, address string) (net.Conn, error) {
	host, port, err := net.SplitHostPort(address)
	if err != nil {
		return nil, err
	}
	if _, err = strconv.ParseUint(port, 10, 16); err != nil {
		return nil, err
	}
	var addresses []netip.Addr
	if ip, err := netip.ParseAddr(host); err == nil {
		addresses = []netip.Addr{ip}
	} else {
		addresses, err = p.resolve(ctx, host)
		if err != nil {
			return nil, errors.New("scan DNS unavailable")
		}
	}
	var failure error = errors.New("no public address for scan")
	for _, ip := range addresses {
		if !p.allowPrivate && !PublicIP(ip) {
			continue
		}
		conn, err := p.dial(ctx, p.path, network, net.JoinHostPort(ip.String(), port))
		if err == nil {
			return conn, nil
		}
		failure = err
	}
	return nil, failure
}

type budgetWriter struct {
	io.Writer
	remaining *atomic.Int64
}

func (w budgetWriter) Write(data []byte) (int, error) {
	if w.remaining.Add(-int64(len(data))) < 0 {
		return 0, errors.New("scan transfer budget exhausted")
	}
	return w.Writer.Write(data)
}
func (p *ScanProxy) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	user, password, ok := r.BasicAuth()
	// BasicAuth reads Authorization, whereas explicit forward proxies use a
	// separate header. Parse that on a private request copy, never forward it.
	if header := r.Header.Get("Proxy-Authorization"); header != "" {
		copy := r.Clone(r.Context())
		copy.Header = make(http.Header)
		copy.Header.Set("Authorization", header)
		user, password, ok = copy.BasicAuth()
	}
	if !ok || user != p.User || subtle.ConstantTimeCompare([]byte(password), []byte(p.Password)) != 1 {
		w.Header().Set("Proxy-Authenticate", `Basic realm="GreenRhythm isolated scan"`)
		w.WriteHeader(407)
		return
	}
	if p.count.Add(1) > 256 || p.budget.Load() <= 0 {
		http.Error(w, "Scan limit", 429)
		return
	}
	select {
	case p.gate <- struct{}{}:
		defer func() { <-p.gate }()
	default:
		http.Error(w, "Scan concurrency limit", 503)
		return
	}
	ctx, cancel := context.WithTimeout(p.ctx, 18*time.Second)
	defer cancel()
	if r.Method == http.MethodConnect {
		remote, err := p.pinned(ctx, "tcp", r.Host)
		if err != nil {
			http.Error(w, "Scan connection failed", 502)
			return
		}
		defer remote.Close()
		hijacker, ok := w.(http.Hijacker)
		if !ok {
			http.Error(w, "Unavailable", 500)
			return
		}
		client, buffer, err := hijacker.Hijack()
		if err != nil {
			return
		}
		defer client.Close()
		p.track(client, true)
		p.track(remote, true)
		defer p.track(client, false)
		defer p.track(remote, false)
		_ = client.SetDeadline(time.Now().Add(18 * time.Second))
		_ = remote.SetDeadline(time.Now().Add(18 * time.Second))
		if _, err = buffer.WriteString("HTTP/1.1 200 Connection Established\r\n\r\n"); err != nil {
			return
		}
		if buffer.Flush() != nil {
			return
		}
		done := make(chan struct{})
		go func() { io.Copy(budgetWriter{remote, &p.budget}, buffer); remote.Close(); close(done) }()
		io.Copy(budgetWriter{client, &p.budget}, remote)
		client.Close()
		remote.Close()
		<-done
		return
	}
	if r.URL.Scheme != "http" || r.URL.User != nil || r.URL.Host == "" {
		http.Error(w, "Invalid scan request", 400)
		return
	}
	request := r.Clone(ctx)
	request.RequestURI = ""
	request.Header = r.Header.Clone()
	request.Header.Del("Proxy-Authorization")
	request.Header.Del("Proxy-Connection")
	request.Header.Del("Authorization")
	transport := &http.Transport{Proxy: nil, DisableKeepAlives: true, DialContext: p.pinned, MaxResponseHeaderBytes: 16384, ResponseHeaderTimeout: 4 * time.Second}
	defer transport.CloseIdleConnections()
	response, err := transport.RoundTrip(request)
	if err != nil {
		http.Error(w, "Scan HTTP failed", 502)
		return
	}
	defer response.Body.Close()
	for key, values := range response.Header {
		for _, value := range values {
			w.Header().Add(key, value)
		}
	}
	w.WriteHeader(response.StatusCode)
	io.Copy(budgetWriter{w, &p.budget}, response.Body)
}
