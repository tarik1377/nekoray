package main

// МОЖНО ЛИ МЕНЯТЬ МАРШРУТЫ, НЕ ОБРЫВАЯ ЖИВОЙ ТУННЕЛЬ.
//
// Вопрос не праздный. Всё, что клиент умеет сегодня, требует перезапуска ядра:
// сохранил маршруты — ответь «Да» на «перезапустить прокси», и все соединения
// оборвались. Для игры это особенно обидно: правку вносят как раз тогда, когда
// игра запущена, и перезапуск роняет её вместе со всем остальным.
//
// Clash API здесь не поможет, и это проверено по исходнику вендоренного ядра:
// PUT /configs — пустое тело (experimental/clashapi/configs.go), PATCH /configs
// меняет только режим, /rules отдаётся только на чтение. Переставить правило
// или сменить его outbound на ходу нельзя.
//
// Остаётся ровно один законный путь: набор правил типа "local". Ядро читает его
// из файла и заводит наблюдателя за каталогом (route/rule/rule_set_local.go:64),
// а по изменению перечитывает и подменяет правила ПОД ЗАМКОМ (:145), не трогая
// ни туннель, ни остальную конфигурацию. Тогда в цепочке стоит неподвижная
// ссылка на набор, а меняется только состав файла.
//
// Этот набор проверок — ворота для такой возможности. Он ничего не подключает и
// ни на что не влияет: он отвечает на вопрос «а работает ли это вообще у нас,
// на Windows, в ЭТОЙ версии ядра». Если он красный, замысел «на лету» надо
// строить иначе, и узнать об этом лучше здесь, чем после половины работы.
//
// Заодно он сторожит вендоренную версию: когда sing-box поднимут и поведение
// изменится, отвалится сборка, а не человек посреди игры.
//
// Запуск (теги обязательны, иначе модуль не собирается):
//   go test -tags "with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_v2ray_api" -run TestLiveRuleSet ./...
//
// ---------------------------------------------------------------------------
// И ГЛАВНОЕ, ЧТО ЭТОТ НАБОР ИЗМЕРИЛ: ПОДМЕНА ГОНИТСЯ С МАРШРУТИЗАЦИЕЙ.
//
// Возможность есть, но пользоваться ей как есть НЕЛЬЗЯ. Под детектором гонок
// тот же самый сценарий разваливается сразу, на первой же подмене:
//
//   запись  rule_set_local.go:146  reloadRules — s.rules = rules, под s.access.Lock()
//                                  (goroutine наблюдателя за файлом)
//   чтение  rule_set_local.go:215  matchStatesWithBase — обход s.rules БЕЗ RLock
//                                  (goroutine маршрутизации, через Match)
//
// Соседние методы того же типа замок берут (Metadata :161, ExtractIPSet :167),
// а сопоставление — нет. Это недосмотр в самом sing-box, не в нашем коде, и он
// на горячем пути: Match зовётся на КАЖДОЕ соединение. То есть подмена файла
// при живом туннеле — это гонка на заголовке среза, а не безобидная операция.
//
// Воспроизводится за секунду (нужен cgo, значит C-компилятор в PATH):
//   CGO_ENABLED=1 go test -race -tags "..." -run TestLiveRuleSet ./...
//
// Поэтому набор намеренно оставлен ФУНКЦИОНАЛЬНЫМ и зелёным без -race: сборка
// в CI гоняет `go test` без него (build-nekoray-cmake.yml:86), и валить её
// чужим дефектом незачем. Знание о гонке живёт здесь, рядом с проверкой, а не
// в чьей-то памяти.
//
// Из этого следует для замысла «на лету»: до того как что-то подключать,
// гонку надо закрыть — двумя строками RLock/RUnlock в matchStatesWithBase,
// через local replace на исправленную копию либо через поднятую версию ядра,
// если её починят выше. Пока она открыта, частая автоматическая подмена
// недопустима, а редкая по нажатию человека лишь снижает вероятность.
// ---------------------------------------------------------------------------

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing-box/route/rule"
	"github.com/sagernet/sing/common/logger"
	N "github.com/sagernet/sing/common/network"
)

// seedFor — содержимое файла набора с одним правилом по имени программы.
func seedFor(processName string) string {
	return `{"version":3,"rules":[{"process_name":["` + processName + `"]}]}`
}

// writeAtomic кладёт файл так же, как это будет делать клиент: во временный
// файл рядом, затем переименованием поверх цели.
//
// Это не педантизм. Наблюдатель следит за КАТАЛОГОМ и срабатывает на создание и
// запись; частично записанный файл он успел бы прочитать в середине, и набор
// съехал бы на половину правил. Переименование в пределах тома атомарно, и
// читателю виден либо старый файл целиком, либо новый целиком.
func writeAtomic(t *testing.T, path, content string) {
	t.Helper()
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, []byte(content), 0o644); err != nil {
		t.Fatalf("временный файл: %v", err)
	}
	if err := os.Rename(tmp, path); err != nil {
		t.Fatalf("переименование поверх цели: %v", err)
	}
}

