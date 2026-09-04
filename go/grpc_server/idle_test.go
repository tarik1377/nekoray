package grpc_server

import (
	"io"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

// Набор отвечает на один вопрос: отличает ли сторож медленную закачку от
// заглохшей. Если не отличает, он либо не помогает вовсе, либо обрывает людей
// на плохом канале — а это хуже, чем его отсутствие.

// trickle отдаёт по байту с паузой: медленный, но живой источник.
type trickle struct {
	data  []byte
	pos   int
	pause time.Duration
}

func (t *trickle) Read(p []byte) (int, error) {
	if t.pos >= len(t.data) {
		return 0, io.EOF
	}
	time.Sleep(t.pause)
	p[0] = t.data[t.pos]
	t.pos++
	return 1, nil
}

// stalled отдаёт немного и замолкает навсегда — сервер, который не разорвал
// соединение, а перестал отвечать.
type stalled struct {
	head   []byte
	pos    int
	closed chan struct{}
}

func (s *stalled) Read(p []byte) (int, error) {
	if s.pos < len(s.head) {
		p[0] = s.head[s.pos]
		s.pos++
		return 1, nil
	}
	<-s.closed // ждём, пока сторож нас отменит
	return 0, io.ErrUnexpectedEOF
}

func TestIdleGuardLetsSlowDownloadFinish(t *testing.T) {
	var stopped atomic.Bool
	src := &trickle{data: []byte(strings.Repeat("x", 40)), pause: 5 * time.Millisecond}
	guard, stop := newIdleGuard(src, 200*time.Millisecond, func() { stopped.Store(true) })
	defer stop()

	n, err := io.Copy(io.Discard, guard)
	if err != nil {
		t.Fatalf("медленное, но живое чтение должно доходить до конца: %v", err)
	}
	if n != 40 {
		t.Fatalf("прочитано %d байт вместо 40", n)
	}
	if stopped.Load() || guard.timedOut() {
		t.Fatal("сторож сработал на живой закачке — он обрывал бы людей на плохом канале")
	}
}

func TestIdleGuardStopsStalledDownload(t *testing.T) {
	closed := make(chan struct{})
	src := &stalled{head: []byte("aaa"), closed: closed}
	guard, stop := newIdleGuard(src, 50*time.Millisecond, func() { close(closed) })
	defer stop()

	start := time.Now()
	_, err := io.Copy(io.Discard, guard)
	elapsed := time.Since(start)

	if err == nil {
		t.Fatal("замерший источник должен приводить к ошибке, а не к бесконечному ожиданию")
	}
	if !guard.timedOut() {
		t.Fatal("причина обрыва должна быть опознана как пауза — иначе человеку покажут «context canceled»")
	}
	if elapsed > 2*time.Second {
		t.Fatalf("обрыв занял %v — сторож ждал слишком долго", elapsed)
	}
}

func TestIdleGuardResetsOnEveryChunk(t *testing.T) {
	// Двадцать пауз по 20 мс при пороге 60 мс: суммарно 400 мс, то есть много
	// больше порога. Сторож не должен срабатывать, если байты идут.
	var stopped atomic.Bool
	src := &trickle{data: []byte(strings.Repeat("y", 20)), pause: 20 * time.Millisecond}
	guard, stop := newIdleGuard(src, 60*time.Millisecond, func() { stopped.Store(true) })
	defer stop()

	if _, err := io.Copy(io.Discard, guard); err != nil {
		t.Fatalf("неожиданная ошибка: %v", err)
	}
	if stopped.Load() {
		t.Fatal("сторож меряет паузу между байтами, а не общее время")
	}
}
