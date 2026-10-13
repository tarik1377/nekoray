// overlay-base: github.com/sagernet/sing-box v1.13.18
//go:build windows

package route

import (
	"encoding/binary"
	"fmt"
	"net"
	"net/netip"
	"runtime"
	"strconv"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

var getBestPrivateInterface = windows.NewLazySystemDLL("iphlpapi.dll").NewProc("GetBestInterfaceEx")

func lookupPrivateInterface(address netip.Addr) (privateRouteInterface, error) {
	var remote [28]byte // SOCKADDR_INET: IPv4 and IPv6 share this union.
	if address.Is4() {
		binary.LittleEndian.PutUint16(remote[:2], windows.AF_INET)
		bytes := address.As4()
		copy(remote[4:8], bytes[:])
	} else {
		binary.LittleEndian.PutUint16(remote[:2], windows.AF_INET6)
		bytes := address.As16()
		copy(remote[8:24], bytes[:])
		if zone := address.Zone(); zone != "" {
			index, err := strconv.Atoi(zone)
			if err != nil {
				iface, lookupErr := net.InterfaceByName(zone)
				if lookupErr != nil {
					return privateRouteInterface{}, lookupErr
				}
				index = iface.Index
			}
			binary.LittleEndian.PutUint32(remote[24:], uint32(index))
		}
	}
	if err := getBestPrivateInterface.Find(); err != nil {
		return privateRouteInterface{}, err
	}
	var index uint32
	status, _, _ := getBestPrivateInterface.Call(uintptr(unsafe.Pointer(&remote[0])), uintptr(unsafe.Pointer(&index)))
	if status != 0 {
		return privateRouteInterface{}, syscall.Errno(status)
	}

	// Read the description as well as the alias: a user can rename our adapter,
	// while its sing-tun description still identifies it as a loop destination.
	size := uint32(15000)
	for attempt := 0; attempt < 3; attempt++ {
		buffer := make([]byte, size)
		head := (*windows.IpAdapterAddresses)(unsafe.Pointer(&buffer[0]))
		err := windows.GetAdaptersAddresses(windows.AF_UNSPEC, 0, 0, head, &size)
		if err == windows.ERROR_BUFFER_OVERFLOW {
			continue
		}
		if err != nil {
			return privateRouteInterface{}, err
		}
		for adapter := head; adapter != nil; adapter = adapter.Next {
			if adapter.IfIndex != index && adapter.Ipv6IfIndex != index {
				continue
			}
			result := privateRouteInterface{
				name:        windows.UTF16PtrToString(adapter.FriendlyName),
				description: windows.UTF16PtrToString(adapter.Description),
				index:       int(index), up: adapter.OperStatus == 1,
			}
			runtime.KeepAlive(buffer)
			return result, nil
		}
		runtime.KeepAlive(buffer)
		return privateRouteInterface{}, fmt.Errorf("route interface %d disappeared", index)
	}
	return privateRouteInterface{}, fmt.Errorf("adapter table changed repeatedly")
}
