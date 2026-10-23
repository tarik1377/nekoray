package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/binary"
	"encoding/json"
	"encoding/pem"
	"io"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"testing"
	"time"

	"github.com/sagernet/sing-box/adapter"
	M "github.com/sagernet/sing/common/metadata"
)

// Real SOCKS UDP ASSOCIATE -> real VLESS/XUDP/TLS/Vision -> owned UDP echoes.
// This test uses no TUN, OS routes, subscription, account or public endpoint.
func TestUDPTransportThroughProfile(t *testing.T) {
	const user = "00000000-0000-4000-8000-000000000001"
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "localhost"},
		DNSNames: []string{"localhost"}, NotBefore: time.Now().Add(-time.Hour), NotAfter: time.Now().Add(time.Hour),
		KeyUsage: x509.KeyUsageDigitalSignature, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}}
	der, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	private, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	certificate := string(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}))
	privatePEM := string(pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: private}))

	var echoes []*net.UDPAddr
	for range 2 {
		conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { _ = conn.Close() })
		echoes = append(echoes, conn.LocalAddr().(*net.UDPAddr))
		go func() {
			data := make([]byte, 4096)
			for {
				n, from, e := conn.ReadFromUDP(data)
				if e != nil {
					return
				}
				_, _ = conn.WriteToUDP(data[:n], from)
			}
		}()
	}
	for _, kind := range []string{"direct", "process-direct", "vless-default", "vless-xudp", "vless-packetaddr", "vision-default", "vision-xudp", "http-no-udp"} {
		t.Run(kind, func(t *testing.T) {
			outbound := map[string]interface{}{"type": "direct", "tag": "proxy"}
			if kind != "direct" && kind != "process-direct" && kind != "http-no-udp" {
				serverPort := udpFixtureTCPPort(t)
				inbound := map[string]interface{}{"type": "vless", "tag": "owned-vless", "listen": "127.0.0.1", "listen_port": serverPort,
					"users": []interface{}{map[string]interface{}{"uuid": user}}}
				outbound = map[string]interface{}{"type": "vless", "tag": "proxy", "server": "127.0.0.1", "server_port": serverPort, "uuid": user}
				if kind == "vless-xudp" || kind == "vision-xudp" {
					outbound["packet_encoding"] = "xudp"
				}
				if kind == "vless-packetaddr" {
					outbound["packet_encoding"] = "packetaddr"
				}
				if kind == "vision-default" || kind == "vision-xudp" {
					inbound["users"] = []interface{}{map[string]interface{}{"uuid": user, "flow": "xtls-rprx-vision"}}
					inbound["tls"] = map[string]interface{}{"enabled": true, "certificate": []string{certificate}, "key": []string{privatePEM}}
					outbound["flow"] = "xtls-rprx-vision"
					outbound["tls"] = map[string]interface{}{"enabled": true, "server_name": "localhost", "certificate": []string{certificate}}
				}
				udpFixtureCore(t, map[string]interface{}{"log": map[string]interface{}{"disabled": true}, "inbounds": []interface{}{inbound},
					"outbounds": []interface{}{map[string]interface{}{"type": "direct", "tag": "exit"}}, "route": map[string]interface{}{"final": "exit"}})
			} else if kind == "http-no-udp" {
				outbound = map[string]interface{}{"type": "http", "tag": "proxy", "server": "127.0.0.1", "server_port": 1}
			}
			clientPort := udpFixtureTCPPort(t)
			outbounds := []interface{}{outbound}
			route := map[string]interface{}{"final": "proxy"}
			if kind == "process-direct" {
				path, err := os.Executable()
				if err != nil {
					t.Fatal(err)
				}
				route = map[string]interface{}{"final": "proxy", "rules": []interface{}{
					map[string]interface{}{"process_name": []string{filepath.Base(path)}, "outbound": "proxy"},
					map[string]interface{}{"action": "reject"},
				}}
				// No ProcessInfo is injected: actual native socket ownership must
				// be resolved for this process or the datagram is rejected.
			}
			udpFixtureCore(t, map[string]interface{}{"log": map[string]interface{}{"disabled": true},
				"inbounds":  []interface{}{map[string]interface{}{"type": "mixed", "listen": "127.0.0.1", "listen_port": clientPort}},
				"outbounds": outbounds, "route": route})
			for association := 0; association < 2; association++ {
				tcp, udp := udpFixtureAssociate(t, clientPort)
				func() {
					defer tcp.Close()
					defer udp.Close()
					_ = udp.SetDeadline(time.Now().Add(8 * time.Second))
					if kind == "http-no-udp" {
						_ = udp.SetDeadline(time.Now().Add(250 * time.Millisecond))
					}
					for burst := 0; burst < 3; burst++ {
						pending := map[string]int{}
						for seq := 0; seq < 8; seq++ {
							payload := bytes.Repeat([]byte{byte(seq + 1)}, []int{32, 512, 1200}[seq%3])
							copy(payload, "GRUDP")
							binary.BigEndian.PutUint32(payload[5:], uint32(burst*8+seq))
							target := echoes[seq%2]
							packet := []byte{0, 0, 0, 1, 127, 0, 0, 1, byte(target.Port >> 8), byte(target.Port)}
							pending[string(payload)] = target.Port
							if _, err := udp.Write(append(packet, payload...)); err != nil {
								t.Fatal(err)
							}
						}
						for len(pending) > 0 {
							data := make([]byte, 4096)
							n, err := udp.Read(data)
							if kind == "http-no-udp" {
								if err == nil {
									t.Fatal("TCP-only profile unexpectedly delivered UDP")
								}
								return
							}
							if err != nil {
								t.Fatalf("association %d burst %d: %d datagrams missing: %v", association, burst, len(pending), err)
							}
							if n < 10 || !bytes.Equal(data[:8], []byte{0, 0, 0, 1, 127, 0, 0, 1}) {
								t.Fatalf("invalid SOCKS reply: %x", data[:n])
							}
							port, found := pending[string(data[10:n])]
							if !found || int(binary.BigEndian.Uint16(data[8:10])) != port {
								t.Fatal("wrong destination, duplicate or corrupted UDP datagram")
							}
							delete(pending, string(data[10:n]))
						}
						time.Sleep(20 * time.Millisecond) // Same association survives a quiet interval.
					}
				}()
			}
		})
	}
}

