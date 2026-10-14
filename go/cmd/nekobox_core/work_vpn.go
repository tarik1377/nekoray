package main

import (
	"context"
	"encoding/json"
	"fmt"
	"runtime"

	"github.com/sagernet/sing-box/common/workvpn"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/service"
)

const workVPNOutboundTag = "greenrhythm-work-vpn"

// A core-only opt-in applies equally to app and standalone run configs. The
// legacy transformer, GUI and persistent config schema need no service names.
func extractWorkVPNOption(content []byte) ([]byte, bool, error) {
	var object map[string]json.RawMessage
	if err := json.Unmarshal(content, &object); err != nil {
		return nil, false, err
	}
	value, exists := object["windows_work_vpn"]
	if !exists {
		return content, false, nil
	}
	var enabled bool
	if string(value) == "null" {
		return nil, false, fmt.Errorf("windows_work_vpn must be a boolean")
	}
	if err := json.Unmarshal(value, &enabled); err != nil {
		return nil, false, fmt.Errorf("windows_work_vpn must be a boolean: %w", err)
	}
	delete(object, "windows_work_vpn")
	result, err := json.Marshal(object)
	return result, enabled, err
}

func configureWorkVPN(ctx context.Context, options *option.Options) error {
	if runtime.GOOS != "windows" {
		return fmt.Errorf("windows_work_vpn is only supported on Windows")
	}
	if options.Route == nil || !options.Route.AutoDetectInterface || options.Route.DefaultInterface != "" {
		return fmt.Errorf("windows_work_vpn requires route.auto_detect_interface and no route.default_interface")
	}
	for _, outbound := range options.Outbounds {
		if outbound.Tag == workVPNOutboundTag {
			return fmt.Errorf("reserved work VPN outbound tag already used")
		}
	}
	var excluded []string
	for _, inbound := range options.Inbounds {
		if inbound.Type == C.TypeTun {
			if tunOptions, ok := inbound.Options.(*option.TunInboundOptions); ok && tunOptions.InterfaceName != "" {
				excluded = append(excluded, tunOptions.InterfaceName)
			}
		}
	}
	service.MustRegister(ctx, workvpn.New(excluded))
	options.Outbounds = append(options.Outbounds, option.Outbound{Type: C.TypeDirect, Tag: workVPNOutboundTag, Options: &option.DirectOutboundOptions{}})
	rule := option.Rule{Type: C.RuleTypeDefault, DefaultOptions: option.DefaultRule{
		RawDefaultRule: option.RawDefaultRule{WindowsWorkVPN: true},
		RuleAction:     option.RuleAction{Action: C.RuleActionTypeRoute, RouteOptions: option.RouteActionOptions{Outbound: workVPNOutboundTag}},
	}}
	// Leading packet guards, DNS interception and sniffing retain their order.
	// Corporate routes take precedence over ordinary proxy/process selection.
	index := 0
	for index < len(options.Route.Rules) {
		r := options.Route.Rules[index]
		action := r.DefaultOptions.Action
		if r.Type == C.RuleTypeLogical {
			action = r.LogicalOptions.Action
		}
		if action != C.RuleActionTypeSniff && action != C.RuleActionTypeHijackDNS && action != C.RuleActionTypeReject && action != C.RuleActionTypeRouteOptions {
			break
		}
		index++
	}
	rules := make([]option.Rule, 0, len(options.Route.Rules)+1)
	rules = append(rules, options.Route.Rules[:index]...)
	rules = append(rules, rule)
	rules = append(rules, options.Route.Rules[index:]...)
	options.Route.Rules = rules
	return nil
}
