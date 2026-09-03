package grpc_server

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"grpc_server/gen"
	"io"
	"net/http"
	"net/url"
	"os"
	"path"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
)

/*
 * ОТКУДА КЛИЕНТ УЗНАЁТ О ВЕРСИЯХ.
 *
 * Раньше — из релизов на GitHub. Теперь из своего манифеста на verdantvibe.ru.
 * Причина простая и не про красоту: GitHub у заметной части наших людей просто
 * не открывается, и «проверить обновление» у них молча не работало никогда.
 *
 * Формат ответа — тот же, что читает приложение под Android, плюс платформа и
 * размер: см. src/server/controller/appRelease.js на сайте.
 */

/** Куда клиент вообще имеет право пойти за пакетом. Одно место, один хост. */
const manifestHost = "verdantvibe.ru"

var update_download_url string

/** sha256 из манифеста. Пустая строка означает «проверять нечем» — это отказ. */
var update_expected_sha string

/**
 * Наш ли это адрес.
 *
 * Проверяется РАЗБОРОМ, а не префиксом строки. Проверка вида
 * strings.HasPrefix(u, "https://verdantvibe.ru") пропускает
 * https://verdantvibe.ru.evil.tld — и пропускает молча, потому что выглядит
 * ровно так же, как правильная.
 */
func isOurUrl(raw string) bool {
	u, err := url.Parse(raw)
	if err != nil {
		return false
	}
	return u.Scheme == "https" && u.Hostname() == manifestHost
}

/** Похоже ли это на sha256. Шестьдесят четыре шестнадцатеричных, не меньше. */
func looksLikeSha256(s string) bool {
	if len(s) != 64 {
		return false
	}
	_, err := hex.DecodeString(s)
	return err == nil
}

/** Строка платформы в манифесте. Пусто — сборка не для того, что мы раздаём. */
func manifestPlatform() string {
	switch {
	case runtime.GOOS == "windows" && runtime.GOARCH == "amd64":
		return "windows-x64"
	case runtime.GOOS == "linux" && runtime.GOARCH == "amd64":
		return "linux-x64"
	case runtime.GOOS == "darwin":
		return "macos-" + runtime.GOARCH
	}
	return ""
}

/** Ответ /api/app/version/<platform>. Поля — как их отдаёт сайт. */
// flexInt64 — целое, которое согласны получить и числом, и строкой.
//
// ЗАЧЕМ ТЕРПИМОСТЬ. Манифест приходит снаружи, и его поля собираются на другой
// стороне из ответа базы. Postgres отдаёт bigint СТРОКОЙ, и стоило записи о
// выпуске появиться с размером файла, как разбор манифеста стал падать целиком:
// «json: cannot unmarshal string into Go struct field releaseManifest.sizeBytes
// of type int64». Человек при этом видел не «обновлений нет» и не «сайт
// недоступен», а строку на английском про Go — и обновиться не мог вовсе.
//
// Цена строгости здесь несоразмерна пользе: размер файла — справочное число,
// а из-за его типа переставал работать весь путь обновления. Проверка
// целостности держится на sha256, и она осталась строгой.
type flexInt64 int64

func (v *flexInt64) UnmarshalJSON(data []byte) error {
	s := strings.TrimSpace(string(data))
	if s == "null" || s == `""` || s == "" {
		*v = 0
		return nil
	}
	s = strings.Trim(s, `"`)
	n, err := strconv.ParseInt(s, 10, 64)
	if err != nil {
		return err
	}
	*v = flexInt64(n)
	return nil
}

type releaseManifest struct {
	// versionCode тоже терпимый: он приходит из той же таблицы и однажды
	// приедет строкой по той же причине.
	VersionCode flexInt64 `json:"versionCode"`
	Version     string    `json:"version"`
	Url         string    `json:"url"`
	Sha256      string    `json:"sha256"`
	SizeBytes   flexInt64 `json:"sizeBytes"`
	Notes       string    `json:"notes"`
	Mandatory   bool      `json:"mandatory"`
	Platform    string    `json:"platform"`
}

