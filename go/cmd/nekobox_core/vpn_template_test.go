package main

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/include"
	"github.com/sagernet/sing-box/option"
	singjson "github.com/sagernet/sing/common/json"
)

/*
Шаблон внешнего туннеля обязан приниматься ЭТИМ ядром.

ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. res/vpn/sing-box-vpn.json написан под sing-box 1.9 и
живёт на полях, которых в 1.13 нет: inet4_address, sniff внутри inbound,
исходящие block и dns. На Windows и Linux туннель по умолчанию внутренний, и
шаблон там не читает никто; на macOS он — ЕДИНСТВЕННЫЙ путь к туннелю, и
читается он только у человека с маком. Ошибка в нём выглядит снаружи как
«TUN включился и сразу отвалился» — ровно та жалоба, с которой пришли.

Здесь шаблон заполняется так же, как это делает WriteVPNSingBoxConfig
(db/ConfigBuilder.cpp), прогоняется через преобразователь и отдаётся
настоящему разбору sing-box. Устройство tun при этом не открывается: box.New
строит экземпляр, а устройство берёт только Start. Значит проверка идёт на
любой машине и без прав.
*/

// filledVpnTemplate повторяет подстановки WriteVPNSingBoxConfig для значений
// по умолчанию на macOS: имени интерфейса нет, IPv6 выключен, поддельный DNS
// выключен, стек «system» — то, что стояло у тестировщика.
func filledVpnTemplate(t *testing.T) []byte {
	t.Helper()
	raw, err := os.ReadFile(filepath.Join("..", "..", "..", "res", "vpn", "sing-box-vpn.json"))
	if err != nil {
		t.Fatalf("шаблон не прочитан: %v", err)
	}
	r := strings.NewReplacer(
		"//%ROUTE_EXCLUDE_EXTRA%", "",
		"//%IPV6_ADDRESS%", "",
		"//%SOCKS_USER_PASS%", "",
		"//%PROCESS_NAME_RULE%", "",
		"//%CIDR_RULE%", "",
		"%MTU%", "1500",
		"%STACK%", "system",
		"//%TUN_NAME%", "",
		"%STRICT_ROUTE%", "false",
		"%FINAL_OUT%", "neko-socks",
		"%DNS_ADDRESS%", "local",
		"%FAKE_DNS_INBOUND%", "empty",
		"%PORT%", "2080",
	)
	filled := r.Replace(string(raw))
	if strings.Contains(filled, "%") {
		t.Fatalf("в заполненном шаблоне остался знак подстановки:\n%s", filled)
	}
	return []byte(filled)
}

func TestVpnTemplateIsAcceptedByThisCore(t *testing.T) {
	filled := filledVpnTemplate(t)

	transformed, err := transformConfigBytes(filled)
	if err != nil {
		t.Fatalf("преобразователь отказал: %v", err)
	}

	ctx := include.Context(context.Background())
	options, err := singjson.UnmarshalExtendedContext[option.Options](ctx, transformed)
	if err != nil {
		t.Fatalf("sing-box %s не принял шаблон туннеля после преобразования: %v\n\nконфиг:\n%s",
			"1.13", err, transformed)
	}

	instance, err := box.New(box.Options{Context: ctx, Options: options})
	if err != nil {
		t.Fatalf("box.New отказал на шаблоне туннеля: %v\n\nконфиг:\n%s", err, transformed)
	}
	_ = instance.Close()
}
