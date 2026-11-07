package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/adapter/outbound"
	"github.com/sagernet/sing-box/log"
	M "github.com/sagernet/sing/common/metadata"
	"github.com/sagernet/sing/service"
	"nekobox_core/robloxauto"
)

const robloxAutoType = "greenrhythm-roblox-auto"

type robloxAutoOptions struct {
	Direct string `json:"direct"`
	Proxy  string `json:"proxy"`
	Scope  string `json:"cache_scope"`
	Cache  bool   `json:"cache"`
}
type robloxAutoOutbound struct {
	outbound.Adapter
	ctx           context.Context
	manager       adapter.OutboundManager
	network       adapter.NetworkManager
	logger        log.ContextLogger
	options       robloxAutoOptions
	engine        *robloxauto.Engine
	store         *robloxauto.Store
	direct, proxy adapter.Outbound
	mu            sync.Mutex
	checked       time.Time
	identity      string
}

func registerRobloxAuto(ctx context.Context) {
	registry := service.FromContext[adapter.OutboundRegistry](ctx).(*outbound.Registry)
	outbound.Register[robloxAutoOptions](registry, robloxAutoType, newRobloxAuto)
}
func newRobloxAuto(ctx context.Context, _ adapter.Router, logger log.ContextLogger, tag string, options robloxAutoOptions) (adapter.Outbound, error) {
	if options.Direct == "" || options.Proxy == "" || options.Direct == options.Proxy || options.Direct == tag || options.Proxy == tag {
		return nil, errors.New("Roblox auto needs independent direct/VPN outbounds")
	}
	if len(options.Scope) != 64 {
		return nil, errors.New("invalid Roblox cache scope")
	}
	if _, err := hex.DecodeString(options.Scope); err != nil {
		return nil, errors.New("invalid Roblox cache scope")
	}
	engine := robloxauto.New(ctx)
	s := &robloxAutoOutbound{Adapter: outbound.NewAdapter(robloxAutoType, tag, []string{"tcp", "udp"}, []string{options.Direct, options.Proxy}),
		ctx: ctx, manager: service.FromContext[adapter.OutboundManager](ctx), network: service.FromContext[adapter.NetworkManager](ctx), logger: logger, options: options, engine: engine}
	if options.Cache {
		s.store = &robloxauto.Store{Directory: filepath.Join(".", ".roblox-auto"), Scope: strings.ToLower(options.Scope)}
		engine.SetPersistence(func(network string, entries map[string]robloxauto.Entry, force bool) {
			if err := s.store.Save(network, entries, force); err != nil {
				logger.Warn("Roblox availability cache could not be saved; using in-memory results")
			}
		})
	}
	return s, nil
}
func (s *robloxAutoOutbound) Start() error {
	var ok bool
	if s.direct, ok = s.manager.Outbound(s.options.Direct); !ok {
		return errors.New("Roblox direct outbound missing")
	}
	if s.proxy, ok = s.manager.Outbound(s.options.Proxy); !ok {
		return errors.New("Roblox VPN outbound missing")
	}
	s.refreshNetwork()
	return nil
}
func (s *robloxAutoOutbound) Close() error {
	s.engine.Close()
	return nil
}
func (s *robloxAutoOutbound) refreshNetwork() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if time.Since(s.checked) < 5*time.Second {
		return
	}
	s.checked = time.Now()
	parts := []string{}
	if s.network != nil {
		if selected := s.network.DefaultNetworkInterface(); selected != nil {
			data, _ := json.Marshal(selected)
			parts = append(parts, string(data))
		}
		data, _ := json.Marshal(s.network.WIFIState())
		parts = append(parts, string(data))
	}
	// A profile's own failover group may change its selected node without a
	// config rebuild. Do not reuse a previous member's availability decision.
	child := s.proxy
	for depth := 0; child != nil && depth < 8; depth++ {
		parts = append(parts, "vpn:"+child.Tag())
		group, ok := child.(adapter.OutboundGroup)
		if !ok {
			break
		}
		next, loaded := s.manager.Outbound(group.Now())
		if !loaded {
			break
		}
		child = next
	}
	if interfaces, err := net.Interfaces(); err == nil {
		for _, iface := range interfaces {
			if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
				continue
			}
			addresses, _ := iface.Addrs()
			for _, address := range addresses {
				parts = append(parts, iface.Name+":"+address.String())
			}
		}
	}
	sort.Strings(parts)
	hash := sha256.Sum256([]byte(strings.Join(parts, "\n")))
	identity := hex.EncodeToString(hash[:])
	if identity == s.identity {
		return
	}
	s.identity = identity
	s.engine.SetNetwork(identity)
	if s.store != nil {
		s.engine.Restore(identity, s.store.Load(identity))
	}
}
func (s *robloxAutoOutbound) detour(path robloxauto.Path) adapter.Outbound {
	if path == robloxauto.Direct {
		return s.direct
	}
	return s.proxy
}
func autoHost(ctx context.Context, destination M.Socksaddr) string {
	if destination.IsFqdn() {
		return robloxauto.Host(destination.Fqdn)
	}
	if metadata := adapter.ContextFrom(ctx); metadata != nil && metadata.Domain != "" {
		return robloxauto.Host(metadata.Domain)
	}
	return destination.Addr.String()
}
func (s *robloxAutoOutbound) chooseTCP(ctx context.Context, destination M.Socksaddr) robloxauto.Path {
	host := autoHost(ctx, destination)
	if robloxauto.Local(destination.Addr.String()) || robloxauto.Local(host) {
		return robloxauto.Direct
	}
	// Unknown plugin/service hosts retain the old full-VPN policy and are
	// never turned into arbitrary health-check/SSRF destinations.
	if destination.Port != 443 || !robloxauto.KnownHost(host) {
		return robloxauto.Proxy
	}
	s.refreshNetwork()
	probe := robloxauto.HTTPSProbe(func(ctx context.Context, path robloxauto.Path, network, address string) (net.Conn, error) {
		return s.detour(path).DialContext(ctx, network, M.ParseSocksaddr(address))
	})
	return s.engine.TCP(ctx, host, probe)
}
func (s *robloxAutoOutbound) DialContext(ctx context.Context, network string, destination M.Socksaddr) (net.Conn, error) {
	if strings.HasPrefix(network, "udp") {
		path, generation, key := s.chooseUDP(ctx, destination)
		conn, err := s.detour(path).DialContext(ctx, network, destination)
		if err != nil {
			if key != "" {
				s.engine.ObserveUDP(key, generation, path, false)
			}
			return nil, err
		}
		if key == "" {
			return conn, nil
		}
		s.logger.InfoContext(ctx, "UDP via ", string(path), "; route held for this flow")
		packet := robloxauto.TrackPacket(&connectedPacket{Conn: conn}, 4*time.Second, func(reply bool) { s.engine.ObserveUDP(key, generation, path, reply) })
		return &trackedUDPConn{Conn: conn, packet: packet}, nil
	}
	path := s.chooseTCP(ctx, destination)
	s.logger.InfoContext(ctx, "TCP via ", string(path))
	// Never replay a failed application connection: it may already contain
	// early TLS/application bytes. The next availability check chooses again.
	conn, err := s.detour(path).DialContext(ctx, network, destination)
	if err != nil {
		s.engine.InvalidateTCP(autoHost(ctx, destination), s.engine.Generation(), path)
	} else if destination.Port == 443 && robloxauto.KnownHost(autoHost(ctx, destination)) {
		host, generation := autoHost(ctx, destination), s.engine.Generation()
		conn = robloxauto.TrackTCP(conn, 4*time.Second, func() { s.engine.InvalidateTCP(host, generation, path) })
	}
	return conn, err
}

