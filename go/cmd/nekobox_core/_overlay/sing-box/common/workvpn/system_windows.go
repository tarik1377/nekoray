// overlay-base: github.com/sagernet/sing-box v1.13.18
//go:build windows

package workvpn

import (
	"fmt"
	"net/netip"
	"runtime"
	"strconv"
	"strings"
	"unsafe"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/registry"
)

func readSystemSnapshot() (Snapshot, error) {
	var result Snapshot
	size := uint32(15000)
	for attempt := 0; ; attempt++ {
		buffer := make([]byte, size)
		head := (*windows.IpAdapterAddresses)(unsafe.Pointer(&buffer[0]))
		err := windows.GetAdaptersAddresses(windows.AF_UNSPEC, 0, 0, head, &size)
		if err == windows.ERROR_BUFFER_OVERFLOW && attempt < 2 {
			continue
		}
		if err != nil {
			return Snapshot{}, err
		}
		for a := head; a != nil; a = a.Next {
			item := Adapter{Name: windows.UTF16PtrToString(a.FriendlyName), Description: windows.UTF16PtrToString(a.Description), Index: int(a.IfIndex), IPv6Index: int(a.Ipv6IfIndex), Up: a.OperStatus == windows.IfOperStatusUp, IPv4Metric: a.Ipv4Metric, IPv6Metric: a.Ipv6Metric}
			if suffix := windows.UTF16PtrToString(a.DnsSuffix); suffix != "" {
				item.Suffixes = append(item.Suffixes, suffix)
			}
			for suffix := a.FirstDnsSuffix; suffix != nil; suffix = suffix.Next {
				if value := windows.UTF16ToString(suffix.String[:]); value != "" {
					item.Suffixes = append(item.Suffixes, value)
				}
			}
			for d := a.FirstDnsServerAddress; d != nil; d = d.Next {
				if d.Address.Sockaddr == nil {
					continue
				}
				ip, ok := netip.AddrFromSlice(d.Address.IP())
				if !ok {
					continue
				}
				ip = ip.Unmap()
				if ip.Is6() && ip.IsLinkLocalUnicast() {
					ip = ip.WithZone(strconv.Itoa(item.IPv6Index))
				}
				item.DNS = append(item.DNS, ip)
			}
			result.Adapters = append(result.Adapters, item)
		}
		runtime.KeepAlive(buffer)
		break
	}
	var table *windows.MibIpForwardTable2
	if err := windows.GetIpForwardTable2(windows.AF_UNSPEC, &table); err != nil {
		return Snapshot{}, err
	}
	if table == nil {
		return Snapshot{}, fmt.Errorf("empty OS route table")
	}
	defer windows.FreeMibTable(unsafe.Pointer(table))
	for _, r := range table.Rows() {
		if r.ValidLifetime == 0 || r.Loopback != 0 {
			continue
		}
		var ip netip.Addr
		switch r.DestinationPrefix.Prefix.Family {
		case windows.AF_INET:
			ip = netip.AddrFrom4((*windows.RawSockaddrInet4)(unsafe.Pointer(&r.DestinationPrefix.Prefix)).Addr)
		case windows.AF_INET6:
			ip = netip.AddrFrom16((*windows.RawSockaddrInet6)(unsafe.Pointer(&r.DestinationPrefix.Prefix)).Addr)
		default:
			continue
		}
		p := netip.PrefixFrom(ip, int(r.DestinationPrefix.PrefixLength)).Masked()
		if p.IsValid() {
			result.Routes = append(result.Routes, Route{p, int(r.InterfaceIndex), r.Metric})
		}
	}
	readNRPT(&result)
	return result, nil
}

func readNRPT(snapshot *Snapshot) {
	// OpenVPN selects the policy hive whenever it exists. Read the same plain
	// split-DNS rules using KEY_READ; never create or update registry keys.
	paths := []string{`SOFTWARE\Policies\Microsoft\Windows NT\DNSClient\DnsPolicyConfig`, `SYSTEM\CurrentControlSet\Services\Dnscache\Parameters\DnsPolicyConfig`}
	for _, path := range paths {
		root, err := registry.OpenKey(registry.LOCAL_MACHINE, path, registry.READ)
		if err == registry.ErrNotExist {
			continue
		}
		if err != nil {
			return
		}
		defer root.Close()
		names, err := root.ReadSubKeyNames(-1)
		if err != nil {
			return
		}
		for _, name := range names {
			key, err := registry.OpenKey(root, name, registry.READ)
			if err != nil {
				continue
			}
			flags, _, _ := key.GetIntegerValue("ConfigOptions")
			namespaces, _, nameErr := key.GetStringsValue("Name")
			servers, _, serverErr := key.GetStringValue("GenericDNSServers")
			key.Close()
			// Other flags require DNSSEC/IPsec/DirectAccess enforcement which
			// this transport cannot provide. Do not reinterpret those policies.
			if nameErr != nil {
				continue
			}
			if flags != 8 || serverErr != nil {
				for _, namespace := range namespaces {
					snapshot.NRPT = append(snapshot.NRPT, PolicyDNS{Namespace: namespace})
				}
				continue
			}
			for _, text := range strings.FieldsFunc(servers, func(r rune) bool { return r == ';' || r == ',' || r == ' ' }) {
				ip, err := netip.ParseAddr(text)
				if err != nil {
					continue
				}
				ip = ip.Unmap()
				for _, namespace := range namespaces {
					if namespaceScope(namespace, strings.TrimPrefix(namespace, ".")) == 0 {
						continue
					}
					snapshot.NRPT = append(snapshot.NRPT, PolicyDNS{namespace, ip})
				}
			}
		}
		return // Existing policy hive takes precedence even when empty.
	}
}
