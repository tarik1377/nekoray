package robloxauto

import (
	"errors"
	"net"
	"sync"
	"sync/atomic"
	"time"
)

type stream struct {
	net.Conn
	mu                                  sync.Mutex
	timer                               *time.Timer
	wrote, replied, closed, invalidated atomic.Bool
	invalidate                          func()
	wait                                time.Duration
}

// TrackTCP expires an availability decision when a new application stream
// fails before its first server response. It never retries/replays that stream.
func TrackTCP(conn net.Conn, wait time.Duration, invalidate func()) net.Conn {
	return &stream{Conn: conn, wait: wait, invalidate: invalidate}
}
func (s *stream) bad() {
	if !s.closed.Load() && !s.invalidated.Swap(true) {
		s.invalidate()
	}
}
func (s *stream) Read(b []byte) (int, error) {
	n, err := s.Conn.Read(b)
	if n > 0 && !s.replied.Swap(true) {
		s.mu.Lock()
		if s.timer != nil {
			s.timer.Stop()
		}
		s.mu.Unlock()
	}
	if err != nil && s.wrote.Load() && !s.replied.Load() && !errors.Is(err, net.ErrClosed) {
		s.bad()
	}
	return n, err
}
func (s *stream) Write(b []byte) (int, error) {
	n, err := s.Conn.Write(b)
	if n > 0 && !s.wrote.Swap(true) {
		s.mu.Lock()
		if !s.closed.Load() && !s.replied.Load() {
			s.timer = time.AfterFunc(s.wait, func() {
				if !s.replied.Load() {
					s.bad()
				}
			})
		}
		s.mu.Unlock()
	}
	if err != nil && !errors.Is(err, net.ErrClosed) {
		s.bad()
	}
	return n, err
}
func (s *stream) Close() error {
	s.closed.Store(true)
	s.mu.Lock()
	if s.timer != nil {
		s.timer.Stop()
	}
	s.mu.Unlock()
	return s.Conn.Close()
}
