package robloxauto

import (
	"context"
	"crypto/tls"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/url"
	"strings"
	"time"
)

type Dial func(context.Context, Path, string, string) (net.Conn, error)

func HTTPSProbe(dial Dial) Probe {
	return httpsProbe(dial, &tls.Config{MinVersion: tls.VersionTLS12})
}

// WebsiteProbe is limited to a separately validated user-selected catalog.
// It never probes captured URLs, follows redirects or forwards account data.
func WebsiteProbe(dial Dial, urlFor func(string) string) Probe {
	return policyProbe(dial, &tls.Config{MinVersion: tls.VersionTLS12}, urlFor)
}

// TLS roots are injectable only by same-package owned-server tests.
func httpsProbe(dial Dial, tlsOptions *tls.Config) Probe {
	return policyProbe(dial, tlsOptions, ProbeURL)
}
func policyProbe(dial Dial, tlsOptions *tls.Config, urlFor func(string) string) Probe {
	return func(ctx context.Context, path Path, host string) Health {
		requestURL := urlFor(host)
		if requestURL == "" {
			return Unknown
		}
		transport := &http.Transport{Proxy: nil, DisableKeepAlives: true, MaxResponseHeaderBytes: 16384,
			TLSHandshakeTimeout: 2 * time.Second, TLSClientConfig: tlsOptions.Clone(),
			DialContext: func(ctx context.Context, network, address string) (net.Conn, error) {
				return dial(ctx, path, network, address)
			}}
		defer transport.CloseIdleConnections()
		client := &http.Client{Transport: transport, Timeout: 3 * time.Second,
			CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
		request, err := http.NewRequestWithContext(ctx, http.MethodGet, requestURL, nil)
		if err != nil {
			return Unknown
		}
		request.Header.Set("User-Agent", "GreenRhythm-Availability/1")
		response, err := client.Do(request)
		if err != nil {
			return Failed
		}
		defer response.Body.Close()
		body, err := io.ReadAll(io.LimitReader(response.Body, 4096))
		if err != nil {
			return Failed
		}
		classificationHost := host
		if parsed, err := url.Parse(requestURL); err == nil {
			classificationHost = parsed.Hostname()
		}
		return Classify(classificationHost, response.StatusCode, response.Header.Get("Content-Type"), body)
	}
}

// 403/429 do not prove filtering: they can mean permissions, rate limits or
// CAPTCHA. Never retry the user's authenticated request via another path.
func Classify(host string, status int, contentType string, body []byte) Health {
	if status == 403 || status == 429 {
		return Unknown
	}
	if status >= 500 {
		return Failed
	}
	if host == "users.roblox.com" && status == 200 {
		var user struct {
			ID int `json:"id"`
		}
		if json.Unmarshal(body, &user) != nil || user.ID != 1 {
			return Unknown
		}
		return Working
	}
	if host == "setup.rbxcdn.com" && status == 200 {
		if !strings.HasPrefix(strings.TrimSpace(string(body)), "version-") {
			return Unknown
		}
		return Working
	}
	if status >= 200 && status < 400 {
		return Working
	}
	if (status == 400 || status == 401 || status == 404 || status == 405) && strings.Contains(strings.ToLower(contentType), "json") {
		var reply struct {
			Errors []json.RawMessage `json:"errors"`
		}
		if json.Unmarshal(body, &reply) == nil && len(reply.Errors) > 0 {
			return Working
		}
	}
	return Unknown
}
