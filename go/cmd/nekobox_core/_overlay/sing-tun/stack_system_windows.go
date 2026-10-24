// overlay-base: github.com/sagernet/sing-tun v0.8.12-0.20260727151122-3a09076491df
package tun

import (
	"errors"
	"log"
	"os"
	"path/filepath"

	ole "github.com/go-ole/go-ole"
	"github.com/sagernet/sing-tun/internal/winfw"

	"golang.org/x/sys/windows"
)

func fixWindowsFirewall() error {
	absPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		return err
	}
	rule := winfw.FWRule{
		Name:            "sing-tun (" + absPath + ")",
		ApplicationName: absPath,
		Enabled:         true,
		Protocol:        winfw.NET_FW_IP_PROTOCOL_TCP,
		Direction:       winfw.NET_FW_RULE_DIR_IN,
		Action:          winfw.NET_FW_ACTION_ALLOW,
		Profiles:        winfw.NET_FW_PROFILE2_ALL,
	}
	_, err = winfw.FirewallRuleAddAdvanced(rule)
	if err != nil {
		log.Printf("System TUN: Windows Firewall setup failed: %v", err)
		var detail *ole.OleError
		if errors.As(err, &detail) {
			log.Printf("System TUN: firewall HRESULT=%#x program=%q", detail.Code(), absPath)
			if info, ok := detail.SubError().(ole.EXCEPINFO); ok {
				log.Printf("System TUN: firewall exception SCODE=%#x", info.SCODE())
			}
		}
	}
	return err
}

func retryableListenError(err error) bool {
	return errors.Is(err, windows.WSAEADDRNOTAVAIL)
}
