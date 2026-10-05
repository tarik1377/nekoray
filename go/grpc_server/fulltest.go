package grpc_server

import (
	"context"
	"encoding/hex"
	"fmt"
	"grpc_server/gen"
	"io"
	"log"
	"math"
	"net"
	"net/http"
	"strings"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
	"github.com/matsuridayo/libneko/speedtest"
)

const (
	KiB = 1024
	MiB = 1024 * KiB
)

func getBetweenStr(str, start, end string) string {
	_, tail, found := strings.Cut(str, start)
	if !found {
		return ""
	}
	value, _, _ := strings.Cut(tail, end)
	return value
}

func DoFullTest(ctx context.Context, in *gen.TestReq, instance interface{}) (out *gen.TestResp, _ error) {
	out = &gen.TestResp{}
	httpClient := neko_common.CreateProxyHttpClient(instance)
	defer httpClient.CloseIdleConnections()

	// Latency
	var latency string
	if in.FullLatency {
		t, _ := speedtest.UrlTest(httpClient, in.Url, in.Timeout, speedtest.UrlTestStandard_RTT)
		out.Ms = t
		if t > 0 {
			latency = fmt.Sprint(t, "ms")
		} else {
			latency = "Error"
		}
	}

	// UDP Latency
	var udpLatency string
	if in.FullUdpLatency {
		ctx, cancel := context.WithTimeout(ctx, time.Second*3)
		result := make(chan string, 1) // buffered so the goroutine never leaks on timeout

		go func() {
			var startTime = time.Now()
			pc, err := neko_common.DialContext(ctx, instance, "udp", "8.8.8.8:53")
			if err == nil {
				defer pc.Close()
				// DialContext only cancels dialing. A silent DNS peer otherwise
				// leaves Read, its goroutine and the socket alive after timeout.
				stopClose := context.AfterFunc(ctx, func() { _ = pc.Close() })
				defer stopClose()
				dnsPacket, _ := hex.DecodeString("0000010000010000000000000377777706676f6f676c6503636f6d0000010001")
				_, err = pc.Write(dnsPacket)
				if err == nil {
					var buf [1400]byte
					_, err = pc.Read(buf[:])
				}
			}
			if err == nil {
				var endTime = time.Now()
				result <- fmt.Sprint(endTime.Sub(startTime).Abs().Milliseconds(), "ms")
			} else {
				log.Println("UDP Latency test error:", err)
				if ctx.Err() != nil {
					result <- "Timeout"
				} else {
					result <- "Error"
				}
			}
			close(result)
		}()

		select {
		case <-ctx.Done():
			udpLatency = "Timeout"
		case r := <-result:
			udpLatency = r
		}
		cancel()
	}

	// 入口 IP
	var in_ip string
	if in.FullInOut {
		_in_ip, err := net.ResolveIPAddr("ip", in.InAddress)
		if err == nil {
			in_ip = _in_ip.String()
		} else {
			in_ip = err.Error()
		}
	}

	// 出口 IP
	var out_ip string
	if in.FullInOut {
		out_ip = func() string {
			requestCtx, cancel := context.WithTimeout(ctx, 5*time.Second)
			defer cancel()
			req, err := http.NewRequestWithContext(requestCtx, "GET", "https://www.cloudflare.com/cdn-cgi/trace", nil)
			if err != nil {
				return "Error"
			}
			resp, err := httpClient.Do(req)
			if err != nil {
				return "Error"
			}
			defer resp.Body.Close()
			// The trace is tiny. An error page or faulty peer must not grow
			// memory without a bound or keep the cancelled test running.
			b, err := io.ReadAll(io.LimitReader(resp.Body, 4096))
			ip := getBetweenStr(string(b), "ip=", "\n")
			if err != nil || resp.StatusCode != http.StatusOK || net.ParseIP(ip) == nil {
				return "Error"
			}
			return ip
		}()
	}

	// 下载
	var speed string
	if in.FullSpeed {
		if in.FullSpeedTimeout <= 0 {
			in.FullSpeedTimeout = 30
		}

		ctx, cancel := context.WithTimeout(ctx, time.Second*time.Duration(in.FullSpeedTimeout))
		result := make(chan string, 1) // buffered so the goroutine never leaks on timeout

		// ЗАКРЫВАТЕЛЬ ТЕЛА СЮДА НЕ ВЫНОСИТСЯ.
		//
		// Здесь стояла переменная bodyClose: горутина её писала, а главная
		// читала после select — без синхронизации, то есть гонка, и `go test
		// -race` показывал бы её первым же прогоном. Причём ровно под
		// комментарием о том, что горутина больше не течёт.
		//
		// Выносить её не нужно вовсе: тело закрывает defer внутри самой
		// горутины, а прерывает чтение ctx, переданный в запрос. По истечении
		// срока cancel() обрывает io.Copy, defer закрывает тело — то же самое,
		// что делала внешняя переменная, только без общего состояния.
		go func() {
			req, err := http.NewRequestWithContext(ctx, "GET", in.FullSpeedUrl, nil)
			if err != nil {
				result <- "Error"
				return
			}
			resp, err := httpClient.Do(req)
			if err == nil && resp != nil && resp.Body != nil {
				defer resp.Body.Close()

				timeStart := time.Now()
				n, _ := io.Copy(io.Discard, resp.Body)
				timeEnd := time.Now()

				duration := math.Max(timeEnd.Sub(timeStart).Seconds(), 0.000001)
				resultSpeed := (float64(n) / duration) / MiB
				result <- fmt.Sprintf("%.2fMiB/s", resultSpeed)
			} else {
				result <- "Error"
			}
			close(result)
		}()

		select {
		case <-ctx.Done():
			speed = "Timeout"
		case s := <-result:
			speed = s
		}

		cancel()
	}

	fr := make([]string, 0)
	if latency != "" {
		fr = append(fr, fmt.Sprintf("Latency: %s", latency))
	}
	if udpLatency != "" {
		fr = append(fr, fmt.Sprintf("UDPLatency: %s", udpLatency))
	}
	if speed != "" {
		fr = append(fr, fmt.Sprintf("Speed: %s", speed))
	}
	if in_ip != "" {
		fr = append(fr, fmt.Sprintf("In: %s", in_ip))
	}
	if out_ip != "" {
		fr = append(fr, fmt.Sprintf("Out: %s", out_ip))
	}

	out.FullReport = strings.Join(fr, " / ")

	return
}
