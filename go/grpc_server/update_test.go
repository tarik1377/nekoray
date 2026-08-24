package grpc_server

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
)

func TestShouldUpdate(t *testing.T) {
	cases := []struct {
		latest, cur string
		want        bool
	}{
		{"v1.2.0", "v1.1.1", true},               // normal upgrade
		{"v1.1.1", "v1.1.1", false},              // same version
		{"v1.1.1", "v1.2.0", false},              // current is newer
		{"v1.2.0", "4.0.1-2024-12-12", true},     // legacy upstream straggler -> rescue
		{"v1.2.0", "v1.2.0", false},              // equal
		{"v1.2.10", "v1.2.9", true},              // numeric, not lexical
		{"v1.10.0", "v1.9.0", true},              // numeric minor
		{"v2.0.0", "v1.99.99", true},             // major wins
		{"garbage", "v1.1.1", false},             // unparseable latest -> no junk prompt
		{"v1.2.0", "", true},                     // empty current -> rescue
	}
	for _, c := range cases {
		if got := shouldUpdate(c.latest, c.cur); got != c.want {
			t.Errorf("shouldUpdate(%q,%q)=%v want %v", c.latest, c.cur, got, c.want)
		}
	}
}

func TestParseVerRejectsLegacy(t *testing.T) {
	if _, ok := parseVer("4.0.1-2024-12-12"); ok {
		t.Error("legacy upstream version must NOT parse as clean vX.Y.Z")
	}
	if _, ok := parseVer("v1.2.0"); !ok {
		t.Error("clean vX.Y.Z must parse")
	}
}

/*
 * Проверка адреса РАЗБОРОМ, а не префиксом строки.
 *
 * Ради одного случая: verdantvibe.ru.evil.tld начинается с нашего имени и
 * проходит любую проверку вида HasPrefix. Пропускается такой адрес молча — он
 * выглядит ровно так же, как правильный, и человек увидит знакомое начало.
 */
func TestIsOurUrl(t *testing.T) {
	cases := []struct {
		url  string
		want bool
	}{
		{"https://verdantvibe.ru/downloads/GreenRhythm-v1.4.2-windows-x64.zip", true},
		{"https://verdantvibe.ru/x.zip", true},

		// ГЛАВНЫЙ СЛУЧАЙ НАБОРА.
		{"https://verdantvibe.ru.evil.tld/x.zip", false},
		{"https://evil.tld/verdantvibe.ru/x.zip", false},
		// Имя пользователя в адресе: до собаки стоит что угодно, хостом это не
		// делается. Наивная проверка «содержит наше имя» здесь тоже сдаётся.
		{"https://verdantvibe.ru@evil.tld/x.zip", false},
		{"https://sub.verdantvibe.ru/x.zip", false},

		// Без TLS сумма из манифеста не значит ничего: подменить можно и её.
		{"http://verdantvibe.ru/x.zip", false},
		{"ftp://verdantvibe.ru/x.zip", false},

		{"", false},
		{"не адрес вовсе", false},
		{"//verdantvibe.ru/x.zip", false},
	}
	for _, c := range cases {
		if got := isOurUrl(c.url); got != c.want {
			t.Errorf("isOurUrl(%q)=%v want %v", c.url, got, c.want)
		}
	}
}

/*
 * Сумма обязана быть суммой.
 *
 * Шестьдесят три символа вместо шестидесяти четырёх — самая вероятная опечатка
 * при переносе руками, и она обязана отвергаться ДО того, как человеку
 * предложат обновление: иначе он согласится, дождётся закачки и получит отказ
 * на последнем шаге.
 */
func TestLooksLikeSha256(t *testing.T) {
	full := strings.Repeat("a", 64)
	cases := []struct {
		s    string
		want bool
	}{
		{full, true},
		{strings.ToUpper(full), true},
		{strings.Repeat("a", 63), false},
		{strings.Repeat("a", 65), false},
		{"", false},
		{strings.Repeat("z", 64), false}, // не шестнадцатеричные
		{strings.Repeat("a", 63) + " ", false},
	}
	for _, c := range cases {
		if got := looksLikeSha256(c.s); got != c.want {
			t.Errorf("looksLikeSha256(%q)=%v want %v", c.s, got, c.want)
		}
	}
}

/*
 * Манифест: что принимается, а что отвергается.
 *
 * Отказ здесь всегда ошибка, а не «обновлений нет». Разница не косметическая:
 * «обновлений нет» человек принимает за ответ и уходит, а означать это может
 * что угодно — от сломанного DNS до подменённого ответа.
 */
