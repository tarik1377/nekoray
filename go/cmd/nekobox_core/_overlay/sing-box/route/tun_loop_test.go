// overlay-base: github.com/sagernet/sing-box v1.13.18
package route

import (
	"net"
	"net/netip"
	"strings"
	"testing"

	tun "github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/control"
)

func loopTestFinder(name string, index int, prefixes ...string) *control.DefaultInterfaceFinder {
	f := control.NewDefaultInterfaceFinder()
	var addresses []netip.Prefix
	for _, prefix := range prefixes {
		addresses = append(addresses, netip.MustParsePrefix(prefix))
	}
	if name != "" {
		f.UpdateInterfaces([]control.Interface{{Name: name, Index: index, Flags: net.FlagRunning | net.FlagUp, Addresses: addresses}})
	}
	return f
}

type loopTestMonitor struct {
	tun.DefaultInterfaceMonitor
	own              []string
	defaultInterface *control.Interface
}

func (m loopTestMonitor) MyInterfaces() []string               { return m.own }
func (m loopTestMonitor) DefaultInterface() *control.Interface { return m.defaultInterface }

// ByAddr checks directly attached subnets BEFORE the existing private-route
// selector. Before #53 it returned our TUN for its own DNS address, bypassing
// all safeguards in privateDestinationInterface and recursively re-entering it.
func TestAutoDetectRejectsOwnTunDestination(t *testing.T) {
	for _, name := range []string{"neko-tun", "My renamed tunnel"} {
		for _, address := range []string{"172.19.0.2:853", "172.19.0.2:53", "[fdfe:dcba:9876::2]:853", "[::ffff:172.19.0.2]:853"} {
			for _, network := range []string{"tcp", "udp"} {
				r := NetworkManager{interfaceFinder: loopTestFinder(name, 99, "172.19.0.1/28", "fdfe:dcba:9876::1/126"),
					interfaceMonitor: loopTestMonitor{own: []string{name}, defaultInterface: &control.Interface{Name: "Ethernet", Index: 2}}}
				got, index, err := r.autoDetectInterfaceForDestination(network, address)
				if err == nil || got != "" || index != -1 || !strings.Contains(err.Error(), "routing loop") {
					t.Fatalf("%s %s via %s allowed recursive binding: %s %d %v", network, address, name, got, index, err)
				}
			}
		}
	}
}

func TestAutoDetectPreservesOtherOnLinkInterfaces(t *testing.T) {
	for _, name := range []string{"Ethernet", "OpenVPN TAP", "OpenVPN DCO", "vEthernet (WSL)", "Other VPN"} {
		r := NetworkManager{interfaceFinder: loopTestFinder(name, 7, "10.0.0.1/8", "192.168.1.1/24", "fd00::1/64"),
			interfaceMonitor: loopTestMonitor{own: []string{"neko-tun"}}}
		for _, address := range []string{"10.20.22.109:443", "10.77.56.11:53", "192.168.1.1:853", "[fd00::42]:443"} {
			got, index, err := r.autoDetectInterfaceForDestination("tcp", address)
			if err != nil || got != name || index != 7 {
				t.Fatalf("corporate/LAN route changed: %s %s %d %v", name, got, index, err)
			}
		}
	}
}

func TestAutoDetectRejectsOwnTunAsDefault(t *testing.T) {
	r := NetworkManager{interfaceFinder: loopTestFinder("", 0), interfaceMonitor: loopTestMonitor{
		own: []string{"renamed TUN"}, defaultInterface: &control.Interface{Name: "renamed TUN", Index: 99}}}
	if _, _, err := r.autoDetectInterfaceForDestination("tcp", "example.test:443"); err == nil {
		t.Fatal("recursive default interface accepted")
	}
	r.interfaceMonitor = loopTestMonitor{own: []string{"renamed TUN"}, defaultInterface: &control.Interface{Name: "Ethernet", Index: 2}}
	if got, index, err := r.autoDetectInterfaceForDestination("udp", "example.test:443"); err != nil || got != "Ethernet" || index != 2 {
		t.Fatalf("ordinary default binding changed: %s %d %v", got, index, err)
	}
}
