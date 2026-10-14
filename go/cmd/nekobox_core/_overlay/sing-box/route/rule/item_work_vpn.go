// overlay-base: github.com/sagernet/sing-box v1.13.18
package rule

import (
	"context"
	"fmt"
	"net/netip"
	"time"

	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/common/workvpn"
	"github.com/sagernet/sing/service"
)

type workVPNInterface interface {
	Interface(netip.Addr) (string, int, bool)
	HasScope(string) bool
}

type WorkVPNItem struct {
	ctx     context.Context
	manager workVPNInterface
	dns     adapter.DNSRouter
}

func NewWorkVPNItem(ctx context.Context) (*WorkVPNItem, error) {
	manager := service.FromContext[*workvpn.Manager](ctx)
	if manager == nil {
		return nil, fmt.Errorf("windows_work_vpn rule requires enabled work VPN discovery")
	}
	return &WorkVPNItem{ctx, manager, service.FromContext[adapter.DNSRouter](ctx)}, nil
}

func (r *WorkVPNItem) Match(metadata *adapter.InboundContext) bool {
	// Without any specific work VPN route, preserve the original proxy path
	// without introducing a DNS lookup for an HTTP/SOCKS domain destination.
	if detector, ok := r.manager.(interface{ HasNetworks() bool }); ok && !detector.HasNetworks() {
		return false
	}
	if metadata.Destination.Addr.IsValid() {
		_, _, ok := r.manager.Interface(metadata.Destination.Addr)
		return ok
	}
	if metadata.Destination.Fqdn == "" {
		return false
	}
	// Resolve through the normal DNS router: its work fallback runs only after
	// a negative ordinary response (or for an OS-advertised namespace).
	ips := metadata.DestinationAddresses
	if len(ips) == 0 {
		if r.dns == nil {
			return false
		}
		ctx, cancel := context.WithTimeout(adapter.WithContext(r.ctx, metadata), 2*time.Second)
		defer cancel()
		var err error
		ips, err = r.dns.Lookup(ctx, metadata.Destination.Fqdn, adapter.DNSQueryOptions{})
		if err != nil {
			// Keep an OS-scoped name on the local direct outbound, where the
			// DNS failure propagates instead of sending its name to a proxy.
			return r.manager.HasScope(metadata.Destination.Fqdn)
		}
	}
	if len(ips) == 0 {
		return false
	}
	for _, ip := range ips {
		if _, _, ok := r.manager.Interface(ip); !ok {
			return false
		}
	}
	metadata.DestinationAddresses = ips
	return true
}

func (r *WorkVPNItem) String() string { return "windows_work_vpn" }
