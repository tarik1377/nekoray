package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"net"
	"net/netip"
	"runtime"
	"syscall"
	"time"

	box "github.com/sagernet/sing-box"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/control"
)

var errTunTCPStartup = errors.New("Windows TUN did not accept a local TCP connection")

type tunProbeTarget struct {
	interfaceName string
	local         netip.Addr
	dns           netip.Addr
}

type tunStartupCheck struct {
	ctx      context.Context
	probe    func(context.Context, tunProbeTarget) error
	fallback bool
}

func startWithTunRecovery(ctx context.Context, config []byte, probe func(context.Context, tunProbeTarget) error) (*box.Box, context.CancelFunc, error) {
	lookup := tunSystemLookupForRequest(ctx)
	check := &tunStartupCheck{ctx: ctx, probe: probe}
	instance, cancel, err := nekoCreateChecked(config, lookup, check)
	if !errors.Is(err, errTunTCPStartup) || !tun.WithGVisor || ctx.Err() != nil {
		return instance, cancel, err
	}
	log.Print("System TUN: локальная проверка TCP не прошла, запускаю gVisor для этого подключения")
	check.fallback = true
	instance, cancel, err = nekoCreateChecked(config, lookup, check)
	if err == nil {
		log.Print("TUN: проверка TCP через gVisor прошла; выбранный в настройках стек сохранён")
	}
	return instance, cancel, err
}

func (check *tunStartupCheck) prepare(options *option.Options) *tunProbeTarget {
	if check == nil || runtime.GOOS != "windows" {
		return nil
	}
	var selected *option.TunInboundOptions
	for _, inbound := range options.Inbounds {
		if inbound.Type != C.TypeTun {
			continue
		}
		candidate, ok := inbound.Options.(*option.TunInboundOptions)
		if selected != nil || !ok || inbound.Tag != "tun-in" || !candidate.AutoRoute ||
			candidate.InterfaceName == "" || candidate.Stack != "system" {
			return nil
		}
		selected = candidate
	}
	if selected == nil {
		return nil
	}
	for _, prefix := range selected.Address {
		address := prefix.Addr()
		if !prefix.IsValid() || !address.Is4() {
			continue
		}
		peer := address.Next()
		if !prefix.Contains(peer) {
			return nil
		}
		if check.fallback {
			selected.Stack = "gvisor"
		}
		return &tunProbeTarget{interfaceName: selected.InterfaceName, local: address, dns: peer}
	}
	return nil
}

func (check *tunStartupCheck) verify(target tunProbeTarget) error {
	if err := check.probe(check.ctx, target); err != nil {
		if check.ctx.Err() != nil {
			return check.ctx.Err()
		}
		return fmt.Errorf("%w: %v", errTunTCPStartup, err)
	}
	return nil
}

// The DNS guard handles port 53 on this exact virtual address inside the box.
// A TCP handshake exercises the TUN packet path without looking up a domain
// or depending on the server, DNS provider, or Internet connection.
func probeTunTCP(ctx context.Context, target tunProbeTarget) error {
	device, err := net.InterfaceByName(target.interfaceName)
	if err != nil {
		return err
	}
	dialer := net.Dialer{
		LocalAddr: net.TCPAddrFromAddrPort(netip.AddrPortFrom(target.local, 0)),
		Timeout:   time.Second,
		Control: func(network, address string, socket syscall.RawConn) error {
			return control.BindToInterface0(nil, socket, network, address, "", device.Index, false)
		},
	}
	for attempt := 0; attempt < 3 && ctx.Err() == nil; attempt++ {
		var connection net.Conn
		connection, err = dialer.DialContext(ctx, "tcp4", netip.AddrPortFrom(target.dns, 53).String())
		if err == nil {
			_ = connection.Close()
			return nil
		}
	}
	if ctx.Err() != nil {
		return ctx.Err()
	}
	return err
}
