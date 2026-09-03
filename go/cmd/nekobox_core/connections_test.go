package main

import (
	"encoding/json"
	"time"
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
      {"id":"a","start":"2026-08-17T20:00:00Z","chains":["proxy-detour","proxy"],"rule":"final",
       "metadata":{"network":"tcp","host":"youtube.com","destinationIP":"142.250.1.1",
                   "destinationPort":"443","processPath":"C:\\Program Files\\Google\\Chrome\\chrome.exe (devops)"}},
      {"id":"b","start":"2026-08-17T20:00:01Z","chains":["bypass"],"rule":"process_name=svchost.exe",
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
	// Сеть и правило переносятся до интерфейса: без них нельзя объяснить
	// человеку, ПОЧЕМУ программа пошла тем путём, а не только куда. Ровно эта
	// разница отделяла «у игры сломан UDP» от «у игры всё в порядке по HTTPS».
	if got[0].Network != "tcp" || got[1].Network != "udp" {
		t.Errorf("Network = %q / %q, want tcp / udp", got[0].Network, got[1].Network)
	}
	if got[0].Rule != "final" {
		t.Errorf("row0 Rule = %q, want final", got[0].Rule)
	}
	if got[1].Rule != "process_name=svchost.exe" {
		t.Errorf("row1 Rule = %q", got[1].Rule)
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

// Разбор имени общий для живых и закрытых соединений. Разъехавшись, он дал бы
// одной программе два имени, и сведение по имени развалилось бы молча — а
// именно на сведении по имени стоит весь разбор поломок.
func TestShortProcess(t *testing.T) {
	cases := []struct{ in, want string }{
		{`C:\SteamLibrary\Squad\SquadGame-Win64-Shipping.exe`, "SquadGame-Win64-Shipping.exe"},
		{`C:\Program Files\Google\Chrome\chrome.exe (devops)`, "chrome.exe"},
		{"/usr/bin/curl", "curl"},
		{"", "—"},
	}
	for _, c := range cases {
		if got := shortProcess(c.in); got != c.want {
			t.Errorf("shortProcess(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

// Без работающего ядра список закрытых пуст, а не падает: ListConnections зовут
// и когда ядро остановлено, и при выключенном clash_api.
func TestClosedConnectionsWithoutCore(t *testing.T) {
	trafficManager.Store(nil)
	if got := closedConnections(time.Now(), closedWindow, closedLimit); got != nil {
		t.Errorf("без ядра ожидался пустой список, получено %d записей", len(got))
	}
}
