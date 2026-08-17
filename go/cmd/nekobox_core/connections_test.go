package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// Proves the new ListConnections path end to end at the core boundary: a sing-box-shaped
// /connections payload in, the exact JSON the GUI table expects out. Guards the two
// assumptions that would have silently emptied the tab — the tag is chains[last] (sing-box
// reverses the chain) and the process comes from processPath, basename only.
func TestBuildConnectionListJSON(t *testing.T) {
	const payload = `{"connections":[
      {"id":"a","start":"2026-08-17T20:00:00Z","chains":["proxy-detour","proxy"],
       "metadata":{"network":"tcp","host":"youtube.com","destinationIP":"142.250.1.1",
                   "destinationPort":"443","processPath":"C:\\Program Files\\Google\\Chrome\\chrome.exe (devops)"}},
      {"id":"b","start":"2026-08-17T20:00:01Z","chains":["bypass"],
       "metadata":{"network":"udp","host":"","destinationIP":"77.88.8.8",
                   "destinationPort":"53","processPath":"C:\\Windows\\svchost.exe"}}
    ]}`

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/connections" {
			t.Errorf("unexpected path %q", r.URL.Path)
		}
		_, _ = w.Write([]byte(payload))
	}))
	defer srv.Close()

	clashAPI.Lock()
	clashAPI.addr = strings.TrimPrefix(srv.URL, "http://")
	clashAPI.secret = ""
	clashAPI.Unlock()

	var got []uiConnection
	if err := json.Unmarshal([]byte(buildConnectionListJSON()), &got); err != nil {
		t.Fatalf("output is not valid JSON: %v", err)
	}
	if len(got) != 2 {
		t.Fatalf("want 2 connections, got %d", len(got))
	}

	if got[0].Tag != "proxy" { // chains[last], not chains[0] ("proxy-detour")
		t.Errorf("row0 Tag = %q, want proxy", got[0].Tag)
	}
	if got[0].Dest != "142.250.1.1:443" {
		t.Errorf("row0 Dest = %q", got[0].Dest)
	}
	if got[0].RDest != "youtube.com:443" {
		t.Errorf("row0 RDest = %q", got[0].RDest)
	}
	if got[0].Process != "chrome.exe" { // basename, " (user)" suffix stripped
		t.Errorf("row0 Process = %q, want chrome.exe", got[0].Process)
	}

	if got[1].Tag != "bypass" {
		t.Errorf("row1 Tag = %q, want bypass", got[1].Tag)
	}
	if got[1].RDest != "" { // no host sniffed -> empty, table shows IP only
		t.Errorf("row1 RDest = %q, want empty", got[1].RDest)
	}
	if got[1].Process != "svchost.exe" {
		t.Errorf("row1 Process = %q", got[1].Process)
	}
}

// A misconfigured or absent Clash API must degrade to an empty list, never a crash or a
// hang — the tab stays empty exactly as before instead of freezing the GUI poll loop.
func TestBuildConnectionListJSON_NoAPI(t *testing.T) {
	clashAPI.Lock()
	clashAPI.addr = ""
	clashAPI.secret = ""
	clashAPI.Unlock()

	if out := buildConnectionListJSON(); out != "[]" {
		t.Errorf("want [] with no API, got %q", out)
	}
}
