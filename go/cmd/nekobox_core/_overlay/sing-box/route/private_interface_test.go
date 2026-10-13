// overlay-base: github.com/sagernet/sing-box v1.13.18
package route

import (
	"errors"
	"net/netip"
	"testing"
)

func TestPrivateDestinationInterface(t *testing.T) {
	lookup := func(netip.Addr) (privateRouteInterface, error) {
		return privateRouteInterface{name: "Work VPN", index: 7, up: true}, nil
	}
	for _, ip := range []string{"10.4.5.6", "172.20.1.2", "192.168.1.7", "127.0.0.1", "169.254.1.2", "fc00::42", "::1", "::ffff:10.4.5.6"} {
		name, index, ok := selectPrivateInterface(netip.MustParseAddr(ip), lookup)
		if !ok || name != "Work VPN" || index != 7 {
			t.Fatalf("local destination %s ignored: %s %d", ip, name, index)
		}
	}
	for _, ip := range []string{"1.1.1.1", "172.15.1.1", "172.32.1.1", "192.169.1.1", "0.0.0.0", "::", "2606:4700:4700::1111", "224.0.0.1"} {
		_, _, ok := selectPrivateInterface(netip.MustParseAddr(ip), func(netip.Addr) (privateRouteInterface, error) {
			t.Fatalf("OS route consulted for non-local destination %s", ip)
			return privateRouteInterface{}, nil
		})
		if ok {
			t.Fatalf("non-local destination %s changed default binding", ip)
		}
	}
	for _, iface := range []privateRouteInterface{
		{name: "neko-tun", index: 17, up: true},
		{name: "Renamed adapter", description: "sing-tun Tunnel", index: 17, up: true},
		{name: "Work VPN", index: 7, up: false},
		{name: "Work VPN", index: 0, up: true},
	} {
		_, _, ok := selectPrivateInterface(netip.MustParseAddr("10.4.5.6"), func(netip.Addr) (privateRouteInterface, error) { return iface, nil })
		if ok {
			t.Fatalf("unsafe interface selected: %+v", iface)
		}
	}
	_, _, ok := selectPrivateInterface(netip.MustParseAddr("10.4.5.6"), func(netip.Addr) (privateRouteInterface, error) {
		return privateRouteInterface{}, errors.New("route unavailable")
	})
	if ok {
		t.Fatal("failed route lookup selected an interface")
	}
}