func TestFetchManifest(t *testing.T) {
	const goodSha = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd"

	cases := []struct {
		name    string
		status  int
		body    string
		wantErr string // подстрока; пусто — ошибки быть не должно
		wantNil bool
	}{
		{
			name:   "нормальный ответ принимается",
			status: 200,
			body: `{"versionCode":10402,"version":"1.4.2",
			        "url":"https://verdantvibe.ru/downloads/x.zip",
			        "sha256":"` + goodSha + `","sizeBytes":57436947,
			        "notes":"","mandatory":false,"platform":"windows-x64"}`,
		},
		{
			name:    "204 — выпусков нет, и это не ошибка",
			status:  204,
			body:    "",
			wantNil: true,
		},
		{
			name:   "чужой хост в ссылке — отказ с понятной причиной",
			status: 200,
			body: `{"version":"1.4.2","url":"https://verdantvibe.ru.evil.tld/x.zip",
			        "sha256":"` + goodSha + `"}`,
			wantErr: "не на наш сайт",
		},
		{
			name:   "сумма из 63 символов — отказ",
			status: 200,
			body: `{"version":"1.4.2","url":"https://verdantvibe.ru/x.zip",
			        "sha256":"abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabc"}`,
			wantErr: "не читается",
		},
		{
			name:    "сумма отсутствует вовсе — отказ, а не установка без проверки",
			status:  200,
			body:    `{"version":"1.4.2","url":"https://verdantvibe.ru/x.zip"}`,
			wantErr: "не читается",
		},
		{
			name:    "пятисотка сайта — ошибка, а не «вы актуальны»",
			status:  500,
			body:    "oops",
			wantErr: "500",
		},
		{
			name:    "мусор вместо json — ошибка",
			status:  200,
			body:    "<html>портал перехватчика</html>",
			wantErr: "",
			wantNil: false,
		},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				w.WriteHeader(c.status)
				io.WriteString(w, c.body)
			}))
			defer srv.Close()

			// fetchManifest строит адрес сам, поэтому запрос заворачивается на
			// испытательный сервер подменой транспорта. Так проверяется именно
			// разбор ответа, а не умение склеить строку.
			client := srv.Client()
			client.Transport = redirectTo(srv.URL)

			m, err := fetchManifest(context.Background(), client, "windows-x64")

			switch {
			case c.name == "мусор вместо json — ошибка":
				if err == nil {
					t.Fatal("html вместо манифеста обязан быть ошибкой")
				}
			case c.wantErr != "":
				if err == nil {
					t.Fatalf("ждали отказ со словами %q, а его нет", c.wantErr)
				}
				if !strings.Contains(err.Error(), c.wantErr) {
					t.Errorf("отказ не объясняет причину: %q, ждали подстроку %q", err, c.wantErr)
				}
			default:
				if err != nil {
					t.Fatalf("неожиданный отказ: %v", err)
				}
				if c.wantNil && m != nil {
					t.Errorf("ждали «выпусков нет», получили %+v", m)
				}
				if !c.wantNil && m == nil {
					t.Fatal("ждали манифест, получили «выпусков нет»")
				}
			}
		})
	}
}

// Платформа, которой мы не раздаём, обязана отказать до всякой сети.
func TestFetchManifestUnknownPlatform(t *testing.T) {
	_, err := fetchManifest(context.Background(), http.DefaultClient, "")
	if err == nil {
		t.Fatal("пустая платформа обязана быть отказом")
	}
}

/** Транспорт, отправляющий любой запрос на указанный адрес. */
func redirectTo(base string) http.RoundTripper {
	u, _ := url.Parse(base)
	return roundTripFunc(func(r *http.Request) (*http.Response, error) {
		r = r.Clone(r.Context())
		r.URL.Scheme = u.Scheme
		r.URL.Host = u.Host
		return http.DefaultTransport.RoundTrip(r)
	})
}

type roundTripFunc func(*http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }

/*
 * Версия из манифеста приходит БЕЗ «v» — в отличие от тега релиза, на который
 * shouldUpdate писалась. Проверка на то, что переход на свою раздачу не
 * поломал сравнение молча: перепутанный формат дал бы «обновлений нет» навсегда
 * и ни одной жалобы — люди просто перестали бы обновляться.
 */
func TestShouldUpdateAcceptsManifestVersions(t *testing.T) {
	cases := []struct {
		latest, cur string
		want        bool
	}{
		{"1.4.2", "v1.4.1", true},  // манифест против установленного тега
		{"1.4.2", "1.4.2", false},  // оба без «v»
		{"1.4.2", "v1.4.2", false}, // тот же выпуск, разная запись
		{"1.4.10", "1.4.9", true},  // числами, а не по алфавиту
	}
	for _, c := range cases {
		if got := shouldUpdate(c.latest, c.cur); got != c.want {
			t.Errorf("shouldUpdate(%q,%q)=%v want %v", c.latest, c.cur, got, c.want)
		}
	}
}

// Платформа определяется тем, на чём собрано. Проверяется одно: строка либо из
// того же набора, что знает сайт, либо пустая — третьего быть не должно.
func TestManifestPlatformIsKnownOrEmpty(t *testing.T) {
	known := map[string]bool{
		"windows-x64": true, "linux-x64": true,
		"macos-amd64": true, "macos-arm64": true,
	}
	p := manifestPlatform()
	if p != "" && !known[p] {
		t.Errorf("manifestPlatform()=%q — такой платформы сайт не знает", p)
	}
}
