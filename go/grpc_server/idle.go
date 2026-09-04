package grpc_server

import (
	"io"
	"sync/atomic"
	"time"
)

// idleGuard прерывает чтение, если байты перестали приходить.
//
// ЗАЧЕМ ИМЕННО ПАУЗА, А НЕ ОБЩИЙ СРОК. Общий таймаут клиента при скачивании снят
// намеренно: пакет весит около шестидесяти мегабайт, и потолок на весь ответ
// обрывал бы честную медленную закачку — тем более через туннель. Но снятый
// потолок оставил случай, при котором не происходит ничего: сервер не разорвал
// соединение, а замер. Тогда io.Copy ждёт вечно, окно обновления крутится,
// отменить нечем, а в журнале пусто. Рядом стоял комментарий, объяснявший, что
// границей остаётся ctx, — вызывающая сторона срока ему не ставила.
//
// Пауза между байтами разделяет эти два случая: медленная закачка проходит,
// заглохшая обрывается.
type idleGuard struct {
	src    io.Reader
	timer  *time.Timer
	period time.Duration
	fired  *atomic.Bool
}

// newIdleGuard оборачивает чтение и вызывает stop, если за period не пришло ни
// одного байта. Возвращает также функцию остановки таймера.
func newIdleGuard(src io.Reader, period time.Duration, stop func()) (*idleGuard, func()) {
	fired := &atomic.Bool{}
	timer := time.AfterFunc(period, func() {
		fired.Store(true)
		stop()
	})
	return &idleGuard{src: src, timer: timer, period: period, fired: fired}, func() { timer.Stop() }
}

func (g *idleGuard) Read(p []byte) (int, error) {
	n, err := g.src.Read(p)
	if n > 0 {
		// Reset после срабатывания уже ничего не спасёт — запрос отменён, — но и
		// вреда не делает: признак fired остаётся взведённым, и ошибку назовут
		// правильно.
		g.timer.Reset(g.period)
	}
	return n, err
}

// timedOut говорит, что чтение прервали мы, а не сеть. Без этого человек увидел
// бы «context canceled» и решил бы, что отменил обновление сам.
func (g *idleGuard) timedOut() bool { return g.fired.Load() }
