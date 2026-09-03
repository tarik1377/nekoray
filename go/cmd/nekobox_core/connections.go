package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sort"
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

	// Network и Rule нужны, чтобы объяснить человеку ПОЧЕМУ, а не только КУДА.
	//
	// Оба поля приходят от ядра и до сих пор выбрасывались. Цена этого видна на
	// живом случае: у игры Squad ломался именно UDP к игровому серверу, тогда как
	// её же HTTPS шёл мимо туннеля правильно. По одному лишь Tag эти два случая
	// не различить — таблица показывала и то и другое вперемешку, и понять,
	// что сломано, было нельзя.
	//
	// Rule — это описание сработавшего правила словами самого ядра. Оно
	// служебное и человеку в таком виде не показывается, но именно по нему
	// отличается «программа не подошла ни под одно исключение» от «подошла, но
	// не под то».
	Network string `json:"Network"`
	Rule    string `json:"Rule"`
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


// shortProcess оставляет от пути одно имя файла.
//
// Полный путь превращает колонку в стену «C:\Program Files\...» и прячет то
// единственное слово, которое человек ищет. Разбор вынесен отдельно, потому что
// одно и то же имя нужно и живым соединениям, и закрытым: разъехавшись, эти два
// разбора дали бы одну и ту же программу под двумя именами, и сведение по имени
// молча развалилось бы.
// Сколько закрытых соединений отдавать и за какой срок.
//
// Ядро хранит последнюю тысячу. Отдавать её целиком раз в секунду — впустую
// гонять мегабайты ради таблицы, которая показывает живые. Две минуты и триста
// записей покрывают разбор одной поломки с запасом: человек называет программу,
// нажимает в ней то, что не выходит, и всё случившееся укладывается в этот срок.
const (
	closedWindow = 2 * time.Minute
	closedLimit  = 200
)

func shortProcess(path string) string {
	proc := path
	if slash := strings.LastIndexAny(proc, `\/`); slash >= 0 {
		proc = proc[slash+1:]
	}
	if paren := strings.Index(proc, " ("); paren > 0 {
		proc = proc[:paren] // отрезаем хвост « (пользователь)», который добавляет API
	}
	if proc == "" {
		return "—"
	}
	return proc
}

// closedConnections — недавно ЗАКРЫТЫЕ соединения работающего ядра.
//
// ЗАЧЕМ. Снимок Clash API отдаёт только живые. Соединение, прожившее меньше
// промежутка между опросами, не попадает в него никогда — а именно такими и
// бывают запросы игры к списку серверов. Без закрытых записей о них нельзя
// сказать даже того, каким путём они ушли.
//
// ЧЕГО ЗДЕСЬ НЕ БУДЕТ. Соединения, которые правило ЗАПРЕТИЛО. Запрет в ядре —
// действие правила, и обрабатывается оно раньше, чем заводится запись об учёте
// (route/route.go: ветка RuleActionReject возвращает управление до вызова
// tracker.RoutedConnection). Так что запрещённого не видно ни здесь, ни среди
// живых, и искать его надо в журнале, а не в соединениях. Знать это важно:
// иначе пустой список читается как «всё в порядке».
func closedConnections(now time.Time, maxAge time.Duration, limit int) []uiConnection {
	mgr := trafficManager.Load()
	if mgr == nil {
		return nil
	}
	all := mgr.ClosedConnections()
	out := make([]uiConnection, 0, limit)
	// С конца: там самые свежие, а именно они и интересны.
	for i := len(all) - 1; i >= 0 && len(out) < limit; i-- {
		m := all[i]
		if m == nil || now.Sub(m.ClosedAt) > maxAge {
			continue
		}
		dest := m.Metadata.Destination.String()
		rdest := ""
		if m.Metadata.Domain != "" {
			rdest = m.Metadata.Domain
		}
		proc := "—"
		if m.Metadata.ProcessInfo != nil {
			proc = shortProcess(m.Metadata.ProcessInfo.ProcessPath)
		}
		rule := ""
		if m.Rule != nil {
			rule = m.Rule.String()
		}
		out = append(out, uiConnection{
			Start:   m.CreatedAt.Unix(),
			End:     m.ClosedAt.Unix(),
			Tag:     m.Outbound,
			Dest:    dest,
			RDest:   rdest,
			Process: proc,
			Network: m.Metadata.Network,
			Rule:    rule,
		})
	}
	return out
}
func buildConnectionListJSON() string {
	conns, err := fetchClashConnections()
	if err != nil {
		return "[]"
	}

	list := make([]uiConnection, 0, len(conns))
	for i, c := range conns {
		// The tag we want is the one the routing rule picked (proxy / direct / bypass / block),
		// and that is the LAST element. sing-box walks outwards-in from the matched outbound
		// into the group members it resolves to, then reverses the slice
		// (experimental/clashapi/trafficontrol/tracker.go:139-164), so Chains[0] is the
		// innermost concrete outbound. Reading Chains[0] worked only while nothing wrapped the
		// outbound; with auto-failover on, every row's tag became the urltest member "g-<id>",
		// which the GUI cannot classify — the table filled up while the route strip still said
		// "Нет активных соединений".
		tag := ""
		if n := len(c.Chains); n > 0 {
			tag = c.Chains[n-1]
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

		proc := shortProcess(c.Metadata.ProcessPath)

		list = append(list, uiConnection{
			ID:      i,
			Start:   start,
			End:     0, // the Clash API only reports live connections
			Tag:     tag,
			Dest:    dest,
			RDest:   rdest,
			Process: proc,
			Network: c.Metadata.Network,
			Rule:    c.Rule,
		})
	}

	// Закрытые дописываем ПОСЛЕ живых и помечаем ненулевым End. Таблица
	// соединений в клиенте показывает только живые и отбирает их по End == 0;
	// разбору поломок нужны все. Так один вызов обслуживает обоих, и заводить
	// второй метод в протоколе не пришлось.
	closed := closedConnections(time.Now(), closedWindow, closedLimit)
	for i := range closed {
		closed[i].ID = len(list) + i
		list = append(list, closed[i])
	}

	// ПОРЯДОК ОБЯЗАН БЫТЬ ПОСТОЯННЫМ.
	//
	// Ядро отдаёт живые соединения в порядке обхода своей карты, а он у Go
	// намеренно непостоянен: один и тот же набор приходит каждый раз в новом
	// порядке. Таблица в клиенте перестраивается раз в секунду, и строки в ней
	// прыгали — найти нужную программу было нельзя, взгляд не успевал за
	// перестановкой. Это не косметика: список соединений смотрят как раз тогда,
	// когда что-то не работает и надо разглядеть одну строку.
	//
	// Сортируем по времени начала: новое появляется снизу, а всё, что человек
	// уже разглядывает, остаётся на месте. Совпадения разводим адресом и именем
	// программы — иначе одинаковое время снова дало бы качели.
	sort.SliceStable(list, func(i, j int) bool {
		if list[i].Start != list[j].Start {
			return list[i].Start < list[j].Start
		}
		if list[i].Process != list[j].Process {
			return list[i].Process < list[j].Process
		}
		return list[i].Dest < list[j].Dest
	})
	for i := range list {
		list[i].ID = i
	}

	b, err := json.Marshal(list)
	if err != nil {
		return "[]"
	}
	return string(b)
}
