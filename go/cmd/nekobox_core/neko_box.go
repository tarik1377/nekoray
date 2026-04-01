package main

import (
	"context"
	"errors"
	"log"
	"net"
	"net/http"
	"os"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/include"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/json"
	M "github.com/sagernet/sing/common/metadata"
)

// nekoCreate creates a sing-box instance from JSON config bytes.
func nekoCreate(configJSON []byte) (*box.Box, context.CancelFunc, error) {
	// Transform geosite/geoip to rule_set for sing-box 1.12+ compatibility
	transformed, err := transformConfigBytes(configJSON)
	if err != nil {
		log.Printf("config transform warning: %v", err)
	} else {
		configJSON = transformed
	}

	// Debug: write transformed config to file
	if neko_common.Debug {
		_ = os.WriteFile("./neko_debug_config.json", configJSON, 0644)
	}

	// Context MUST be created BEFORE unmarshaling — DNS transport registry is needed
	ctx, cancel := context.WithCancel(context.Background())
	ctx = include.Context(ctx)

	options, err := json.UnmarshalExtendedContext[option.Options](ctx, configJSON)
	if err != nil {
		cancel()
		return nil, nil, err
	}

	instance, err := box.New(box.Options{
		Context: ctx,
		Options: options,
	})
	if err != nil {
		cancel()
		return nil, nil, err
	}

	err = instance.Start()
	if err != nil {
		instance.Close()
		cancel()
		return nil, nil, err
	}

	return instance, cancel, nil
}

// nekoDialContext dials through the box's default outbound.
func nekoDialContext(ctx context.Context, b *box.Box, network, addr string) (net.Conn, error) {
	defaultOut := b.Outbound().Default()
	if defaultOut == nil {
		return nil, errors.New("no default outbound")
	}
	dialer, ok := defaultOut.(interface {
		DialContext(ctx context.Context, network string, destination M.Socksaddr) (net.Conn, error)
	})
	if !ok {
		return nil, errors.New("default outbound does not support dialing")
	}
	return dialer.DialContext(ctx, network, M.ParseSocksaddr(addr))
}

// nekoDialUDP creates a UDP packet connection through the box.
func nekoDialUDP(ctx context.Context, b *box.Box) (net.PacketConn, error) {
	defaultOut := b.Outbound().Default()
	if defaultOut == nil {
		return nil, errors.New("no default outbound")
	}
	listener, ok := defaultOut.(interface {
		ListenPacket(ctx context.Context, destination M.Socksaddr) (net.PacketConn, error)
	})
	if !ok {
		return nil, errors.New("default outbound does not support ListenPacket")
	}
	return listener.ListenPacket(ctx, M.Socksaddr{})
}

// nekoCreateProxyHttpClient creates an HTTP client routing through the box.
func nekoCreateProxyHttpClient(b *box.Box) *http.Client {
	transport := &http.Transport{
		TLSHandshakeTimeout:   3 * time.Second,
		ResponseHeaderTimeout: 3 * time.Second,
		DisableKeepAlives:     true,
		IdleConnTimeout:       30 * time.Second,
	}
	if b != nil {
		transport.DialContext = func(ctx context.Context, network, addr string) (net.Conn, error) {
			return nekoDialContext(ctx, b, network, addr)
		}
	}
	return &http.Client{
		Transport: transport,
		Timeout:   30 * time.Second,
	}
}
