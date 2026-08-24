package main

import (
	"reflect"
	"testing"
)

/*
 * Форма вызова режима run — закреплённая, а не подразумеваемая.
 *
 * Режим уже однажды пропал целиком и молчал несколько выпусков: скрипт
 * res/vpn/vpn-run-root.sh и ветка Windows в StartVPNProcess звали
 * `run -c <файл>`, а ядро отвечало «Usage:» и выходило с нулём. Со стороны
 * человека это выглядело как «TUN включился и сразу отвалился» — без единой
 * строки в журнале, потому что ошибки-то и не было.
 *
 * Ошибка в разборе аргументов даст ровно ту же картину, поэтому обе живые формы
 * вызова записаны здесь дословно.
 */
func TestSkipLeadingFlags(t *testing.T) {
	cases := []struct {
		name string
		in   []string
		want []string
	}{
		{
			// ДОСЛОВНО ветка Windows: ui/mainwindow.cpp, StartVPNProcess.
			name: "флаг перед командой не съедает команду",
			in:   []string{"--disable-color", "run", "-c", `C:\app\config.json`},
			want: []string{"run", "-c", `C:\app\config.json`},
		},
		{
			// ДОСЛОВНО скрипт: res/vpn/vpn-run-root.sh.
			name: "вызов без флагов не трогается",
			in:   []string{"run", "-c", "/tmp/config.json"},
			want: []string{"run", "-c", "/tmp/config.json"},
		},
		{
			name: "режим управления из приложения",
			in:   []string{"greenrhythm"},
			want: []string{"greenrhythm"},
		},
		{
			name: "несколько флагов подряд",
			in:   []string{"--disable-color", "-q", "run"},
			want: []string{"run"},
		},
		{
			name: "пусто остаётся пустым",
			in:   []string{},
			want: []string{},
		},
		{
			name: "одни флаги — команды нет",
			in:   []string{"--disable-color"},
			want: []string{},
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			got := skipLeadingFlags(c.in)
			if len(got) == 0 && len(c.want) == 0 {
				return
			}
			if !reflect.DeepEqual(got, c.want) {
				t.Errorf("skipLeadingFlags(%q)=%q want %q", c.in, got, c.want)
			}
		})
	}
}

func TestConfigPathFromArgs(t *testing.T) {
	cases := []struct {
		name    string
		in      []string
		want    string
		wantErr bool
	}{
		{name: "короткий ключ", in: []string{"-c", "/tmp/a.json"}, want: "/tmp/a.json"},
		{name: "длинный ключ", in: []string{"--config", "/tmp/a.json"}, want: "/tmp/a.json"},
		{
			// Путь на маке содержит пробел: ~/Library/Application Support/…
			// Он приходит одним аргументом и обязан таким и остаться.
			name: "путь с пробелом остаётся одним аргументом",
			in:   []string{"-c", "/Users/x/Library/Application Support/GreenRhythm/vpn.json"},
			want: "/Users/x/Library/Application Support/GreenRhythm/vpn.json",
		},
		{name: "ключ без значения — отказ", in: []string{"-c"}, wantErr: true},
		{name: "без ключа — пусто, но не отказ", in: []string{"run"}, want: ""},
		{name: "пусто", in: []string{}, want: ""},
		{
			// Значение, похожее на флаг, значением и остаётся: разбирать его как
			// флаг значило бы молча потерять путь.
			name: "значение, похожее на флаг",
			in:   []string{"-c", "-strange-name.json"},
			want: "-strange-name.json",
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			got, err := configPathFromArgs(c.in)
			if c.wantErr {
				if err == nil {
					t.Fatalf("configPathFromArgs(%q): ждали отказ", c.in)
				}
				return
			}
			if err != nil {
				t.Fatalf("configPathFromArgs(%q): неожиданный отказ %v", c.in, err)
			}
			if got != c.want {
				t.Errorf("configPathFromArgs(%q)=%q want %q", c.in, got, c.want)
			}
		})
	}
}
