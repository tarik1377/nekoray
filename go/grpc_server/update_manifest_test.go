package grpc_server

import (
	"encoding/json"
	"testing"
)

// Манифест приходит снаружи, и числа в нём могут прийти строкой: на той стороне
// они собираются из ответа Postgres, а тот отдаёт bigint СТРОКОЙ. Пока размер
// файла был пустым, это не всплывало; стоило записи о выпуске появиться с
// размером — и разбор падал целиком, показывая человеку строку на английском
// про Go вместо обновления.
//
// Набор закрепляет терпимость к обоим видам. Проверка целостности при этом
// остаётся строгой: она держится на sha256, а не на размере.
func TestManifestAcceptsNumbersAsStrings(t *testing.T) {
	cases := []struct {
		name string
		body string
		size int64
		code int64
	}{
		{"числами", `{"versionCode":1601,"sizeBytes":57473413}`, 57473413, 1601},
		{"строками", `{"versionCode":"1601","sizeBytes":"57473413"}`, 57473413, 1601},
		{"пусто", `{"versionCode":1601,"sizeBytes":null}`, 0, 1601},
		{"пустая строка", `{"versionCode":1601,"sizeBytes":""}`, 0, 1601},
	}
	for _, c := range cases {
		var m releaseManifest
		if err := json.Unmarshal([]byte(c.body), &m); err != nil {
			t.Errorf("%s: разбор не удался: %v", c.name, err)
			continue
		}
		if int64(m.SizeBytes) != c.size {
			t.Errorf("%s: sizeBytes = %d, ожидалось %d", c.name, int64(m.SizeBytes), c.size)
		}
		if int64(m.VersionCode) != c.code {
			t.Errorf("%s: versionCode = %d, ожидалось %d", c.name, int64(m.VersionCode), c.code)
		}
	}
}

// Мусор по-прежнему отвергается: терпимость к типу не означает согласия на
// что угодно, иначе испорченный манифест проехал бы молча.
func TestManifestRejectsNonNumber(t *testing.T) {
	var m releaseManifest
	if err := json.Unmarshal([]byte(`{"sizeBytes":"пятьдесят"}`), &m); err == nil {
		t.Error("ожидался отказ на нечисловой строке, его не было")
	}
}