// The router calls this once BEFORE its immutable connection-accounting
// snapshot. A per-flow adapter pins the choice, so diagnostics show the actual
// DIRECT/VPN tag, and a concurrent cache update cannot move this connection.
func (s *robloxAutoOutbound) SelectConnection(ctx context.Context, metadata adapter.InboundContext) (adapter.Outbound, error) {
	ctx = adapter.WithContext(ctx, &metadata)
	selected := &robloxSelectedOutbound{parent: s, host: autoHost(ctx, metadata.Destination)}
	if metadata.Network == "udp" {
		selected.path, selected.generation, selected.key = s.chooseUDP(ctx, metadata.Destination)
	} else {
		selected.path = s.chooseTCP(ctx, metadata.Destination)
		selected.generation = s.engine.Generation()
	}
	child := s.detour(selected.path)
	selected.Adapter = outbound.NewAdapter(child.Type(), child.Tag(), child.Network(), nil)
	return selected, nil
}

type robloxSelectedOutbound struct {
	outbound.Adapter
	parent          *robloxAutoOutbound
	path            robloxauto.Path
	generation, key string
	host            string
}

func (s *robloxSelectedOutbound) DialContext(ctx context.Context, network string, destination M.Socksaddr) (net.Conn, error) {
	conn, err := s.parent.detour(s.path).DialContext(ctx, network, destination)
	if err != nil {
		if s.key == "" {
			s.parent.engine.InvalidateTCP(s.host, s.generation, s.path)
		}
		if s.key != "" {
			s.parent.engine.ObserveUDP(s.key, s.generation, s.path, false)
		}
		return nil, err
	}
	if s.key == "" {
		if !strings.HasPrefix(network, "udp") && robloxauto.KnownHost(s.host) && destination.Port == 443 {
			return robloxauto.TrackTCP(conn, 4*time.Second, func() { s.parent.engine.InvalidateTCP(s.host, s.generation, s.path) }), nil
		}
		return conn, nil
	}
	packet := robloxauto.TrackPacket(&connectedPacket{Conn: conn}, 4*time.Second, func(reply bool) { s.parent.engine.ObserveUDP(s.key, s.generation, s.path, reply) })
	return &trackedUDPConn{Conn: conn, packet: packet}, nil
}
func (s *robloxSelectedOutbound) ListenPacket(ctx context.Context, destination M.Socksaddr) (net.PacketConn, error) {
	conn, err := s.parent.detour(s.path).ListenPacket(ctx, destination)
	if err != nil {
		if s.key != "" {
			s.parent.engine.ObserveUDP(s.key, s.generation, s.path, false)
		}
		return nil, err
	}
	if s.key == "" {
		return conn, nil
	}
	return robloxauto.TrackPacket(conn, 4*time.Second, func(reply bool) { s.parent.engine.ObserveUDP(s.key, s.generation, s.path, reply) }), nil
}
func (s *robloxAutoOutbound) chooseUDP(ctx context.Context, destination M.Socksaddr) (robloxauto.Path, string, string) {
	host := autoHost(ctx, destination)
	if robloxauto.Local(destination.Addr.String()) || robloxauto.Local(host) {
		return robloxauto.Direct, "", ""
	}
	s.refreshNetwork()
	key := destination.String()
	path, generation := s.engine.UDP(key)
	return path, generation, key
}
func (s *robloxAutoOutbound) ListenPacket(ctx context.Context, destination M.Socksaddr) (net.PacketConn, error) {
	path, generation, key := s.chooseUDP(ctx, destination)
	conn, err := s.detour(path).ListenPacket(ctx, destination)
	if err != nil {
		if key != "" {
			s.engine.ObserveUDP(key, generation, path, false)
		}
		return nil, err
	}
	s.logger.InfoContext(ctx, "UDP via ", string(path), "; route held for this flow")
	if key == "" {
		return conn, nil
	}
	return robloxauto.TrackPacket(conn, 4*time.Second, func(reply bool) { s.engine.ObserveUDP(key, generation, path, reply) }), nil
}

type connectedPacket struct{ net.Conn }

func (c *connectedPacket) ReadFrom(b []byte) (int, net.Addr, error) {
	n, e := c.Read(b)
	return n, c.RemoteAddr(), e
}
func (c *connectedPacket) WriteTo(b []byte, _ net.Addr) (int, error) { return c.Write(b) }

type trackedUDPConn struct {
	net.Conn
	packet *robloxauto.Packet
}

func (c *trackedUDPConn) Read(b []byte) (int, error) { n, _, e := c.packet.ReadFrom(b); return n, e }
func (c *trackedUDPConn) Write(b []byte) (int, error) {
	return c.packet.WriteTo(b, c.Conn.RemoteAddr())
}
func (c *trackedUDPConn) Close() error { return c.packet.Close() }
