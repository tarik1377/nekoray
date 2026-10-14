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
	"sync"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
	releaseauth "greenrhythm_release"
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

var verifiedUpdate *releaseauth.Manifest
var updateMu sync.Mutex

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
	VersionCode        flexInt64 `json:"versionCode"`
	Version            string    `json:"version"`
	Url                string    `json:"url"`
	Sha256             string    `json:"sha256"`
	SizeBytes          flexInt64 `json:"sizeBytes"`
	Notes              string    `json:"notes"`
	Mandatory          bool      `json:"mandatory"`
	Platform           string    `json:"platform"`
	SignatureAlgorithm string    `json:"signatureAlgorithm"`
	SigningKeyID       string    `json:"signingKeyId"`
	Signature          string    `json:"signature"`
}

func (m *releaseManifest) signedManifest() *releaseauth.Manifest {
	return &releaseauth.Manifest{VersionCode: int64(m.VersionCode), Version: m.Version, URL: m.Url, SHA256: m.Sha256,
		SizeBytes: int64(m.SizeBytes), Mandatory: m.Mandatory, Platform: m.Platform,
		Algorithm: m.SignatureAlgorithm, KeyID: m.SigningKeyID, Signature: m.Signature}
}

/**
 * Забрать и ПРОВЕРИТЬ манифест.
 *
 * Отказ здесь всегда возвращается ошибкой, а не «обновлений нет». Разница не
 * косметическая: «обновлений нет» человек принимает за ответ и уходит, а
 * означать это может что угодно — от сломанного DNS до подменённого ответа.
 */
func fetchManifest(ctx context.Context, client *http.Client, platform string) (*releaseManifest, error) {
	return fetchManifestVerified(ctx, client, platform, releaseauth.Verify)
}
func fetchManifestVerified(ctx context.Context, client *http.Client, platform string, verify func(*releaseauth.Manifest, string) error) (*releaseManifest, error) {
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
	if err := verify(m.signedManifest(), platform); err != nil {
		return nil, err
	}
	return &m, nil
}

