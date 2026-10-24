package main

import (
	"context"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"

	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/route/rule"
	"github.com/sagernet/sing/common/buf"
	M "github.com/sagernet/sing/common/metadata"
)

func TestProcessNamePlatformCaseMatching(t *testing.T) {
	r := rule.NewProcessItem([]string{"Dota2.EXE", "my-owned-game.exe"})
	for _, tc := range []struct {
		name string
		want bool
	}{
		{"Dota2.EXE", true}, {"dota2.exe", runtime.GOOS == "windows"},
		{"MY-OWNED-GAME.EXE", runtime.GOOS == "windows"},
		{"my-owned-game.exe", true}, {"not-dota2.exe", false},
		{"dota2.exe.bak", false},
	} {
		metadata := adapter.InboundContext{ProcessInfo: &adapter.ConnectionOwner{ProcessPath: filepath.Join("fixture", tc.name)}}
		if got := r.Match(&metadata); got != tc.want {
			t.Errorf("%s: got %v, want %v", tc.name, got, tc.want)
		}
	}
	if r.Match(&adapter.InboundContext{}) || r.Match(&adapter.InboundContext{ProcessInfo: &adapter.ConnectionOwner{}}) {
		t.Fatal("missing process owner matched")
	}
}

// Metadata represents the process information supplied by a TUN inbound. No
// TUN device, OS route, installed client, subscription or public endpoint is used.
// Both selected outbounds terminate at different owned loopback echo servers;
// their response identifies the selected route, including for UDP/443.
func TestGameVoiceUDPAndTCPRoutes(t *testing.T) {
	fixture, err := os.ReadFile(filepath.Join("testdata", "game_voice_rules.json"))
	if err != nil {
		t.Fatal(err)
	}
	var base []map[string]interface{}
	if err = json.Unmarshal(fixture, &base); err != nil {
		t.Fatal(err)
	}

	tcpTargets, udpTargets := map[string]int{}, map[string]int{}
	for _, label := range []string{"bypass", "proxy"} {
		label := label
		listener, err := net.Listen("tcp4", "127.0.0.1:0")
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { _ = listener.Close() })
		tcpTargets[label] = listener.Addr().(*net.TCPAddr).Port
		go func() {
			for {
				c, e := listener.Accept()
				if e != nil {
					return
				}
				go func() {
					defer c.Close()
					_ = c.SetDeadline(time.Now().Add(3 * time.Second))
					b := []byte{0}
					if _, e := io.ReadFull(c, b); e == nil {
						_, _ = io.WriteString(c, label)
					}
				}()
			}
		}()
		packet, err := net.ListenPacket("udp4", "127.0.0.1:0")
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { _ = packet.Close() })
		udpTargets[label] = packet.LocalAddr().(*net.UDPAddr).Port
		go func() {
			b := make([]byte, 2048)
			for {
				_, from, e := packet.ReadFrom(b)
				if e != nil {
					return
				}
				_, _ = packet.WriteTo([]byte(label), from)
			}
		}()
	}
	// One instance per transport, with no profile reloads during the probes.
	for _, variant := range []string{"udp", "tcp", "udp-custom", "tcp-custom"} {
		network := strings.Split(variant, "-")[0]
		custom := strings.HasSuffix(variant, "-custom")
		t.Run(variant, func(t *testing.T) {
			targets := tcpTargets
			if network == "udp" {
				targets = udpTargets
			}
			var rules []map[string]interface{}
			if err := json.Unmarshal(fixture, &rules); err != nil {
				t.Fatal(err)
			}
			if custom {
				rules = append([]map[string]interface{}{
					{"process_name": []string{"my-owned-game.exe"}, "outbound": "bypass"},
					{"process_name": []string{"dota2.exe", "Discord.exe"}, "outbound": "proxy"},
				}, rules...)
			}
			for _, rule := range rules {
				if tag, _ := rule["outbound"].(string); tag == "bypass" || tag == "proxy" {
					rule["override_address"] = "127.0.0.1"
					rule["override_port"] = targets[tag]
				}
			}
			sniffFixture, err := os.ReadFile(filepath.Join("testdata", "game_voice_sniff.json"))
			if err != nil {
				t.Fatal(err)
			}
			var sniffRules []map[string]interface{}
			if err = json.Unmarshal(sniffFixture, &sniffRules); err != nil {
				t.Fatal(err)
			}
			if custom {
				names := append(sniffRules[0]["process_name"].([]interface{}), "my-owned-game.exe")
				sniffRules[0]["process_name"] = names
				sniffRules[1]["rules"].([]interface{})[1].(map[string]interface{})["rules"].([]interface{})[1].(map[string]interface{})["process_name"] = names
			}
			rules = append(sniffRules, rules...)
			config := map[string]interface{}{
				"log":       map[string]interface{}{"disabled": true},
				"outbounds": []interface{}{map[string]interface{}{"type": "direct", "tag": "bypass"}, map[string]interface{}{"type": "direct", "tag": "proxy"}, map[string]interface{}{"type": "block", "tag": "block"}},
				"route":     map[string]interface{}{"final": "proxy", "rules": rules},
			}
			raw, _ := json.Marshal(config)
			box, cancel, err := nekoCreate(raw)
			if err != nil {
				t.Fatal(err)
			}
			defer func() { cancel(); _ = box.Close() }()
			processes := []string{"cs2.exe", "csgo.exe", "steam.exe", "steamwebhelper.exe", "steamservice.exe", "SteamService.exe",
				"dota2.exe", "r5apex.exe", "VALORANT.exe", "VALORANT-Win64-Shipping.exe", "League of Legends.exe", "LeagueClient.exe", "LeagueClientUx.exe", "LeagueClientUxRenderer.exe",
				"FortniteClient-Win64-Shipping.exe", "FortniteClient-Win64-Shipping_BE.exe", "FortniteClient-Win64-Shipping_EAC.exe", "FortniteLauncher.exe",
				"SquadGame-Win64-Shipping.exe", "SquadGame.exe", "Squad.exe", "SoTGame.exe", "SoTLauncher.exe", "EscapeFromTarkov.exe", "BsgLauncher.exe",
				"Discord.exe", "discord.exe", "DiscordPTB.exe", "DiscordCanary.exe", "my-owned-game.exe", "other.exe"}
			if runtime.GOOS == "windows" {
				processes = append(processes, "DOTA2.EXE", "CS2.EXE", "DISCORD.EXE", "MY-OWNED-GAME.EXE", "STEAM.EXE")
			}
			for _, process := range processes {
				processKey := strings.ToLower(process)
				for _, host := range []string{"178.130.128.26:27015", "198.51.100.8:443"} {
					t.Run(process+"/"+host, func(t *testing.T) {
						want := "bypass"
						if strings.HasPrefix(processKey, "discord") || processKey == "other.exe" || processKey == "my-owned-game.exe" && !custom || processKey == "dota2.exe" && custom {
							want = "proxy"
						}
						ordinary := processKey == "other.exe" || processKey == "my-owned-game.exe" && !custom
						if ordinary && strings.HasPrefix(host, "178.") {
							want = "bypass"
						}
						blocked := ordinary && network == "udp" && strings.HasSuffix(host, ":443")
						metadata := adapter.InboundContext{Inbound: "tun-in", Network: network, Destination: M.ParseSocksaddr(host), ProcessInfo: &adapter.ConnectionOwner{ProcessPath: "C:/fixture/" + process}}
						if network == "tcp" {
							client, server := net.Pipe()
							defer client.Close()
							_ = client.SetDeadline(time.Now().Add(3 * time.Second))
							go func() { _ = box.Router().RouteConnection(context.Background(), server, metadata); _ = server.Close() }()
							_, err = client.Write([]byte{1})
							if err != nil {
								t.Fatal(err)
							}
							result := make([]byte, len(want))
							_, err = io.ReadFull(client, result)
							if err != nil || string(result) != want {
								t.Fatalf("got %q, %v; want %s", result, err, want)
							}
							return
						}
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
						_ = client.SetDeadline(time.Now().Add(2 * time.Second))
						done := make(chan error, 1)
						go func() {
							box.Router().RoutePacketConnectionEx(context.Background(), &gameVoicePacketConn{conn: inbound, destination: metadata.Destination, peer: client.LocalAddr().(*net.UDPAddr)}, metadata, func(err error) { done <- err })
						}()
						_, err = client.Write([]byte{1})
						if err != nil {
							t.Fatal(err)
						}
						result := make([]byte, 32)
						n, readErr := client.Read(result)
						if blocked {
							if readErr == nil {
								t.Fatalf("unrelated UDP/443 block bypassed: %q", result[:n])
							}
						} else if readErr != nil || string(result[:n]) != want {
							t.Fatalf("got %q, %v; want %s", result[:n], readErr, want)
						}
						_ = inbound.Close()
						select {
						case <-done:
						case <-time.After(3 * time.Second):
							t.Fatal("owned UDP forwarder did not stop")
						}
					})
				}
			}
		})
	}
}

type gameVoicePacketConn struct {
	conn        *net.UDPConn
	destination M.Socksaddr
	peer        *net.UDPAddr
}

func (c *gameVoicePacketConn) ReadPacket(buffer *buf.Buffer) (M.Socksaddr, error) {
	n, _, err := c.conn.ReadFromUDP(buffer.FreeBytes())
	if err == nil {
		buffer.Truncate(n)
	}
	return c.destination, err
}
func (c *gameVoicePacketConn) WritePacket(buffer *buf.Buffer, _ M.Socksaddr) error {
	defer buffer.Release()
	_, err := c.conn.WriteToUDP(buffer.Bytes(), c.peer)
	return err
}
func (c *gameVoicePacketConn) Close() error                       { return c.conn.Close() }
func (c *gameVoicePacketConn) LocalAddr() net.Addr                { return c.conn.LocalAddr() }
func (c *gameVoicePacketConn) SetDeadline(t time.Time) error      { return c.conn.SetDeadline(t) }
func (c *gameVoicePacketConn) SetReadDeadline(t time.Time) error  { return c.conn.SetReadDeadline(t) }
func (c *gameVoicePacketConn) SetWriteDeadline(t time.Time) error { return c.conn.SetWriteDeadline(t) }
