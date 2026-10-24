// overlay-base: github.com/sagernet/sing-tun v0.8.12-0.20260727151122-3a09076491df
package tun

import (
	"net/netip"
	"testing"

	"github.com/sagernet/sing-tun/internal/gtcpip/checksum"
	"github.com/sagernet/sing-tun/internal/gtcpip/header"
)

type resetCaptureTun struct {
	Tun
	packet []byte
}

func (t *resetCaptureTun) Write(p []byte) (int, error) {
	t.packet = append([]byte(nil), p...)
	return len(p), nil
}

func TestSystemResetPacket(t *testing.T) {
	for _, v6 := range []bool{false, true} {
		for _, ack := range []bool{false, true} {
			capture := new(resetCaptureTun)
			s := System{tun: capture}
			ipSize := header.IPv4MinimumSize
			if v6 {
				ipSize = header.IPv6MinimumSize
			}
			packet := make([]byte, ipSize+header.TCPMinimumSize)
			tcp := header.TCP(packet[ipSize:])
			fields := header.TCPFields{SrcPort: 60000, DstPort: 853, DataOffset: header.TCPMinimumSize, SeqNum: 12345, Flags: header.TCPFlagSyn}
			if ack {
				fields.Flags = header.TCPFlagAck
				fields.AckNum = 54321
			}
			tcp.Encode(&fields)
			if v6 {
				ip := header.IPv6(packet)
				ip.Encode(&header.IPv6Fields{PayloadLength: header.TCPMinimumSize, TransportProtocol: header.TCPProtocolNumber, HopLimit: 64, SrcAddr: netip.MustParseAddr("fd00::1"), DstAddr: netip.MustParseAddr("fd00::2")})
				if err := s.resetIPv6TCP(ip, tcp); err != nil {
					t.Fatal(err)
				}
			} else {
				ip := header.IPv4(packet)
				ip.Encode(&header.IPv4Fields{TotalLength: uint16(len(packet)), Protocol: uint8(header.TCPProtocolNumber), TTL: 64, SrcAddr: netip.MustParseAddr("198.18.253.1"), DstAddr: netip.MustParseAddr("198.18.253.2")})
				if err := s.resetIPv4TCP(ip, tcp); err != nil {
					t.Fatal(err)
				}
			}
			p := capture.packet[PacketOffset:]
			var source, destination []byte
			if v6 {
				ip := header.IPv6(p)
				if ip.HopLimit() == 0 {
					t.Fatal("IPv6 reset has zero Hop Limit")
				}
				source, destination = ip.SourceAddressSlice(), ip.DestinationAddressSlice()
				if ip.SourceAddr() != netip.MustParseAddr("fd00::2") || ip.DestinationAddr() != netip.MustParseAddr("fd00::1") {
					t.Fatal("IPv6 reset endpoint mismatch")
				}
			} else {
				ip := header.IPv4(p)
				if ip.TTL() == 0 {
					t.Fatal("IPv4 reset has zero TTL")
				}
				source, destination = ip.SourceAddressSlice(), ip.DestinationAddressSlice()
				if ip.SourceAddr() != netip.MustParseAddr("198.18.253.2") || ip.DestinationAddr() != netip.MustParseAddr("198.18.253.1") {
					t.Fatal("IPv4 reset endpoint mismatch")
				}
				if checksum.Checksum(p[:ipSize], 0) != 0xffff {
					t.Fatal("invalid IPv4 reset checksum")
				}
			}
			rst := header.TCP(p[ipSize:])
			if rst.SourcePort() != 853 || rst.DestinationPort() != 60000 || rst.Flags()&header.TCPFlagRst == 0 {
				t.Fatal("invalid reset TCP endpoints/flags")
			}
			if ack {
				if rst.SequenceNumber() != 54321 || rst.Flags()&header.TCPFlagAck != 0 {
					t.Fatal("ACK reset sequence mismatch")
				}
			} else if rst.AckNumber() != 12346 || rst.Flags()&header.TCPFlagAck == 0 {
				t.Fatal("SYN reset acknowledgement mismatch")
			}
			sum := header.PseudoHeaderChecksum(header.TCPProtocolNumber, source, destination, header.TCPMinimumSize)
			if checksum.Checksum(rst, sum) != 0xffff {
				t.Fatal("invalid reset TCP checksum")
			}
		}
	}
}
