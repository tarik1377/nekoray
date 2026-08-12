package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"sync"
	"time"
)

// The connection list is read from the core's own Clash API rather than from
// sing-box internals: it is a stable published interface, it already carries the
// process name for every connection (which is the whole point of the view), and
// it cannot break when the vendored sing-box is bumped.
//
// ListConnections used to return nothing at all ("TODO upstream api"), so the
// Connections tab and the route summary were permanently empty — users saw
// "0 B / 0 B" and no way to tell what actually goes through the tunnel.

var clashAPI struct {
	sync.Mutex
	addr   string // host:port, empty when the API is not configured
	secret string
}

// rememberClashAPI extracts experimental.clash_api from the config the GUI sent,
// so ListConnections knows where to ask. Failing to parse is not an error worth
// surfacing — the tab simply stays empty, exactly as before.
func rememberClashAPI(coreConfig []byte) {
	var cfg struct {
		Experimental struct {
			ClashAPI struct {
				ExternalController string `json:"external_controller"`
				Secret             string `json:"secret"`
			} `json:"clash_api"`
		} `json:"experimental"`
	}
	clashAPI.Lock()
	defer clashAPI.Unlock()
	clashAPI.addr, clashAPI.secret = "", ""
	if json.Unmarshal(coreConfig, &cfg) != nil {
		return
	}
	clashAPI.addr = cfg.Experimental.ClashAPI.ExternalController
	clashAPI.secret = cfg.Experimental.ClashAPI.Secret
}

// clashConnection is the subset of the Clash API's /connections entry we use.
type clashConnection struct {
	ID       string   `json:"id"`
	Start    string   `json:"start"`
	Chains   []string `json:"chains"`
	Rule     string   `json:"rule"`
	Metadata struct {
		Network         string `json:"network"`
		Host            string `json:"host"`
		DestinationIP   string `json:"destinationIP"`
		DestinationPort string `json:"destinationPort"`
		// The API emits only processPath — there is no "process" field, despite what
		// the Clash schema suggests. Reading one would have silently shown nothing.
		ProcessPath string `json:"processPath"`
	} `json:"metadata"`
}

// uiConnection is what the GUI table expects. Field names are fixed by the C++
// side (mainwindow.cpp: Start/End/Tag/ID/Dest/RDest); Process is the new column.
type uiConnection struct {
	ID      int    `json:"ID"`
	Start   int64  `json:"Start"`
	End     int64  `json:"End"`
	Tag     string `json:"Tag"`
	Dest    string `json:"Dest"`
	RDest   string `json:"RDest"`
	Process string `json:"Process"`
}

func fetchClashConnections() ([]clashConnection, error) {
	clashAPI.Lock()
	addr, secret := clashAPI.addr, clashAPI.secret
	clashAPI.Unlock()
	if addr == "" {
		return nil, fmt.Errorf("clash api not configured")
	}

	req, err := http.NewRequest("GET", "http://"+addr+"/connections", nil)
	if err != nil {
		return nil, err
	}
	if secret != "" {
		req.Header.Set("Authorization", "Bearer "+secret)
	}
	// Short timeout: this runs on the GUI's polling loop, and a hung request
	// would freeze the connection list rather than just leaving it stale.
	client := &http.Client{Timeout: 2 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}
	var out struct {
		Connections []clashConnection `json:"connections"`
	}
	if err := json.Unmarshal(body, &out); err != nil {
		return nil, err
	}
	return out.Connections, nil
}

func buildConnectionListJSON() string {
	conns, err := fetchClashConnections()
	if err != nil {
		return "[]"
	}

	list := make([]uiConnection, 0, len(conns))
	for i, c := range conns {
		// The outbound tag is the last hop of the chain; Clash lists it first.
		tag := ""
		if len(c.Chains) > 0 {
			tag = c.Chains[0]
		}

		dest := c.Metadata.DestinationIP
		if c.Metadata.DestinationPort != "" {
			dest = dest + ":" + c.Metadata.DestinationPort
		}
		// Show the hostname when sniffing resolved one — an IP alone tells the
		// user nothing about which site a connection belongs to.
		rdest := ""
		if c.Metadata.Host != "" {
			rdest = c.Metadata.Host
			if c.Metadata.DestinationPort != "" {
				rdest = rdest + ":" + c.Metadata.DestinationPort
			}
		}

		start := time.Now().Unix()
		if t, e := time.Parse(time.RFC3339Nano, c.Start); e == nil {
			start = t.Unix()
		}

		// Show just the executable name: a full path turns the column into a wall of
		// "C:\Program Files\..." and hides the one word the user is looking for. The
		// path stays available as the cell's tooltip on the GUI side.
		proc := c.Metadata.ProcessPath
		if slash := strings.LastIndexAny(proc, `\/`); slash >= 0 {
			proc = proc[slash+1:]
		}
		if paren := strings.Index(proc, " ("); paren > 0 {
			proc = proc[:paren] // strip the " (user)" suffix the API appends
		}
		if proc == "" {
			proc = "—"
		}

		list = append(list, uiConnection{
			ID:      i,
			Start:   start,
			End:     0, // the Clash API only reports live connections
			Tag:     tag,
			Dest:    dest,
			RDest:   rdest,
			Process: proc,
		})
	}

	b, err := json.Marshal(list)
	if err != nil {
		return "[]"
	}
	return string(b)
}
