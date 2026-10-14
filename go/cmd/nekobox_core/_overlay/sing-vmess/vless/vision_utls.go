// overlay-base: github.com/sagernet/sing-vmess v0.2.8-0.20250909125414-3aed155119a1
//go:build with_utls

package vless

import (
	"net"
	"reflect"
	"unsafe"

	utls "github.com/metacubex/utls"
	N "github.com/sagernet/sing/common/network"
)

func init() {
	tlsRegistry = append(tlsRegistry, func(conn net.Conn) (loaded bool, netConn net.Conn, reflectType reflect.Type, reflectPointer unsafe.Pointer) {
		uConn, loaded := N.CastReader[*utls.UConn](conn)
		if loaded {
			return true, uConn.NetConn(), reflect.TypeOf(uConn.Conn).Elem(), unsafe.Pointer(uConn.Conn)
		}
		tlsConn, loaded := N.CastReader[*utls.Conn](conn)
		if loaded {
			return true, tlsConn.NetConn(), reflect.TypeOf(tlsConn).Elem(), unsafe.Pointer(tlsConn)
		}
		return
	})
}