func processMeta(path string) *adapter.InboundContext {
	return &adapter.InboundContext{
		Network:     N.NetworkTCP,
		ProcessInfo: &adapter.ConnectionOwner{ProcessPath: path},
	}
}

// waitMatch ждёт, пока набор начнёт (или перестанет) совпадать с программой.
//
// Срок выбран с запасом: наблюдатель склеивает частые изменения задержкой в
// сотню миллисекунд, а на занятой машине файловое событие может задержаться.
// Пять секунд отделяют «медленно» от «не работает вовсе», и это единственное,
// что здесь нужно различить.
func waitMatch(t *testing.T, set *rule.LocalRuleSet, exe string, want bool) bool {
	t.Helper()
	deadline := time.Now().Add(5 * time.Second)
	for {
		if set.Match(processMeta(exe)) == want {
			return true
		}
		if time.Now().After(deadline) {
			return false
		}
		time.Sleep(20 * time.Millisecond)
	}
}

func TestLiveRuleSetReloadsWithoutRestart(t *testing.T) {
	dir := t.TempDir()
	path, err := filepath.Abs(filepath.Join(dir, "live-bypass.json"))
	if err != nil {
		t.Fatalf("абсолютный путь: %v", err)
	}

	const alpha = `C:\games\Alpha\Alpha-Win64-Shipping.exe`
	const beta = `C:\games\Beta\BetaLauncher-Shipping.exe`

	if err := os.WriteFile(path, []byte(seedFor("Alpha-Win64-Shipping.exe")), 0o644); err != nil {
		t.Fatalf("семя: %v", err)
	}

	ctx := context.Background()
	set, err := rule.NewLocalRuleSet(ctx, logger.NOP(), option.RuleSet{
		Type:         C.RuleSetTypeLocal,
		Tag:          "live-bypass",
		Format:       C.RuleSetFormatSource,
		LocalOptions: option.LocalRuleSet{Path: path},
	})
	if err != nil {
		t.Fatalf("набор не создался: %v", err)
	}
	defer set.Close()

	if err := set.StartContext(ctx, nil); err != nil {
		t.Fatalf("наблюдение не запустилось: %v", err)
	}

	// Исходное состояние. Без него зелёный результат ниже ничего не значил бы:
	// набор, который совпадает со всем подряд, «перезагрузился» бы и не меняясь.
	if !set.Match(processMeta(alpha)) {
		t.Fatal("семя не ловит того, кого названо ловить")
	}
	if set.Match(processMeta(beta)) {
		t.Fatal("семя ловит чужую программу — проверка ниже стала бы бессмысленной")
	}
	// Ядро включает поиск процесса по этому признаку набора; без него правила по
	// имени не сработали бы вовсе (route/router.go:119-130).
	if !set.Metadata().ContainsProcessRule {
		t.Fatal("набор не объявил себя содержащим правило по процессу")
	}

	// ---- ГЛАВНОЕ: подмена файла при живом наборе ----
	writeAtomic(t, path, seedFor("BetaLauncher-Shipping.exe"))

	if !waitMatch(t, set, beta, true) {
		t.Fatal("новое имя не подхватилось за пять секунд — живой подмены нет")
	}
	if !waitMatch(t, set, alpha, false) {
		t.Fatal("старое имя продолжает ловиться — набор не заменился, а дополнился")
	}

	// ---- БИТЫЙ ФАЙЛ НЕ ОБНУЛЯЕТ МАРШРУТИЗАЦИЮ ----
	//
	// Это важнее, чем кажется. Единственный способ остаться без связи здесь —
	// записать мусор и получить пустой набор. Ядро вместо этого пишет в журнал
	// «reload rule-set» и ОСТАВЛЯЕТ прежние правила в памяти
	// (rule_set_local.go:65-69). Обратная сторона того же свойства: отсутствие
	// ошибки нельзя считать признаком успеха — применение придётся подтверждать
	// наблюдением, а не молчанием.
	writeAtomic(t, path, `{"version":3,"rules":[{"process_name":`)
	time.Sleep(700 * time.Millisecond)
	if !set.Match(processMeta(beta)) {
		t.Fatal("битый файл стёр действующие правила — так можно оставить человека без маршрутов")
	}

	// ---- И ВОЗВРАТ РАБОТАЕТ ПОСЛЕ ОШИБКИ ----
	//
	// Наблюдатель, замолчавший после первой неудачи, дал бы худшее из положений:
	// на вид всё цело, а изменения больше не доезжают.
	writeAtomic(t, path, seedFor("Alpha-Win64-Shipping.exe"))
	if !waitMatch(t, set, alpha, true) {
		t.Fatal("после битого файла наблюдение больше не работает")
	}
}
