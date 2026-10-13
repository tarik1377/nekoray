// overlay-base: github.com/sagernet/sing-box v1.13.18
//go:build !windows

package route

import (
	"errors"
	"net/netip"
)

func lookupPrivateInterface(netip.Addr) (privateRouteInterface, error) {
	return privateRouteInterface{}, errors.New("Windows destination route lookup unavailable")
}
