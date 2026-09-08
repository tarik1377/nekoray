/**
 * Помощник внешнего туннеля: разбор, кавычки, команды и сторожа.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Всё, что здесь проверяется, исполняется только на
 * маке, а мака у нас нет: ошибка в кавычках, в разборе `route -n get` или в
 * имени файла доезжала бы до человека и выглядела бы как «туннель не
 * работает» — без подробностей, потому что подробности как раз и ломались.
 * Чистые функции модуля проверяются здесь на любой платформе.
 *
 * Два сторожа читают файлы дерева: имена файлов помощника обязаны совпадать
 * между main/TunHelper.cpp и res/vpn/vpn-run-root.sh, а остановка на маке
 * обязана идти через macStopCommand — не через «убить все ядра от root».
 *
 * Запуск: ninja tun_helper_test && ./tun_helper_test
 */

#include "main/TunHelper.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QTemporaryFile>

#include <cstdio>

using namespace GreenRhythm::TunHelper;

static int checks = 0;
static int fails = 0;

static void is(const QString &what, bool ok) {
    checks++;
    if (!ok) fails++;
    std::fputs((QString(ok ? QStringLiteral("  ok   ") : QStringLiteral("  ПРОВАЛ "))
                + what + QStringLiteral("\n"))
                   .toUtf8()
                   .constData(),
               stdout);
}

