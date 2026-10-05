package grpc_server

import (
	"context"
	"grpc_server/gen"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
)

type watchedProbeConn struct {
	net.Conn
	readStarted chan struct{}
	readExited  chan struct{}
	closed      chan struct{}
	readOnce    sync.Once
	closeOnce   sync.Once
}

func (c *watchedProbeConn) Read(p []byte) (int, error) {
	c.readOnce.Do(func() { close(c.readStarted) })
	defer close(c.readExited)
	return c.Conn.Read(p)
}

func (c *watchedProbeConn) Close() error {
	c.closeOnce.Do(func() { close(c.closed) })
	return c.Conn.Close()
}

// A peer that receives the DNS packet but never replies reproduces the real
// timeout without contacting a public resolver or starting a VPN tunnel.
func stalledUDPProbe(t *testing.T) *watchedProbeConn {
	t.Helper()
	client, peer := net.Pipe()
	conn := &watchedProbeConn{Conn: client, readStarted: make(chan struct{}), readExited: make(chan struct{}), closed: make(chan struct{})}
	oldDial, oldHTTP := neko_common.DialContext, neko_common.CreateProxyHttpClient
	neko_common.DialContext = func(context.Context, interface{}, string, string) (net.Conn, error) { return conn, nil }
	neko_common.CreateProxyHttpClient = func(interface{}) *http.Client { return &http.Client{} }
	done := make(chan struct{})
	go func() { _, _ = io.Copy(io.Discard, peer); close(done) }()
	t.Cleanup(func() {
		_ = conn.Close()
		_ = peer.Close()
		<-done
		neko_common.DialContext, neko_common.CreateProxyHttpClient = oldDial, oldHTTP
	})
	return conn
}

func TestFullTestUDPTimeoutReleasesConnection(t *testing.T) {
	conn := stalledUDPProbe(t)
	resp, err := DoFullTest(context.Background(), &gen.TestReq{FullUdpLatency: true}, nil)
	if err != nil || !strings.Contains(resp.FullReport, "UDPLatency: Timeout") {
		t.Fatalf("expected UDP timeout report: %+v, %v", resp, err)
	}
	select {
	case <-conn.closed:
	case <-time.After(250 * time.Millisecond):
		t.Fatal("UDP probe returned on timeout but left its connection and reader alive")
	}
	select {
	case <-conn.readExited:
	case <-time.After(time.Second):
		t.Fatal("timed-out UDP reader did not exit")
	}
}

func TestFullTestUDPCancellationReleasesConnection(t *testing.T) {
	conn := stalledUDPProbe(t)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan struct{})
	go func() { _, _ = DoFullTest(ctx, &gen.TestReq{FullUdpLatency: true}, nil); close(done) }()
	select {
	case <-conn.readStarted:
	case <-time.After(time.Second):
		t.Fatal("UDP reader did not start")
	}
	cancel()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Error("cancelled full test continued running")
		_ = conn.Close()
		<-done
	}
	select {
	case <-conn.closed:
	case <-time.After(250 * time.Millisecond):
		t.Fatal("cancelled full test did not close the probe connection")
	}
	select {
	case <-conn.readExited:
	case <-time.After(time.Second):
		t.Fatal("cancelled UDP reader did not exit")
	}
}

type probeTransport func(*http.Request) (*http.Response, error)

func (f probeTransport) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }

func useProbeHTTP(t *testing.T, transport http.RoundTripper) {
	t.Helper()
	old := neko_common.CreateProxyHttpClient
	neko_common.CreateProxyHttpClient = func(interface{}) *http.Client { return &http.Client{Transport: transport} }
	t.Cleanup(func() { neko_common.CreateProxyHttpClient = old })
}

type countedProbeBody struct {
	io.Reader
	bytes  int
	closed bool
}

func (b *countedProbeBody) Read(p []byte) (int, error) {
	n, err := b.Reader.Read(p)
	b.bytes += n
	return n, err
}
func (b *countedProbeBody) Close() error { b.closed = true; return nil }

func TestFullTestTraceBoundsAndClosesBody(t *testing.T) {
	for _, tc := range []struct {
		name, body, want string
		status           int
	}{
		{"IPv4", "ip=203.0.113.7\n", "203.0.113.7", 200},
		{"IPv6", "ip=2001:db8::1\n", "2001:db8::1", 200},
		{"short error", "x", "Error", 200},
		{"empty", "", "Error", 200},
		{"HTTP error", "ip=203.0.113.7\n", "Error", 503},
		{"large response", "ip=203.0.113.7\n" + strings.Repeat("x", 100000), "203.0.113.7", 200},
	} {
		t.Run(tc.name, func(t *testing.T) {
			body := &countedProbeBody{Reader: strings.NewReader(tc.body)}
			useProbeHTTP(t, probeTransport(func(r *http.Request) (*http.Response, error) {
				if deadline, ok := r.Context().Deadline(); !ok || time.Until(deadline) > 5*time.Second {
					t.Error("trace has no bounded timeout")
				}
				return &http.Response{StatusCode: tc.status, Body: body, Header: make(http.Header)}, nil
			}))
			resp, err := DoFullTest(context.Background(), &gen.TestReq{FullInOut: true, InAddress: "127.0.0.1"}, nil)
			if err != nil || !strings.Contains(resp.FullReport, "Out: "+tc.want) {
				t.Fatalf("unexpected trace result: %+v, %v", resp, err)
			}
			if body.bytes > 4096 || !body.closed {
				t.Fatalf("trace retained or over-read body: %d bytes, closed=%v", body.bytes, body.closed)
			}
		})
	}
}

func TestFullTestMalformedDownloadURLReturnsError(t *testing.T) {
	useProbeHTTP(t, probeTransport(func(*http.Request) (*http.Response, error) {
		t.Error("malformed URL reached network")
		return nil, io.ErrUnexpectedEOF
	}))
	resp, err := DoFullTest(context.Background(), &gen.TestReq{FullSpeed: true, FullSpeedUrl: "http://%"}, nil)
	if err != nil || resp.FullReport != "Speed: Error" {
		t.Fatalf("unexpected invalid-URL result: %+v, %v", resp, err)
	}
}

func TestFullTestDownloadHonorsCancellation(t *testing.T) {
	started, stopped := make(chan struct{}), make(chan struct{})
	useProbeHTTP(t, probeTransport(func(r *http.Request) (*http.Response, error) {
		close(started)
		<-r.Context().Done()
		close(stopped)
		return nil, r.Context().Err()
	}))
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan struct{})
	go func() {
		_, _ = DoFullTest(ctx, &gen.TestReq{FullSpeed: true, FullSpeedUrl: "https://example.invalid/probe"}, nil)
		close(done)
	}()
	select {
	case <-started:
	case <-time.After(time.Second):
		t.Fatal("download probe did not start")
	}
	cancel()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("download ignored parent cancellation")
	}
	select {
	case <-stopped:
	case <-time.After(time.Second):
		t.Fatal("download worker did not exit")
	}
}
