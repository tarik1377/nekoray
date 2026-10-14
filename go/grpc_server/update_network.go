package grpc_server

import (
	"context"
	"errors"
	releaseauth "greenrhythm_release"
	"net/http"
	"sync"
	"time"
)

type updateNetwork struct {
	direct, fallback                         *http.Client
	checkTimeout, headerTimeout, idleTimeout time.Duration
	archivePath                              string
	verify                                   func(*releaseauth.Manifest, string) error
	prepareUpdater                           func(string) error
}

func defaultUpdateNetwork(direct, fallback *http.Client) updateNetwork {
	return updateNetwork{direct: direct, fallback: fallback, checkTimeout: 10 * time.Second, headerTimeout: 15 * time.Second,
		idleTimeout: 90 * time.Second, archivePath: "../greenrhythm.zip", verify: releaseauth.Verify, prepareUpdater: prepareWindowsUpdater}
}
func fetchManifestAttempt(ctx context.Context, client *http.Client, platform string, network updateNetwork) (*releaseManifest, error) {
	attempt, cancel := context.WithTimeout(ctx, network.checkTimeout)
	defer cancel()
	return fetchManifestVerified(attempt, client, platform, network.verify)
}

var errUpdateHeaders = errors.New("сервер обновлений не ответил вовремя — повторите попытку позже")

// Limit connection/header waiting separately from the response body. Cancelling
// a deadline context as soon as headers arrive would also cancel a valid slow
// body, so the request keeps its own context until the caller finishes reading.
func beginUpdateDownload(ctx context.Context, client *http.Client, address string, timeout time.Duration) (*http.Response, context.CancelFunc, error) {
	attempt, cancel := context.WithCancel(ctx)
	req, err := http.NewRequestWithContext(attempt, http.MethodGet, address, nil)
	if err != nil {
		cancel()
		return nil, nil, err
	}
	var mu sync.Mutex
	received, expired := false, false
	timer := time.AfterFunc(timeout, func() {
		mu.Lock()
		defer mu.Unlock()
		if !received {
			expired = true
			cancel()
		}
	})
	download := *client
	download.Timeout = 0 // Only the body idle guard limits a slow transfer.
	response, err := download.Do(req)
	mu.Lock()
	received = true
	timedOut := expired
	mu.Unlock()
	timer.Stop()
	if err != nil || timedOut {
		if response != nil && response.Body != nil {
			response.Body.Close()
		}
		cancel()
		if timedOut {
			return nil, nil, errUpdateHeaders
		}
		return nil, nil, err
	}
	return response, cancel, nil
}
