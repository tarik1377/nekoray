package sitebook

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/url"
	"os"
	"os/exec"
	"sync"
	"time"
)

const browserWorkerArg = "--greenrhythm-owned-site-browser-worker"

type browserWorkerConfig struct {
	Browser string `json:"browser"`
	Proxy   string `json:"proxy"`
}
type browserWorkerReply struct {
	Socket string `json:"socket"`
	Error  string `json:"error,omitempty"`
}

// A separate guardian owns the disposable browser. EOF on its private input
// pipe also cleans the process tree/profile when the parent exits abruptly.
// It cannot load an arbitrary executable or operate on a caller's directory.
func init() {
	if len(os.Args) != 2 || os.Args[1] != browserWorkerArg || os.Getenv("GREENRHYTHM_SITE_BROWSER_WORKER") != "1" {
		return
	}
	runBrowserWorker()
	os.Exit(0)
}
func runBrowserWorker() {
	ctx, cancel := context.WithTimeout(context.Background(), 65*time.Second)
	defer cancel()
	input := bufio.NewReaderSize(os.Stdin, 8192)
	line, err := input.ReadBytes('\n')
	var config browserWorkerConfig
	if err != nil || len(line) > 8192 || json.Unmarshal(line, &config) != nil || config.Browser == "" || config.Browser != FindBrowser() {
		return
	}
	proxy, err := url.Parse(config.Proxy)
	if err != nil || proxy.Scheme != "http" || proxy.Hostname() != "127.0.0.1" || proxy.Port() == "" || proxy.User != nil || proxy.Path != "" {
		return
	}
	eof := make(chan struct{})
	go func() { io.Copy(io.Discard, input); close(eof); cancel() }()
	socket, cleanup, err := launchBrowser(ctx, config.Browser, config.Proxy)
	if err != nil {
		json.NewEncoder(os.Stdout).Encode(browserWorkerReply{Error: "browser_launch_failed"})
		return
	}
	defer cleanup()
	if json.NewEncoder(os.Stdout).Encode(browserWorkerReply{Socket: socket}) != nil {
		return
	}
	select {
	case <-eof:
	case <-ctx.Done():
	}
}
func startBrowser(ctx context.Context, browser, proxy string) (*cdp, func(), error) {
	executable, err := os.Executable()
	if err != nil {
		return nil, nil, err
	}
	command := exec.Command(executable, browserWorkerArg)
	prepareBrowser(command)
	command.Env = append(os.Environ(), "GREENRHYTHM_SITE_BROWSER_WORKER=1")
	input, err := command.StdinPipe()
	if err != nil {
		return nil, nil, err
	}
	output, err := command.StdoutPipe()
	if err != nil {
		input.Close()
		return nil, nil, err
	}
	if err = command.Start(); err != nil {
		input.Close()
		output.Close()
		return nil, nil, err
	}
	exited := make(chan struct{})
	go func() { command.Wait(); close(exited) }()
	stop := context.AfterFunc(ctx, func() { input.Close() })
	var once sync.Once
	cleanup := func() {
		once.Do(func() {
			stop()
			input.Close()
			select {
			case <-exited:
			case <-time.After(3 * time.Second):
			}
			output.Close()
		})
	}
	if json.NewEncoder(input).Encode(browserWorkerConfig{Browser: browser, Proxy: proxy}) != nil {
		cleanup()
		return nil, nil, errors.New("browser worker unavailable")
	}
	type result struct {
		reply browserWorkerReply
		err   error
	}
	ready := make(chan result, 1)
	go func() {
		var reply browserWorkerReply
		e := json.NewDecoder(io.LimitReader(output, 8192)).Decode(&reply)
		ready <- result{reply, e}
	}()
	var socket string
	select {
	case result := <-ready:
		if result.err != nil || result.reply.Error != "" {
			cleanup()
			return nil, nil, errors.New("browser launch failed")
		}
		socket = result.reply.Socket
	case <-ctx.Done():
		cleanup()
		return nil, nil, ctx.Err()
	case <-time.After(12 * time.Second):
		cleanup()
		return nil, nil, errors.New("browser launch timed out")
	}
	connection, err := newCDP(ctx, socket)
	if err != nil {
		cleanup()
		return nil, nil, err
	}
	return connection, func() { connection.close(); cleanup() }, nil
}
