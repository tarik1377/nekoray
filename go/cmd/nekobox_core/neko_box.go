package main

import (
	"context"
	"errors"
	"log"
	"net"
	"net/http"
	"os"
	"sync/atomic"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/experimental/clashapi"
	"github.com/sagernet/sing-box/experimental/clashapi/trafficontrol"
	"github.com/sagernet/sing-box/experimental/v2rayapi"
	"github.com/sagernet/sing-box/include"
	"github.com/sagernet/sing-box/option"
	M "github.com/sagernet/sing/common/metadata"
	"github.com/sagernet/sing/service"

	singjson "github.com/sagernet/sing/common/json"
)

// statsService is the running instance's v2ray_api stats service, used by
// QueryStats to read per-outbound traffic counters. Accessed concurrently
// (QueryStats runs on a gRPC goroutine while Start/Stop run on others), so it is
// an atomic pointer; nil when no instance runs.
var statsService atomic.Pointer[v2rayapi.StatsService]

// trafficManager — учёт соединений работающего ядра. Нужен ради ЗАКРЫТЫХ
// соединений: снимок Clash API отдаёт только живые, а короткое соединение живёт
// меньше, чем промежуток между опросами, и в снимок не попадает никогда. Именно
// такими были запросы игры к списку серверов — по живому списку их не видно
// вовсе, и понять, куда они уходили, нельзя.
//
// ЭТО ОСОЗНАННОЕ ИСКЛЮЧЕНИЕ из правила «брать соединения через опубликованный
// API, а не из внутренностей» (см. начало connections.go). Другого пути нет:
// ClosedConnections() наружу не отдаёт ни один маршрут Clash API. Цена —
// привязка к конкретному типу; она проявится ошибкой СБОРКИ при обновлении
// sing-box, а не молчаливой пустотой, и потому приемлема.
var trafficManager atomic.Pointer[trafficontrol.Manager]

// nekoCreate creates a sing-box instance from JSON config bytes.
func nekoCreate(configJSON []byte) (*box.Box, context.CancelFunc, error) {
	// Transform legacy config fields if needed
	transformed, err := transformConfigBytes(configJSON)
	if err != nil {
		log.Printf("config transform warning: %v", err)
	} else {
		configJSON = transformed
	}

	// Write the debug config only in debug mode — it contains secrets (inbound auth
	// password, clash secret, outbound credentials) and was previously written to a
	// persistent world-readable file on every start.
	if neko_common.Debug {
		_ = os.WriteFile("./neko_debug_config.json", configJSON, 0600)
	}

	ctx, cancel := context.WithCancel(context.Background())
	ctx = include.Context(ctx)

	options, err := singjson.UnmarshalExtendedContext[option.Options](ctx, configJSON)
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

	// Capture the v2ray_api stats service (enabled via injected experimental.v2ray_api)
	// so QueryStats can read per-outbound traffic counters for the GUI.
	statsService.Store(nil)
	if v2 := service.FromContext[adapter.V2RayServer](ctx); v2 != nil {
		if ss, ok := v2.StatsService().(*v2rayapi.StatsService); ok {
			statsService.Store(ss)
		}
	}

	// Тем же приёмом — учёт соединений. Отсутствие не ошибка: при выключенном
	// clash_api его просто нет, и список закрытых останется пустым.
	trafficManager.Store(nil)
	if cs := service.FromContext[adapter.ClashServer](ctx); cs != nil {
		if srv, ok := cs.(*clashapi.Server); ok {
			trafficManager.Store(srv.TrafficManager())
		}
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
