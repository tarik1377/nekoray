package robloxauto

import (
	"net"
	"sync"
	"sync/atomic"
	"time"
)

// Only observes the first destination reply/no-reply window. Read/Write payloads
// are forwarded unchanged and never replayed on the alternate outbound.
type Packet struct {
	net.PacketConn
	mu                               sync.Mutex
	timer                            *time.Timer
	written, replied, failed, closed bool
	fastRead, fastWrite              atomic.Bool
	observe                          func(bool)
	wait                             time.Duration
}

func TrackPacket(conn net.PacketConn, wait time.Duration, observe func(bool)) *Packet {
	return &Packet{PacketConn: conn, observe: observe, wait: wait}
}
func (p *Packet) WriteTo(bytes []byte, address net.Addr) (int, error) {
	n, err := p.PacketConn.WriteTo(bytes, address)
	if p.fastWrite.Load() {
		return n, err
	}
	p.mu.Lock()
	notifyError := err != nil && !p.failed && !p.closed && !p.replied
	if notifyError {
		p.failed = true
		p.fastWrite.Store(true)
	}
	if n > 0 && !p.written && !p.closed {
		p.written = true
		p.fastWrite.Store(true)
		p.timer = time.AfterFunc(p.wait, func() {
			p.mu.Lock()
			notify := !p.replied && !p.closed && !p.failed
			if notify {
				p.failed = true
			}
			if notify {
				p.observe(false)
			}
			p.mu.Unlock()
		})
	}
	if notifyError {
		p.observe(false)
	}
	p.mu.Unlock()
	return n, err
}
func (p *Packet) ReadFrom(bytes []byte) (int, net.Addr, error) {
	n, address, err := p.PacketConn.ReadFrom(bytes)
	if err == nil && n > 0 && !p.fastRead.Load() {
		p.mu.Lock()
		notify := !p.replied && !p.closed
		p.replied = true
		p.fastRead.Store(true)
		if p.timer != nil {
			p.timer.Stop()
		}
		if notify {
			p.observe(true)
		}
		p.mu.Unlock()
	}
	return n, address, err
}
func (p *Packet) Close() error {
	p.mu.Lock()
	p.closed = true
	if p.timer != nil {
		p.timer.Stop()
	}
	p.mu.Unlock()
	return p.PacketConn.Close()
}