/**
 * Забрать и ПРОВЕРИТЬ манифест.
 *
 * Отказ здесь всегда возвращается ошибкой, а не «обновлений нет». Разница не
 * косметическая: «обновлений нет» человек принимает за ответ и уходит, а
 * означать это может что угодно — от сломанного DNS до подменённого ответа.
 */
func fetchManifest(ctx context.Context, client *http.Client, platform string) (*releaseManifest, error) {
	if platform == "" {
		return nil, errors.New("Not official support platform")
	}

	req, err := http.NewRequestWithContext(ctx, "GET",
		"https://"+manifestHost+"/api/app/version/"+platform, nil)
	if err != nil {
		return nil, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	// 204 — платформа известна, но выпусков под неё ещё нет. Это единственный
	// случай, когда «обновлений нет» — правда.
	if resp.StatusCode == http.StatusNoContent {
		return nil, nil
	}
	if resp.StatusCode != http.StatusOK {
		return nil, errors.New("сайт ответил " + strconv.Itoa(resp.StatusCode))
	}

	var m releaseManifest
	// Предел на размер: манифест — сотня байт, и читать без границы то, что
	// пришло из сети, незачем ни при каких обстоятельствах.
	if err := json.NewDecoder(io.LimitReader(resp.Body, 64*1024)).Decode(&m); err != nil {
		return nil, err
	}

	// ПРОВЕРКИ ДО ПОКАЗА ЧЕЛОВЕКУ, а не перед скачиванием. Предложение обновиться
	// — это уже обещание; предлагать то, что заведомо не пройдёт проверку, значит
	// подвести человека дважды.
	if !isOurUrl(m.Url) {
		return nil, errors.New("ссылка на пакет ведёт не на наш сайт — обновление отменено")
	}
	if !looksLikeSha256(m.Sha256) {
		return nil, errors.New("контрольная сумма в манифесте не читается — обновление отменено")
	}
	return &m, nil
}

func (s *BaseServer) Update(ctx context.Context, in *gen.UpdateReq) (*gen.UpdateResp, error) {
	ret := &gen.UpdateResp{}

	// НАШ САЙТ БЕРЁМ НАПРЯМУЮ, А ЧЕРЕЗ ТУННЕЛЬ — ТОЛЬКО ЕСЛИ НАПРЯМУЮ НЕ ВЫШЛО.
	//
	// CreateProxyHttpClient маршрутизацию не спрашивает: он дёргает исходящий
	// «proxy» напрямую, поэтому правило «российские домены — мимо туннеля» к
	// обновлению не применялось НИКОГДА. Наш сайт стоит в России, и его тянули
	// кругом через зарубежный выход — в журнале это видно как строку без номера
	// соединения и без входящего. Кончалось это «net/http: TLS handshake
	// timeout»: обновиться было нельзя, а причина выглядела поломкой сайта.
	//
	// Порядок именно такой. Прямой путь короче, дешевле и не зависит от того,
	// поднят ли туннель вообще. Но у части людей наш сайт закрыт провайдером —
	// для них остаётся прежний путь, вторым заходом.
	client := &http.Client{Timeout: 30 * time.Second}
	fallback := neko_common.CreateProxyHttpClient(neko_common.GetCurrentInstance())

	if in.Action == gen.UpdateAction_Check { // Check update
		ctx, cancel := context.WithTimeout(ctx, time.Second*10)
		defer cancel()

		m, err := fetchManifest(ctx, client, manifestPlatform())
		if err != nil && fallback != nil {
			// Прямой путь не вышел — пробуем через туннель, молча: человеку важен
			// итог, а не то, какой дорогой мы его добыли.
			m, err = fetchManifest(ctx, fallback, manifestPlatform())
			if err == nil {
				client = fallback
			}
		}
		if err != nil {
			// Именно ошибка, а не «вы актуальны». Несостоявшаяся проверка и
			// отсутствие новой версии — разные вещи, и путать их нельзя: во
			// втором случае человек спокойно уходит, в первом ему надо знать.
			ret.Error = err.Error()
			return ret, nil
		}
		if m == nil {
			return ret, nil // Выпусков под эту платформу пока нет
		}

		// shouldUpdate НЕ ТРОГАЕТСЯ: она обкатана, знает про застрявшие сборки с
		// версией вида 4.0.1-2024-12-12 и вытаскивает их. Манифест отдаёт версию
		// без «v», а parseVer этот префикс всё равно снимает.
		if !shouldUpdate(m.Version, neko_common.Version_neko) {
			return ret, nil // Уже свежая
		}

		update_download_url = m.Url
		update_expected_sha = m.Sha256

		// Имя берётся из адреса, а не придумывается: оно показывается человеку и
		// должно совпадать с тем, что он увидит в папке загрузок.
		if u, err := url.Parse(m.Url); err == nil {
			ret.AssetsName = path.Base(u.Path)
		}
		ret.DownloadUrl = m.Url
		ret.ReleaseUrl = "https://" + manifestHost + "/apps"
		ret.ReleaseNote = m.Notes
		// Предварительных выпусков в нашей раздаче нет: манифест отдаёт ровно
		// одну текущую сборку на платформу. Признак остаётся ложным всегда, и
		// галка «проверять предварительные» на этот путь больше не влияет.
		ret.IsPreRelease = false
		return ret, nil
	} else { // Download update
		if update_download_url == "" || update_expected_sha == "" {
			// Оба условия — одно и то же состояние: проверка не проходила или не
			// прошла. Скачивать при этом нечего и не с чем сверять.
			ret.Error = "No update URL"
			return ret, nil
		}
		// Повторная проверка адреса перед походом. Между проверкой и скачиванием
		// проходит время и одно человеческое действие, а переменная — пакетная:
		// стоить эта строчка ничего не стоит, а закрывает целый класс «как оно
		// туда попало».
		if !isOurUrl(update_download_url) {
			ret.Error = "ссылка на пакет ведёт не на наш сайт — обновление отменено"
			return ret, nil
		}

		/*
		 * СКАЧИВАНИЕ ИДЁТ КЛИЕНТОМ БЕЗ ОБЩЕГО ТАЙМАУТА, и это не мелочь.
		 *
		 * neko_common.CreateProxyHttpClient отдаёт клиент с Timeout = 30 секунд,
		 * и этот таймаут в Go покрывает не соединение, а ВЕСЬ ответ, включая
		 * чтение тела. Пакет весит около шестидесяти мегабайт, то есть тем же
		 * клиентом он докачался бы только при устойчивых двух мегабайтах в
		 * секунду — и это через туннель. На любом более медленном канале io.Copy
		 * обрывается, файл удаляется, и человек получает не «медленно», а сырую
		 * английскую строку про context deadline exceeded.
		 *
		 * Копия клиента с обнулённым Timeout: транспорт (а с ним и маршрут через
		 * туннель) остаётся прежним, снимается только потолок на весь ответ.
		 * Ограничение сверху при этом не исчезает — им остаётся ctx, который
		 * отменяется вместе с запросом от интерфейса.
		 */
		download := *client
		download.Timeout = 0

		req, _ := http.NewRequestWithContext(ctx, "GET", update_download_url, nil)
		resp, err := download.Do(req)
		if err != nil && fallback != nil {
			// Тот же запасной путь, что и у проверки. Скачивание приходит
			// ОТДЕЛЬНЫМ вызовом, поэтому выбор, сделанный при проверке, сюда не
			// доезжает — и без этой ветки человек, у которого наш сайт закрыт
			// провайдером, увидел бы новую версию и не смог бы её взять.
			viaTunnel := *fallback
			viaTunnel.Timeout = 0
			req2, _ := http.NewRequestWithContext(ctx, "GET", update_download_url, nil)
			resp, err = viaTunnel.Do(req2)
		}
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer resp.Body.Close()
		if resp.StatusCode != http.StatusOK {
			ret.Error = "сайт ответил " + strconv.Itoa(resp.StatusCode)
			return ret, nil
		}

		// Кладётся как greenrhythm.zip — этого имени ждёт распаковщик, и другого
		// формата он не понимает.
		const zipPath = "../greenrhythm.zip"
		f, err := os.OpenFile(zipPath, os.O_TRUNC|os.O_CREATE|os.O_RDWR, 0644)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer f.Close()

		// Сумма считается на лету: файл под шестьдесят мегабайт, и читать его
		// вторым проходом ради того же числа незачем.
		h := sha256.New()
		_, err = io.Copy(io.MultiWriter(f, h), resp.Body)
		if err != nil {
			f.Close()
			os.Remove(zipPath)
			ret.Error = err.Error()
			return ret, nil
		}
		f.Sync()

		/*
		 * ПРОВЕРКА СУММЫ ОБЯЗАТЕЛЬНА, А НЕ «ЕСЛИ НАШЛАСЬ».
		 *
		 * Прежде сумма лежала отдельным файлом рядом с релизом, и её отсутствие
		 * означало «проверять нечем» — то есть установку без проверки. Теперь
		 * она приходит в самом манифесте и без неё сюда не доходят вовсе:
		 * несостоявшаяся проверка равна проваленной.
		 *
		 * Честно про предел: сумма едет по тому же TLS и с того же сайта, что и
		 * файл. Она ловит битую закачку и подмену по дороге, но не сайт,
		 * которым завладели. Настоящее лечение — подпись офлайновым ключом,
		 * открытая часть которого лежит в клиенте; называть sha256 подписью
		 * нечестно, и здесь она ею не называется.
		 */
		got := hex.EncodeToString(h.Sum(nil))
		if !strings.EqualFold(got, update_expected_sha) {
			// Файл убирается сразу. Оставленный, он дождётся распаковщика,
			// который проверок не делает вовсе.
			f.Close()
			os.Remove(zipPath)
			ret.Error = "контрольная сумма пакета не сошлась — обновление отменено"
			return ret, nil
		}
	}

	return ret, nil
}

// parseVer parses a clean GreenRhythm tag "vX.Y.Z" into numeric parts.
// Returns ok=false for anything that is NOT a clean dotted-numeric version —
// notably the legacy upstream string "4.0.1-2024-12-12" (it carries a suffix).
func parseVer(s string) ([]int, bool) {
	s = strings.TrimPrefix(s, "v")
	if s == "" || strings.ContainsAny(s, "-+") {
		return nil, false
	}
	parts := strings.Split(s, ".")
	nums := make([]int, 0, len(parts))
	for _, p := range parts {
		n, err := strconv.Atoi(p)
		if err != nil {
			return nil, false
		}
		nums = append(nums, n)
	}
	return nums, true
}

// shouldUpdate reports whether release tag `latest` should be offered to a client
// running `cur`. If `cur` is not a clean vX.Y.Z (e.g. a stale upstream build that
// reports "4.0.1-2024-12-12"), always offer the latest clean release so those
// stragglers get rescued instead of being trapped by a higher legacy major.
// If `latest` itself is unparseable, never offer (avoid junk prompts).
func shouldUpdate(latest, cur string) bool {
	lv, lok := parseVer(latest)
	if !lok {
		return false
	}
	cv, cok := parseVer(cur)
	if !cok {
		return true
	}
	for i := 0; i < len(lv) || i < len(cv); i++ {
		var a, b int
		if i < len(lv) {
			a = lv[i]
		}
		if i < len(cv) {
			b = cv[i]
		}
		if a != b {
			return a > b
		}
	}
	return false
}
