// overlay-base: github.com/sagernet/sing-box v1.13.18
package route

import (
	"net/netip"
	"strings"
)

type privateRouteInterface struct {
	name, description string
	index             int
	up                bool
}

// Only local destinations use the OS route. Public destinations retain the
// default-interface binding that prevents packets from looping into our TUN.
func selectPrivateInterface(address netip.Addr, lookup func(netip.Addr) (privateRouteInterface, error)) (string, int, bool) {
	address = address.Unmap()
	if !address.IsValid() || address.IsUnspecified() ||
		!(address.IsPrivate() || address.IsLoopback() || address.IsLinkLocalUnicast()) {
		return "", 0, false
	}
	iface, err := lookup(address)
	if err != nil || !iface.up || iface.index <= 0 || iface.name == "" {
		return "", 0, false
	}
	identity := strings.ToLower(iface.name + " " + iface.description)
	if strings.Contains(identity, "neko-tun") || strings.Contains(identity, "sing-tun") {
		return "", 0, false
	}
	return iface.name, iface.index, true
}

func privateDestinationInterface(address netip.Addr) (string, int, bool) {
	return selectPrivateInterface(address, lookupPrivateInterface)
}
