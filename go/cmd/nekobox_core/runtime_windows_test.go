//go:build windows

package main

import (
	"net"
	"runtime"
	"sync"
	"testing"
)

// Windows keeps pointers passed to overlapped socket operations beyond the
// submitting syscall. Closing a pending accept while stacks move exercises the
// runtime path behind golang/go#77975. This uses loopback only, never a TUN.
func TestWindowsPendingAcceptCancellation(t *testing.T) {
	const workers, iterations = 16, 100
	var wg sync.WaitGroup
	errors := make(chan error, workers)
	for worker := 0; worker < workers; worker++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < iterations; i++ {
				listener, err := net.Listen("tcp4", "127.0.0.1:0")
				if err != nil {
					errors <- err
					return
				}
				started, done := make(chan struct{}), make(chan struct{})
				go func() {
					close(started)
					conn, _ := listener.Accept()
					if conn != nil {
						conn.Close()
					}
					close(done)
				}()
				<-started
				runtime.Gosched()
				listener.Close()
				<-done
				if i%25 == 0 {
					runtime.GC()
				}
			}
		}()
	}
	wg.Wait()
	close(errors)
	for err := range errors {
		t.Error(err)
	}
}