func (s *BaseServer) Update(ctx context.Context, in *gen.UpdateReq) (*gen.UpdateResp, error) {
	client := &http.Client{Timeout: 30 * time.Second}
	fallback := neko_common.CreateProxyHttpClient(neko_common.GetCurrentInstance())
	return s.updateWithNetwork(ctx, in, defaultUpdateNetwork(client, fallback))
}
func (s *BaseServer) updateWithNetwork(ctx context.Context, in *gen.UpdateReq, network updateNetwork) (*gen.UpdateResp, error) {
	ret := &gen.UpdateResp{}
	if in == nil || (in.Action != gen.UpdateAction_Check && in.Action != gen.UpdateAction_Download) {
		ret.Error = "неизвестный запрос обновления"
		return ret, nil
	}
	if !updateMu.TryLock() {
		ret.Error = "проверка или загрузка обновления уже выполняется"
		return ret, nil
	}
	defer updateMu.Unlock()

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
	client, fallback := network.direct, network.fallback

	if in.Action == gen.UpdateAction_Check { // Check update
		verifiedUpdate = nil
		update_download_url, update_expected_sha = "", ""
		m, err := fetchManifestAttempt(ctx, client, manifestPlatform(), network)
		if err != nil && fallback != nil && ctx.Err() == nil {
			// Прямой путь не вышел — пробуем через туннель, молча: человеку важен
			// итог, а не то, какой дорогой мы его добыли.
			m, err = fetchManifestAttempt(ctx, fallback, manifestPlatform(), network)
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
		verifiedUpdate = m.signedManifest()

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
		progress := &updateProgress{path: network.archivePath + ".progress.json"}
		if verifiedUpdate != nil {
			progress.Total = verifiedUpdate.SizeBytes
		}
		progress.save("download")
		defer func() {
			if ret.Error != "" {
				progress.Error = ret.Error
				progress.save("error")
			} else {
				progress.save("ready")
			}
		}()
		if update_download_url == "" || update_expected_sha == "" || verifiedUpdate == nil {
			// Оба условия — одно и то же состояние: проверка не проходила или не
			// прошла. Скачивать при этом нечего и не с чем сверять.
			ret.Error = "No update URL"
			return ret, nil
		}
		if err := network.verify(verifiedUpdate, manifestPlatform()); err != nil {
			ret.Error = err.Error()
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
		 *
		 * Прежде здесь было написано, что границей остаётся ctx. Это было
		 * неправдой: срока ему никто не ставил, и на замершем (не разорванном)
		 * сервере чтение висело бы бесконечно. Границей стала ПАУЗА между
		 * байтами — см. idle.go: медленная закачка проходит, заглохшая нет.
		 */
		resp, cancelDownload, err := beginUpdateDownload(ctx, client, update_download_url, network.headerTimeout)
		if err != nil && fallback != nil && ctx.Err() == nil {
			resp, cancelDownload, err = beginUpdateDownload(ctx, fallback, update_download_url, network.headerTimeout)
		}
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer cancelDownload()
		defer resp.Body.Close()
		if resp.StatusCode != http.StatusOK {
			ret.Error = "сайт ответил " + strconv.Itoa(resp.StatusCode)
			return ret, nil
		}

		// Кладётся как greenrhythm.zip — этого имени ждёт распаковщик, и другого
		// формата он не понимает.
		zipPath := network.archivePath
		partialPath := zipPath + ".partial"
		f, err := os.OpenFile(partialPath, os.O_TRUNC|os.O_CREATE|os.O_RDWR, 0644)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer f.Close()
		defer os.Remove(partialPath)

		// Сумма считается на лету: файл под шестьдесят мегабайт, и читать его
		// вторым проходом ради того же числа незачем.
		h := sha256.New()
		// Полторы минуты тишины при живом соединении — это не медленный канал,
		// а зависший ответ. Порог с запасом: на самом плохом мобильном интернете
		// байты идут чаще.
		guard, stopGuard := newIdleGuard(resp.Body, network.idleTimeout, cancelDownload)
		downloadedBytes, err := io.Copy(io.MultiWriter(f, h, progress), io.LimitReader(guard, verifiedUpdate.SizeBytes+1))
		stopGuard()
		if err != nil {
			f.Close()
			os.Remove(partialPath)
			if guard.timedOut() {
				// Иначе человек увидел бы «context canceled» и решил, что
				// отменил обновление сам.
				ret.Error = "сервер перестал отвечать во время скачивания — обновление отменено"
			} else {
				ret.Error = err.Error()
			}
			return ret, nil
		}
		progress.save("verify")
		if err := f.Sync(); err != nil {
			ret.Error = "не удалось сохранить пакет: " + err.Error()
			return ret, nil
		}
		if err := f.Close(); err != nil {
			ret.Error = err.Error()
			return ret, nil
		}

		/*
		 * ПРОВЕРКА СУММЫ ОБЯЗАТЕЛЬНА, А НЕ «ЕСЛИ НАШЛАСЬ».
		 *
		 * Прежде сумма лежала отдельным файлом рядом с релизом, и её отсутствие
		 * означало «проверять нечем» — то есть установку без проверки. Теперь
		 * она приходит в самом манифесте и без неё сюда не доходят вовсе:
		 * несостоявшаяся проверка равна проваленной.
		 *
		 * SHA256 описывает байты архива; доверие к этому значению даёт подпись
		 * Ed25519 всего манифеста. Её проверяют и перед предложением обновиться,
		 * и отдельно в updater. Закрытого ключа на сервере раздачи нет.
		 */
		got := hex.EncodeToString(h.Sum(nil))
		if downloadedBytes != verifiedUpdate.SizeBytes || !strings.EqualFold(got, update_expected_sha) {
			// Не оставляем повреждённый пакет даже после проверки в downloader:
			// updater независимо проверит подпись, размер и сумму до распаковки.
			f.Close()
			os.Remove(partialPath)
			ret.Error = "контрольная сумма пакета не сошлась — обновление отменено"
			return ret, nil
		}
		manifestBytes, err := json.Marshal(verifiedUpdate)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		if err := os.Rename(partialPath, zipPath); err != nil {
			ret.Error = "не удалось подготовить пакет: " + err.Error()
			return ret, nil
		}
		if err := os.WriteFile(zipPath+".manifest.json", manifestBytes, 0644); err != nil {
			f.Close()
			os.Remove(zipPath)
			ret.Error = "не удалось сохранить подписанный манифест: " + err.Error()
			return ret, nil
		}

		// Sidecar суммы сохраняется для перехода со старого updater. Новый
		// updater принимает только .manifest.json с подписью закреплённого
		// ключа; один .sha256 никогда не разрешает установку.
		if err := os.WriteFile(zipPath+".sha256", []byte(got), 0644); err != nil {
			f.Close()
			os.Remove(zipPath)
			ret.Error = "не удалось записать контрольную сумму рядом с пакетом: " + err.Error()
			return ret, nil
		}
		if runtime.GOOS == "windows" {
			progress.save("prepare")
			if err := network.prepareUpdater(zipPath); err != nil {
				ret.Error = "не удалось подготовить установку: " + err.Error()
				return ret, nil
			}
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