static QString slurp(const QString &path) {
    for (const QString &prefix: {QStringLiteral(""), QStringLiteral("../"), QStringLiteral("../../")}) {
        QFile f(prefix + path);
        if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll());
    }
    return {};
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // ── номер процесса ───────────────────────────────────────────────────
    is("parsePid: число с переводом строки", parsePid("1234\n") == 1234);
    is("parsePid: пусто — 0", parsePid("") == 0);
    is("parsePid: мусор — 0", parsePid("abc") == 0);
    is("parsePid: отрицательное — 0", parsePid("-5") == 0);

    // ── route -n get ─────────────────────────────────────────────────────
    const QString routeUp =
        "   route to: 1.1.1.1\n"
        "destination: 0.0.0.0\n"
        "       mask: 128.0.0.0\n"
        "    gateway: 172.19.0.2\n"
        "  interface: utun4\n"
        "      flags: <UP,GATEWAY,DONE,STATIC,PRCLONING,GLOBAL>\n";
    const QString routeDown =
        "   route to: 1.1.1.1\n"
        "destination: default\n"
        "       mask: default\n"
        "    gateway: 192.168.1.1\n"
        "  interface: en0\n";
    is("routeInterface: через туннель", routeInterface(routeUp) == "utun4");
    is("routeInterface: напрямую", routeInterface(routeDown) == "en0");
    is("routeInterface: нет строки — пусто", routeInterface("garbage\n").isEmpty());

    // ── ifconfig -a ──────────────────────────────────────────────────────
    const QString ifconfig =
        "lo0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST> mtu 16384\n"
        "\tinet 127.0.0.1 netmask 0xff000000\n"
        "en0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500\n"
        "\tinet 192.168.1.10 netmask 0xffffff00 broadcast 192.168.1.255\n"
        "utun3: flags=8051<UP,POINTOPOINT,RUNNING,MULTICAST> mtu 1380\n"
        "\tinet 172.19.0.10 --> 172.19.0.10 netmask 0xffffffff\n"
        "utun4: flags=8051<UP,POINTOPOINT,RUNNING,MULTICAST> mtu 1500\n"
        "\tinet 172.19.0.1 --> 172.19.0.1 netmask 0xfffffff0\n";
    is("interfaceWithAddress: наш адрес на utun4, а не на utun3 с 172.19.0.10",
       interfaceWithAddress(ifconfig, "172.19.0.1") == "utun4");
    is("interfaceWithAddress: чужой адрес — пусто", interfaceWithAddress(ifconfig, "10.9.9.9").isEmpty());
    is("ourAddress совпадает с шаблоном туннеля",
       slurp("res/vpn/sing-box-vpn.json").contains("\"" + ourAddress() + "/28\""));

    // ── кавычки ──────────────────────────────────────────────────────────
    is("shellSingleQuote: пробел остаётся внутри кавычек",
       shellSingleQuote("/Users/x/Library/Application Support/g/config/s.sh")
           == "'/Users/x/Library/Application Support/g/config/s.sh'");
    is("shellSingleQuote: одинарная кавычка — четыре символа",
       shellSingleQuote("/a'b") == "'/a'\\''b'");
    is("appleScriptLiteral: кавычка и слэш экранированы",
       appleScriptLiteral("a\"b\\c") == "a\\\"b\\\\c");
    is("macStartCommand: путь с пробелом проходит оба разбора",
       macStartCommand("/Users/x/Application Support/s.sh") == "bash '/Users/x/Application Support/s.sh'");
    is("macStartCommand: путь с кавычкой не рвёт литерал AppleScript",
       macStartCommand("/a'b/s.sh") == "bash '/a'\\\\''b/s.sh'");

    // ── команда остановки ────────────────────────────────────────────────
    const auto byPid = macStopCommand(4242, 100);
    is("macStopCommand: сигнал номеру — только если это ядро",
       byPid.contains("ps -p $p -o comm=") && byPid.contains("*greenrhythm_core) kill -2 $p"));
    is("macStopCommand: не ядро под этим номером — обход, а не сигнал наугад",
       byPid.contains("pgrep -U 0 greenrhythm_core") && byPid.contains("!= 100"));
    const auto fallback = macStopCommand(0, 777);
    is("macStopCommand: без номера — обход с исключением основного ядра",
       fallback.contains("pgrep -U 0 greenrhythm_core") && fallback.contains("!= 777"));
    is("macStopCommand: двойных кавычек нет (литерал AppleScript)",
       !macStopCommand(0, 1).contains('"') && !macStopCommand(5, 1).contains('"'));

    // ── отмена пароля ────────────────────────────────────────────────────
    is("looksCancelled: код -128", looksCancelled("execution error: User canceled. (-128)"));
    is("looksCancelled: обычная ошибка — нет", !looksCancelled("run: cannot start config: bad json"));
    is("looksCancelled: -128 в середине следа не считается отменой",
       !looksCancelled("+ echo rc=-128 in trace\nexecution error: script failed (1)"));
    is("looksCancelledExit: pkexec отвечает 126", looksCancelledExit(126, ""));
    is("looksCancelledExit: обычный отказ — нет", looksCancelledExit(1, "run: cannot start config") == false);

    // ── чтение журнала по мере записи ────────────────────────────────────
    {
        QTemporaryFile tmp;
        tmp.open();
        tmp.write("first line\nsecond line\npartial");
        tmp.flush();
        QStringList got;
        auto off = readNewLines(tmp.fileName(), 0, got);
        is("readNewLines: две полные строки, хвост придержан", got.size() == 2 && got[1] == "second line");
        tmp.write(" finished\n");
        tmp.flush();
        got.clear();
        off = readNewLines(tmp.fileName(), off, got);
        is("readNewLines: хвост дочитан целиком", got.size() == 1 && got[0] == "partial finished");
        got.clear();
        const auto same = readNewLines(tmp.fileName(), off, got);
        is("readNewLines: без новых данных смещение не меняется", same == off && got.isEmpty());
        is("lastLines: последние две", lastLines(tmp.fileName(), 2) == "second line\npartial finished");
        is("lastLines: файла нет — пусто", lastLines("/nonexistent/tun-helper.log", 3).isEmpty());
        is("readPid: файла нет — 0", readPid("/nonexistent") == 0);
    }

    // ── первое разорванное звено ─────────────────────────────────────────
    {
        Chain c;
        is("цепочка: помощник не жив — первое звено",
           firstBrokenLink(c, "127.0.0.1", 2080).contains("не запущен"));
        c.helperAlive = true;
        is("цепочка: устройства нет", firstBrokenLink(c, "127.0.0.1", 2080).contains("не поднялось"));
        c.adapter = "utun4";
        c.routeVia = "en0";
        is("цепочка: маршрут мимо устройства", firstBrokenLink(c, "127.0.0.1", 2080).contains("en0"));
        c.routeVia = "utun4";
        is("цепочка: порт ядра закрыт", firstBrokenLink(c, "127.0.0.1", 2080).contains("2080"));
        c.socksOpen = true;
        is("цепочка: ответ из сети не пришёл",
           firstBrokenLink(c, "127.0.0.1", 2080).contains("ответ из сети"));
        c.throughTunnel = true;
        is("цепочка цела — пусто", firstBrokenLink(c, "127.0.0.1", 2080).isEmpty());
    }

    // ── сторожа по файлам дерева ─────────────────────────────────────────
    {
        const auto script = slurp("res/vpn/vpn-run-root.sh");
        is("скрипт найден", !script.isEmpty());
        is("скрипт пишет тот же файл номера, что читает приложение",
           script.contains(pidPath("$BASEDIR").section('/', -1)));
        is("скрипт пишет тот же файл журнала, что читает приложение",
           script.contains(logPath("$BASEDIR").section('/', -1)));
        is("скрипт ждёт ядро через || — set -e не оборвёт уборку",
           script.contains("wait $core || rc=$?"));
        is("скрипт передаёт сигнал ядру, а не глотает его",
           script.contains("trap 'kill -2 $core"));
        is("создание файлов защищено noclobber", script.contains("set -C"));
        is("отказ по ссылке идёт до правил iptables",
        // Якорь берётся по отступу вызова, а не по переводу строки: рабочая
        // копия скрипта может лежать с CRLF, и «pre_start_linux\n» тогда не
        // находится вовсе — сторож падал бы на своей платформе, а не на ошибке.
           script.indexOf("refusing symlink") < script.indexOf("\n  pre_start_linux"));
        is("скрипт убирает файл номера после выхода ядра",
           script.contains("rm -f \"$PID_FILE\""));

        const auto mw = slurp("ui/mainwindow.cpp");
        is("mainwindow.cpp найден", !mw.isEmpty());
        is("остановка на маке идёт через macStopCommand", mw.contains("macStopCommand("));
        is("запуск на маке идёт через macStartCommand", mw.contains("macStartCommand("));
        // Ищется строковый литерал в кавычках, а не подстрока: комментарий в
        // окне называет прежнюю команду в обратных кавычках, и сторож на
        // объяснении запрета учил бы только снимать сторожа.
        is("«убить все ядра от root» из окна ушло", !mw.contains("\"pkill -2 -U 0 greenrhythm_core\""));
    }

    // ── строковые литералы не разорваны переводом строки ─────────────────
    //
    // ПОЧЕМУ ЭТА ПРОВЕРКА ЗДЕСЬ. Ветки macOS и Linux в этих файлах закрыты
    // условной компиляцией: на Windows их не видит ни компилятор, ни один
    // набор, и опечатка внутри них доезжает до сборки в CI, а до неё десять
    // минут. Один такой разрыв уже случился: правка превратила «\n» в живой
    // перевод строки внутри литерала, и MSVC прошёл мимо, потому что строка
    // лежала в ветке не своей платформы.
    //
    // Проверка грубая намеренно: считаются кавычки в строке, экранированные и
    // сырые литералы пропускаются. Она не заменяет компилятор — она ловит ровно
    // тот класс поломки, который у нас нечем поймать иначе.
    {
        const QStringList guarded{QStringLiteral("ui/mainwindow.cpp"), QStringLiteral("main/main.cpp"),
                                  QStringLiteral("sys/ForeignTunnels.cpp"), QStringLiteral("main/TunHelper.cpp")};
        QStringList broken;
        for (const auto &path: guarded) {
            const auto text = slurp(path);
            if (text.isEmpty()) {
                broken << path + QStringLiteral(" (не прочитан)");
                continue;
            }
            int lineNo = 0;
            for (const auto &raw: text.split('\n')) {
                lineNo++;
                const auto t = raw.trimmed();
                if (t.startsWith(QStringLiteral("//")) || t.startsWith('*') ||
                    t.startsWith(QStringLiteral("/*")) || raw.contains(QStringLiteral("R\"")))
                    continue;
                auto cleaned = raw;
                cleaned.replace(QStringLiteral("\\\\"), QString());
                cleaned.replace(QStringLiteral("\\\""), QString());
                if (cleaned.count('"') % 2 == 1) {
                    broken << QStringLiteral("%1:%2").arg(path).arg(lineNo);
                }
            }
        }
        is(QStringLiteral("строковые литералы не разорваны переводом строки%1")
               .arg(broken.isEmpty() ? QString() : QStringLiteral(" — ") + broken.join(QStringLiteral(", "))),
           broken.isEmpty());
    }

    std::printf("\nпроверок %d, провалов %d\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
