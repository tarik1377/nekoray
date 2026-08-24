/**
 * Проверка сборки файла автонастройки прокси (PAC) для macOS.
 *
 * Набор существует потому, что проверить это на самом маке здесь нечем, а цена
 * ошибки высокая и незаметная: неверный PAC не отказывает — он молча отправляет
 * часть трафика не туда. Человек видит «интернет работает», а половина запросов
 * идёт мимо канала или, наоборот, домашний NAS уезжает в него.
 *
 * Проверяются три разных свойства:
 *   — что попало в списки и что из них выброшено НАЗВАННЫМ;
 *   — что подстановка строк не ломает разбор (кавычки в правиле);
 *   — что порядок проверок такой, как обещано в шапке PacBuilder.hpp.
 *
 * Сам JavaScript исполняется отдельно: support/Test-Pac.js прогоняет готовый
 * файл через node и сверяет решения по адресам. Здесь — только текст.
 *
 * Запуск: ninja pac_test && ./pac_test
 */

#include "sys/macos/PacBuilder.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace NekoGui_sys;

static int checks = 0;
static int fails = 0;

/** printf, а не qInfo: qInfo в сборке Release заглушён правилами логирования. */
static void say(const QString &s) {
    std::fputs(s.toUtf8().constData(), stdout);
    std::fputc('\n', stdout);
}

static void is(const QString &what, bool ok) {
    checks++;
    if (ok) {
        say("  ок    " + what);
    } else {
        fails++;
        say("  ПЛОХО " + what);
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
    say("");
    say("PacBuilder");

    // ---- списки попадают в файл ----

    {
        PacInput in;
        in.socksPort = 2080;
        in.proxyDomain = "example.com\nfull:news.example.org";
        in.directDomain = "domain:bank.ru\n# заметка\n\nmail.ru";
        in.blockDomain = "ads.example.net";
        in.proxyIp = "203.0.113.0/24";
        in.directIp = "198.51.100.0/24";

        PacNotes notes;
        const auto pac = BuildPac(in, &notes);

        is("порт подставлен в оба вида записи",
           pac.contains("SOCKS5 127.0.0.1:2080") && pac.contains("SOCKS 127.0.0.1:2080"));
        is("в конце всегда есть запасной прямой путь", pac.contains("; DIRECT\""));

        is("домен «через канал» в списке", pac.contains("var P = [\"example.com\",\"news.example.org\"]"));
        is("приставка full: снята", !pac.contains("full:"));
        is("приставка domain: снята", !pac.contains("domain:bank.ru"));
        is("заметка и пустая строка выброшены",
           pac.contains("var D = [\"bank.ru\",\"mail.ru\"]"));
        is("блокировка в своём списке", pac.contains("var B = [\"ads.example.net\"]"));
        is("сети в своих списках",
           pac.contains("var PI = [\"203.0.113.0/24\"]") && pac.contains("var DI = [\"198.51.100.0/24\"]"));
        is("выбрасывать было нечего", notes.skipped.isEmpty());
    }

    // ---- невыразимое выбрасывается, но НАЗЫВАЕТСЯ ----

    {
        // Молчаливый пропуск здесь — худший исход: правило перестаёт работать,
        // ничего об этом не сообщает, и поддержка ищет причину вслепую.
        PacInput in;
        in.socksPort = 2080;
        in.proxyDomain = "geosite:google\nexample.com\nregexp:.*\\.example\\.net";
        in.directIp = "geoip:ru\n192.0.2.0/24\nfd00::/8";

        PacNotes notes;
        const auto pac = BuildPac(in, &notes);

        is("geosite не попал в файл", !pac.contains("geosite"));
        is("geoip не попал в файл", !pac.contains("geoip"));
        is("regexp не попал в файл", !pac.contains("regexp"));
        is("выразимое рядом уцелело",
           pac.contains("\"example.com\"") && pac.contains("\"192.0.2.0/24\""));

        is("выброшенное названо всё", notes.skipped.size() == 4);
        is("в том числе адрес IPv6", notes.skipped.contains("fd00::/8"));
        is("и geosite", notes.skipped.contains("geosite:google"));
    }

    // ---- подстановка не ломает разбор ----

    {
        // Правило приходит из настроек, а настройки человек правит руками и
        // иногда переносит из чужих конфигов. Кавычка внутри правила без
        // экранирования закрыла бы строку и сделала весь файл неразбираемым —
        // то есть выключила бы прокси целиком, а не одно правило.
        PacInput in;
        in.socksPort = 2080;
        in.proxyDomain = "eve\".com\nback\\slash.com";

        const auto pac = BuildPac(in, nullptr);

        is("кавычка в правиле экранирована", pac.contains("\\\"") && !pac.contains("[\"eve\".com\""));
        is("обратная косая экранирована", pac.contains("back\\\\slash.com"));
    }

    // ---- порядок проверок ----

    {
        const auto pac = BuildPac(PacInput{}, nullptr);

        const int localAt = pac.indexOf("\"localhost\"");
        const int proxyAt = pac.indexOf("any(h, P)");
        const int blockAt = pac.indexOf("any(h, B)");
        const int directAt = pac.indexOf("any(h, D)");
        const int finalAt = pac.lastIndexOf("return VIA;");

        is("местное проверяется раньше всего", localAt > 0 && localAt < proxyAt);
        // Человек, добавивший домен и туда и туда, почти наверняка хотел
        // провести его через канал: «мимо» чаще унаследовано из общего списка.
        is("«через канал» проверяется раньше «мимо»", proxyAt < directAt);
        is("блокировка между ними", proxyAt < blockAt && blockAt < directAt);
        is("канал — последний ответ", finalAt > directAt);

        is("частные сети перечислены",
           pac.contains("10.0.0.0") && pac.contains("172.16.0.0") &&
               pac.contains("192.168.0.0") && pac.contains("169.254.0.0"));
        // Без этой строки домашняя сеть человека уезжает в канал ровно в
        // момент включения прокси.
        is("CGNAT перечислен тоже", pac.contains("100.64.0.0"));
        is("имя без точки — своё, значит напрямую", pac.contains("h.indexOf(\".\") < 0"));
    }

    // ---- файл для проверки в node ----
    //
    // Пишется всегда: support/Test-Pac.js исполняет его настоящим движком и
    // сверяет решения. Разбор текста, каким бы подробным он ни был, не отвечает
    // на вопрос «а этот файл вообще разбирается».
    {
        PacInput in;
        in.socksPort = 2080;
        in.proxyDomain = "example.com";
        in.directDomain = "bank.ru";
        in.blockDomain = "ads.example.net";
        in.proxyIp = "203.0.113.0/24";
        in.directIp = "198.51.100.0/24";

        QFile out("pac-sample.pac");
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(BuildPac(in, nullptr).toUtf8());
            out.close();
            say("  файл  pac-sample.pac записан рядом");
        }
    }

    say("");
    say(QString("проверок: %1, провалов: %2").arg(checks).arg(fails));
    std::fflush(stdout);
    return fails > 0 ? 1 : 0;
}