func udpFixtureTCPPort(t *testing.T) int {
	t.Helper()
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := listener.Addr().(*net.TCPAddr).Port
	_ = listener.Close()
	return port
}
func udpFixtureCore(t *testing.T, config map[string]interface{}) adapter.Router {
	t.Helper()
	raw, err := json.Marshal(config)
	if err != nil {
		t.Fatal(err)
	}
	instance, cancel, err := nekoCreate(raw)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { cancel(); _ = instance.Close() })
	return instance.Router()
}

func TestUDPProcessLookupFromTunSource(t *testing.T) {
	path, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	echo, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = echo.Close() })
	go func() {
		data := make([]byte, 2048)
		for {
			n, peer, e := echo.ReadFromUDP(data)
			if e != nil {
				return
			}
			_, _ = echo.WriteToUDP(data[:n], peer)
		}
	}()
	router := udpFixtureCore(t, map[string]interface{}{"log": map[string]interface{}{"disabled": true},
		"outbounds": []interface{}{map[string]interface{}{"type": "direct", "tag": "direct"}},
		"route": map[string]interface{}{"final": "direct", "rules": []interface{}{
			map[string]interface{}{"process_name": []string{filepath.Base(path)}, "outbound": "direct"}, map[string]interface{}{"action": "reject"},
		}}})
	inbound, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer inbound.Close()
	client, err := net.DialUDP("udp4", nil, inbound.LocalAddr().(*net.UDPAddr))
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	_ = client.SetDeadline(time.Now().Add(4 * time.Second))
	metadata := adapter.InboundContext{Inbound: "owned-tun", InboundType: "tun", Network: "udp",
		Source: M.SocksaddrFromNet(client.LocalAddr()), Destination: M.SocksaddrFromNet(echo.LocalAddr())}
	done := make(chan error, 1)
	router.RoutePacketConnectionEx(context.Background(), &gameVoicePacketConn{conn: inbound, destination: metadata.Destination, peer: client.LocalAddr().(*net.UDPAddr)}, metadata, func(err error) { done <- err })
	for seq := 0; seq < 8; seq++ {
		payload := []byte("owned UDP socket " + strconv.Itoa(seq))
		if _, err = client.Write(payload); err != nil {
			t.Fatal(err)
		}
		data := make([]byte, 2048)
		n, e := client.Read(data)
		if e != nil || !bytes.Equal(data[:n], payload) {
			t.Fatalf("native TUN-style UDP owner routing failed: %v", e)
		}
	}
	_ = inbound.Close()
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		t.Fatal("owned UDP router did not stop")
	}
}
func udpFixtureAssociate(t *testing.T, port int) (net.Conn, *net.UDPConn) {
	t.Helper()
	tcp, err := net.DialTimeout("tcp4", net.JoinHostPort("127.0.0.1", strconv.Itoa(port)), 2*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	_ = tcp.SetDeadline(time.Now().Add(3 * time.Second))
	if _, err = tcp.Write([]byte{5, 1, 0}); err != nil {
		tcp.Close()
		t.Fatal(err)
	}
	method := make([]byte, 2)
	if _, err = io.ReadFull(tcp, method); err != nil || !bytes.Equal(method, []byte{5, 0}) {
		tcp.Close()
		t.Fatalf("SOCKS method: %x %v", method, err)
	}
	if _, err = tcp.Write([]byte{5, 3, 0, 1, 0, 0, 0, 0, 0, 0}); err != nil {
		tcp.Close()
		t.Fatal(err)
	}
	header := make([]byte, 4)
	if _, err = io.ReadFull(tcp, header); err != nil || !bytes.Equal(header, []byte{5, 0, 0, 1}) {
		tcp.Close()
		t.Fatalf("SOCKS association: %x %v", header, err)
	}
	address := make([]byte, 6)
	if _, err = io.ReadFull(tcp, address); err != nil {
		tcp.Close()
		t.Fatal(err)
	}
	ip := net.IP(address[:4])
	if ip.IsUnspecified() {
		ip = net.IPv4(127, 0, 0, 1)
	}
	udp, err := net.DialUDP("udp4", nil, &net.UDPAddr{IP: ip, Port: int(binary.BigEndian.Uint16(address[4:]))})
	if err != nil {
		tcp.Close()
		t.Fatal(err)
	}
	_ = tcp.SetDeadline(time.Time{})
	return tcp, udp
}
