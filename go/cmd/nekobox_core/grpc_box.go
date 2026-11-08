package main

import (
	"context"
	"encoding/json"
	"errors"
	"log"

	"grpc_server"
	"grpc_server/gen"

	"github.com/matsuridayo/libneko/neko_common"
	"github.com/matsuridayo/libneko/speedtest"
	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/experimental/v2rayapi"
)

type server struct {
	grpc_server.BaseServer
}

func (s *server) SiteTools(ctx context.Context, in *gen.SiteToolsReq) (*gen.SiteToolsResp, error) {
	out := &gen.SiteToolsResp{}
	if ctx.Err() != nil {
		out.Error = "request_cancelled"
		return out, nil
	}
	services := activeSiteServices.Load()
	if services == nil || services.ctx.Err() != nil {
		out.Error = "site_services_unavailable"
		return out, nil
	}
	if len(in.PayloadJson) > 2<<20 {
		out.Error = "request_too_large"
		return out, nil
	}
	var request struct {
		Scope   string `json:"scope"`
		URL     string `json:"url"`
		ID      string `json:"id"`
		Library string `json:"library"`
		ScanID  string `json:"scan_id"`
	}
	if in.PayloadJson != "" && json.Unmarshal([]byte(in.PayloadJson), &request) != nil {
		out.Error = "invalid_request"
		return out, nil
	}
	var result any
	var err error
	switch in.Action {
	case "status":
		result = services.status()
	case "apply":
		result, err = services.apply(request.Scope, request.Library, request.ScanID)
	case "scan":
		result, err = services.startScan(request.Scope, request.URL)
	case "scan_status":
		result, err = services.scanStatus(request.ID, false)
	case "cancel":
		result, err = services.scanStatus(request.ID, true)
	default:
		err = errors.New("unsupported_site_action")
	}
	if err != nil {
		out.Error = err.Error()
		return out, nil
	}
	data, err := json.Marshal(result)
	if err != nil {
		out.Error = "result_unavailable"
	} else {
		out.ResultJson = string(data)
	}
	return out, nil
}

func (s *server) Start(ctx context.Context, in *gen.LoadConfigReq) (out *gen.ErrorResp, _ error) {
	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = err.Error()
			instance = nil
		}
	}()

	if neko_common.Debug {
		log.Println("Start:", in.CoreConfig)
	}

	if instance != nil {
		err = errors.New("instance already started")
		return
	}

	// V2Ray stats outbounds are now expected to be configured in the JSON config
	// passed from the C++ side, so no manual injection is needed.
	rememberClashAPI([]byte(in.CoreConfig))
	instance, instance_cancel, err = nekoCreateWithTunLookup([]byte(in.CoreConfig), tunSystemLookupForRequest(ctx))
	if err == nil && instance != nil {
		if item, ok := instance.Outbound().Outbound(siteServicesTag); ok {
			if tools, ok := item.(*siteServicesOutbound); ok {
				activeSiteServices.Store(tools)
			}
		}
	}

	return
}

func (s *server) Stop(ctx context.Context, in *gen.EmptyReq) (out *gen.ErrorResp, _ error) {
	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = err.Error()
		}
	}()

	if instance == nil {
		return
	}

	instance_cancel()
	activeSiteServices.Store(nil)
	instance.Close()

	instance = nil
	statsService.Store(nil)

	return
}

func (s *server) Test(ctx context.Context, in *gen.TestReq) (out *gen.TestResp, _ error) {
	var err error
	out = &gen.TestResp{Ms: 0}

	defer func() {
		if err != nil {
			out.Error = err.Error()
		}
	}()

	if in.Mode == gen.TestMode_UrlTest {
		var i *box.Box
		var cancel context.CancelFunc
		if in.Config != nil {
			// Test instance
			i, cancel, err = nekoCreate([]byte(in.Config.CoreConfig))
			if i != nil {
				defer i.Close()
				defer cancel()
			}
			if err != nil {
				return
			}
		} else {
			// Test running instance
			i = instance
			if i == nil {
				return
			}
		}
		// Latency
		out.Ms, err = speedtest.UrlTest(nekoCreateProxyHttpClient(i), in.Url, in.Timeout, speedtest.UrlTestStandard_RTT)
	} else if in.Mode == gen.TestMode_TcpPing {
		out.Ms, err = speedtest.TcpPing(in.Address, in.Timeout)
	} else if in.Mode == gen.TestMode_FullTest {
		i, cancel, err := nekoCreate([]byte(in.Config.CoreConfig))
		if i != nil {
			defer i.Close()
			defer cancel()
		}
		if err != nil {
			return
		}
		return grpc_server.DoFullTest(ctx, in, i)
	}

	return
}

func (s *server) QueryStats(ctx context.Context, in *gen.QueryStatsReq) (out *gen.QueryStatsResp, _ error) {
	out = &gen.QueryStatsResp{}

	ss := statsService.Load()
	if ss == nil {
		return
	}

	// Counter name format from sing-box v2rayapi: outbound>>>{tag}>>>traffic>>>{uplink|downlink}.
	// in.Direct is "uplink"/"downlink" (TrafficLooper). Reset_ on read: the GUI treats each
	// returned value as the per-interval delta (rate = value*1000/interval), so reset to zero.
	name := "outbound>>>" + in.Tag + ">>>traffic>>>" + in.Direct
	resp, err := ss.GetStats(ctx, &v2rayapi.GetStatsRequest{Name: name, Reset_: true})
	if err == nil && resp != nil && resp.Stat != nil {
		out.Traffic = resp.Stat.Value
	}

	return
}

func (s *server) ListConnections(ctx context.Context, in *gen.EmptyReq) (*gen.ListConnectionsResp, error) {
	if instance == nil {
		return &gen.ListConnectionsResp{NekorayConnectionsJson: "[]"}, nil
	}
	return &gen.ListConnectionsResp{NekorayConnectionsJson: buildConnectionListJSON()}, nil
}
