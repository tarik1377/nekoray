package main

import (
	"errors"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	_ "unsafe"

	"grpc_server"

	"github.com/matsuridayo/libneko/neko_common"
	"github.com/sagernet/sing-box/constant"
)

func main() {
	fmt.Println("sing-box:", constant.Version, "GreenRhythm:", neko_common.Version_neko)
	fmt.Println()

	args := skipLeadingFlags(os.Args[1:])

	if len(args) > 0 && args[0] == "greenrhythm" {
		neko_common.RunMode = neko_common.RunMode_NekoBox_Core
		grpc_server.RunCore(setupCore, &server{})
		return
	}

	if len(args) > 0 && args[0] == "run" {
		os.Exit(runStandalone(args[1:]))
	}

	// Вывод здесь английский, как и весь остальной вывод этого файла, и это не
	// небрежность. Консоль, в которой он появляется, поднимается элевацией без
	// нашего участия и живёт в кодовой странице системы: русские строки
	// показались бы там кашей, то есть хуже, чем не показались бы вовсе.
	fmt.Println("Usage:")
	fmt.Println("  greenrhythm_core greenrhythm      — run under the app")
	fmt.Println("  greenrhythm_core run -c <file>    — bring a config up and hold it")
}

/*
 * ЗАПУСК ПО ФАЙЛУ КОНФИГУРАЦИИ — ТОТ САМЫЙ РЕЖИМ, БЕЗ КОТОРОГО НЕТ TUN.
 *
 * Он существовал, был потерян при переезде на sing-box 1.13 и с тех пор
 * молчал: `res/vpn/vpn-run-root.sh` и Windows-ветка StartVPNProcess зовут ровно
 * `run -c <файл>`, а ядро отвечало «Usage:» и выходило с нулём. Со стороны это
 * выглядит не как отсутствующий режим, а как «TUN включился и тут же
 * отвалился», причём без единой строки в журнале.
 *
 * Внешний привилегированный TUN на этом держится целиком. На macOS другого и не
 * будет: ядро там — потомок интерфейса, интерфейс не root, поднять устройство
 * изнутри процесса невозможно. Заодно оживает Windows при vpn_internal_tun=0.
 *
 * КОНФИГУРАЦИЯ ОБЯЗАТЕЛЬНО ИДЁТ ЧЕРЕЗ ПРЕОБРАЗОВАТЕЛЬ. Файл пишется по образцу
 * res/vpn/sing-box-vpn.json и содержит поля прежних версий; без преобразования
 * sing-box отвергает его за них — и это тоже читается как «TUN сломан», а не
 * как забытый вызов. nekoCreate делает преобразование сам, поэтому здесь
 * достаточно не обходить его стороной.
 */
/*
 * Ведущие флаги пропускаются: интерфейс запускает ядро как
 * `--disable-color run -c <файл>` (ui/mainwindow.cpp, ветка Windows). Цвет и
 * прочее ядру безразличны, но упасть из-за незнакомого флага оно не должно —
 * а именно так выглядела бы для человека очередная «поломка TUN».
 */
func skipLeadingFlags(args []string) []string {
	for len(args) > 0 && len(args[0]) > 0 && args[0][0] == '-' {
		args = args[1:]
	}
	return args
}

/*
 * Путь к конфигурации из аргументов режима run.
 *
 * Вынесено отдельно ради проверок. Режим уже однажды пропал целиком и молчал
 * несколько выпусков: скрипт и Windows-ветка звали `run -c <файл>`, а ядро
 * отвечало «Usage:» и выходило с нулём. Ошибка в разборе аргументов выглядит
 * ровно так же — «TUN включился и сразу отвалился», без строчки в журнале, —
 * поэтому форма вызова закреплена набором, а не только этим комментарием.
 *
 * Пустая строка означает «не указано», и вызывающий обязан на это ответить.
 */
func configPathFromArgs(args []string) (string, error) {
	var configPath string
	for i := 0; i < len(args); i++ {
		if args[i] != "-c" && args[i] != "--config" {
			continue
		}
		if i+1 >= len(args) {
			return "", errors.New("-c needs a file path")
		}
		configPath = args[i+1]
		i++
	}
	return configPath, nil
}

func runStandalone(args []string) int {
	configPath, err := configPathFromArgs(args)
	if err != nil {
		fmt.Fprintln(os.Stderr, "run:", err)
		return 2
	}
	if configPath == "" {
		fmt.Fprintln(os.Stderr, "run: no config given, use: run -c <file>")
		return 2
	}

	configJSON, err := os.ReadFile(configPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "run: cannot read config:", err)
		return 1
	}

	instance, cancel, err := nekoCreate(configJSON)
	if err != nil {
		fmt.Fprintln(os.Stderr, "run: cannot start config:", err)
		return 1
	}

	fmt.Println("running", configPath)

	// Ждём сигнала и уходим сами. Полагаться на то, что нас убьют, нельзя:
	// sing-tun снимает маршруты и восстанавливает DNS в Close(), и пропущенный
	// вызов оставляет систему без интернета до перезагрузки. Ради этого весь
	// режим и ждёт сигнала, вместо того чтобы просто заснуть.
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	<-stop

	fmt.Println("stopping")
	if err := instance.Close(); err != nil {
		fmt.Fprintln(os.Stderr, "run: on shutdown:", err)
	}
	cancel()
	return 0
}
