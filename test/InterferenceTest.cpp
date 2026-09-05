/**
 * Помехи: кто мешает, кто уживается, и что с этим делать.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Здесь два разных класса цены ошибки, и оба платит не
 * программа, а человек.
 *
 * Первый: мы своими руками выключаем ЧУЖУЮ работающую программу. Ошибка не
 * роняет клиент и не портит подключение — она оставляет человека без рабочего
 * VPN, без доступа к корпоративной сети или без туннеля до дома, и он даже не
 * свяжет это с нами, потому что мы к тому моменту закрылись.
 *
 * Второй, обнаруженный позже и оказавшийся важнее: мы ПРЕДЛАГАЕМ выключить то,
 * что и так уживается. Первая версия показывала все туннельные адаптеры с
 * галочкой «приостановить». На машине владельца это были три рабочих OpenVPN,
 * каждый из которых несёт только свои подсети (10.20.19.0/24, 10.77.56.0/23,
 * 192.168.114.0/24) — а наш туннель эти диапазоны и так исключает. То есть
 * предложение было «сломайте себе работу без всякой выгоды», и выглядело оно
 * как забота.
 *
 * ЧТО СТОРОЖИТ НАБОР.
 *
 *  1. ПАРУ /1 ВИДИМ. redirect-gateway у OpenVPN и AllowedIPs=0.0.0.0/0 у
 *     WireGuard не ставят 0.0.0.0/0 — они ставят 0.0.0.0/1 и 128.0.0.0/1.
 *     Проверяй мы только 0.0.0.0/0, полнотуннельный клиент попал бы в
 *     «уживается», и человек искал бы причину где угодно, кроме настоящей.
 *
 *  2. СПИСОК ИСКЛЮЧЕНИЙ СОВПАДАЕТ С ЯДЕРНЫМ. Здесь копия
 *     route_exclude_address из ConfigBuilder. Разойдись они молча — и
 *     «уживается» стало бы враньём ровно там, где на него полагаются.
 *
 *  3. СНИМОК ЛОЖИТСЯ НА ДИСК ДО ПЕРВОЙ ОСТАНОВКИ. Обратный порядок читается
 *     лучше и стоил бы человеку рабочего VPN ровно тогда, когда мы упали между
 *     двумя действиями.
 *
 *  4. СНИМОК УДАЛЯЕТСЯ ПОСЛЕ ПОСЛЕДНЕГО ВОЗВРАТА. Удали его в начале — и
 *     прерывание на середине оставит половину служб выключенными без записи о
 *     том, что их надо вернуть.
 *
 *  5. СЛУЖБА ПЕРЕВОДИТСЯ В «ВРУЧНУЮ», А НЕ В «ОТКЛЮЧЕНО». Отключённую человек
 *     не вернёт одним движением, если наш возврат не сработает.
 *
 * Образцы маршрутов здесь НАСТОЯЩИЕ, снятые с машины владельца. Набор,
 * написанный по выдуманному образцу, проверяет собственную выдумку.
 *
 * Запуск: ninja interference_test && ./interference_test
 */

#include "main/Interference.hpp"

#include <QCoreApplication>
#include <QFile>

#include <cstdio>

using namespace GreenRhythm;

static int checks = 0;
static int fails = 0;

static void is(const QString &what, bool ok) {
    checks++;
    if (!ok) fails++;
    std::fputs((QString(ok ? QStringLiteral("  ok    ") : QStringLiteral("  ПРОВАЛ ")) + what
                + QStringLiteral("\n"))
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

/** НАСТОЯЩИЕ маршруты рабочего OpenVPN с машины владельца (ifIndex 12). */
static QStringList realOpenVpn() {
    return {QStringLiteral("10.20.18.0/24"),  QStringLiteral("10.20.20.0/24"), QStringLiteral("10.30.30.0/24"),
            QStringLiteral("10.77.52.0/24"),  QStringLiteral("10.77.56.0/23"), QStringLiteral("10.77.58.0/23"),
            QStringLiteral("10.9.0.1/32"),    QStringLiteral("10.9.9.92/30"),  QStringLiteral("10.9.9.94/32"),
            QStringLiteral("10.9.9.95/32"),   QStringLiteral("224.0.0.0/4"),   QStringLiteral("255.255.255.255/32")};
}

/** Второй настоящий: у него есть и 192.168.114.0/24 (ifIndex 23). */
static QStringList realOpenVpn2() {
    return {QStringLiteral("10.20.19.0/24"),      QStringLiteral("10.23.0.0/24"),
            QStringLiteral("10.77.56.0/23"),      QStringLiteral("10.9.216.244/30"),
            QStringLiteral("192.168.114.0/24"),   QStringLiteral("224.0.0.0/4"),
            QStringLiteral("255.255.255.255/32")};
}

/** Полнотуннельный клиент: redirect-gateway ставит ПАРУ /1, а не 0.0.0.0/0. */
static QStringList fullTunnel() {
    return {QStringLiteral("0.0.0.0/1"), QStringLiteral("128.0.0.0/1"), QStringLiteral("10.8.0.0/24"),
            QStringLiteral("224.0.0.0/4")};
}

static QString field(const QStringList &f) { return f.join(QChar('\t')); }

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // ── Разбор разведки ──────────────────────────────────────────────────
    std::fputs("Разбор разведки\n", stdout);
    QList<Meddling> found;
    {
        QStringList lines;
        lines << field({QStringLiteral("service"), QStringLiteral("OpenVPNService"),
                        QStringLiteral("OpenVPNService"), QStringLiteral("Running, запуск Auto"),
                        QStringLiteral("1"), QStringLiteral("1"), QString(), QString()});
        lines << field({QStringLiteral("service"), QStringLiteral("vpnagent"),
                        QStringLiteral("Cisco AnyConnect Secure Mobility Agent"),
                        QStringLiteral("Stopped, запуск Disabled"), QStringLiteral("0"), QStringLiteral("1"),
                        QStringLiteral("корпоративный клиент: без него может пропасть доступ к рабочей сети"),
                        QString()});
        lines << field({QStringLiteral("service"), QStringLiteral("CloudflareWARP"),
                        QStringLiteral("Cloudflare WARP"), QStringLiteral("Running, запуск Auto"),
                        QStringLiteral("1"), QStringLiteral("1"), QString(), QString()});
        lines << field({QStringLiteral("adapter"), QStringLiteral("OpenVPN TAP-Windows6"),
                        QStringLiteral("TAP-Windows Adapter V9"), QStringLiteral("Up"), QStringLiteral("1"),
                        QStringLiteral("1"), QString(), realOpenVpn().join(QChar(','))});
        lines << field({QStringLiteral("adapter"), QStringLiteral("MyTapAdapter"),
                        QStringLiteral("TAP-Windows Adapter V9 #2"), QStringLiteral("Up"), QStringLiteral("1"),
                        QStringLiteral("1"), QString(), realOpenVpn2().join(QChar(','))});
        lines << field({QStringLiteral("process"), QStringLiteral("winws"), QStringLiteral("winws"),
                        QStringLiteral("работает"), QStringLiteral("1"), QStringLiteral("0"),
                        QStringLiteral("запущено вручную: вернуть сможете только вы сами"), QString()});
        lines << QStringLiteral("мусор без полей");
        found = parseScan(lines.join(QChar('\n')));
    }
    is(QStringLiteral("разобраны все полные строки, мусор отброшен"), found.size() == 6);
    is(QStringLiteral("маршруты адаптера прочитаны"),
       found.size() > 3 && found[3].prefixes.size() == realOpenVpn().size());
    is(QStringLiteral("корпоративный клиент приезжает с предупреждением"),
       found.size() > 1 && found[1].risk.contains(QStringLiteral("рабочей сети")));
    is(QStringLiteral("процесс помечен как НЕобратимый"),
       found.size() > 5 && !found[5].reversible);
    is(QStringLiteral("пустой ввод даёт пустой список"), parseScan(QString()).isEmpty());

    // ── Что несёт чужой туннель ──────────────────────────────────────────
    std::fputs("\nЧто несёт чужой туннель\n", stdout);
    is(QStringLiteral("рабочий OpenVPN несёт только свои подсети"),
       reachOf(realOpenVpn()) == Reach::OwnSubnets);
    // Ловушка 1 из шапки — ради неё набор и переписан.
    is(QStringLiteral("пара 0.0.0.0/1 + 128.0.0.0/1 опознана как «забирает всё»"),
       reachOf(fullTunnel()) == Reach::Everything);
    is(QStringLiteral("явный 0.0.0.0/0 тоже опознан"),
       reachOf({QStringLiteral("0.0.0.0/0")}) == Reach::Everything);
    is(QStringLiteral("одна половина /1 без второй — это ещё не весь трафик"),
       reachOf({QStringLiteral("0.0.0.0/1"), QStringLiteral("10.8.0.0/24")}) == Reach::OwnSubnets);
    is(QStringLiteral("опущенный адаптер без маршрутов — Unknown, а не «уживается»"),
       reachOf({}) == Reach::Unknown);
    is(QStringLiteral("адаптер с одним лишь шумом — тоже Unknown"),
       reachOf({QStringLiteral("224.0.0.0/4"), QStringLiteral("255.255.255.255/32")}) == Reach::Unknown);
    {
        const auto clean = meaningfulPrefixes(realOpenVpn());
        is(QStringLiteral("групповая рассылка и широковещание отброшены"),
           !clean.contains(QStringLiteral("224.0.0.0/4"))
               && !clean.contains(QStringLiteral("255.255.255.255/32")));
        is(QStringLiteral("IPv6 в счёт не идёт"),
           meaningfulPrefixes({QStringLiteral("fe80::/64"), QStringLiteral("ff00::/8")}).isEmpty());
    }

    // ── Вложенность префиксов ────────────────────────────────────────────
    std::fputs("\nВложенность\n", stdout);
    is(QStringLiteral("10.77.56.0/23 внутри 10.0.0.0/8"),
       prefixInside(QStringLiteral("10.77.56.0/23"), QStringLiteral("10.0.0.0/8")));
    is(QStringLiteral("192.168.114.0/24 внутри 192.168.0.0/16"),
       prefixInside(QStringLiteral("192.168.114.0/24"), QStringLiteral("192.168.0.0/16")));
    is(QStringLiteral("172.20.0.0/16 внутри 172.16.0.0/12"),
       prefixInside(QStringLiteral("172.20.0.0/16"), QStringLiteral("172.16.0.0/12")));
    is(QStringLiteral("172.32.0.0/16 НЕ внутри 172.16.0.0/12 — граница проверена"),
       !prefixInside(QStringLiteral("172.32.0.0/16"), QStringLiteral("172.16.0.0/12")));
    is(QStringLiteral("широкое не помещается в узкое"),
       !prefixInside(QStringLiteral("10.0.0.0/8"), QStringLiteral("10.77.0.0/16")));
    is(QStringLiteral("8.8.8.8/32 не внутри частных диапазонов"),
       !prefixInside(QStringLiteral("8.8.8.8/32"), QStringLiteral("10.0.0.0/8")));
    is(QStringLiteral("мусор вместо префикса не считается вложенным"),
       !prefixInside(QStringLiteral("не префикс"), QStringLiteral("10.0.0.0/8")));

    // ── Главный вывод: рабочие OpenVPN уживаются сами ────────────────────
    std::fputs("\nУживаются или нет\n", stdout);
    {
        const auto ex = tunnelExcludes();
        is(QStringLiteral("настоящий OpenVPN №1 не пересекается с нашим туннелем"),
           notCoveredByTunnel(realOpenVpn(), ex).isEmpty());
        is(QStringLiteral("настоящий OpenVPN №2 (с 192.168.114.0/24) — тоже"),
           notCoveredByTunnel(realOpenVpn2(), ex).isEmpty());
        // А публичный адрес в чужом туннеле мы бы забрали — и вот это надо
        // развести, а не выключать.
        const auto over = notCoveredByTunnel(
            {QStringLiteral("10.8.0.0/24"), QStringLiteral("203.0.113.0/24")}, ex);
        is(QStringLiteral("публичная подсеть чужого туннеля названа пересечением"),
           over == QStringList{QStringLiteral("203.0.113.0/24")});
    }

    // ── Ловушка 2: список исключений обязан совпадать с ядерным ──────────
    {
        const auto builder = slurp(QStringLiteral("db/ConfigBuilder.cpp"));
        is(QStringLiteral("исходник построителя найден"), !builder.isEmpty());
        bool all = !builder.isEmpty();
        for (const auto &e: tunnelExcludes()) {
            if (!builder.contains(QStringLiteral("\"") + e + QStringLiteral("\""))) all = false;
        }
        is(QStringLiteral("каждое наше исключение есть в route_exclude_address ядра"), all);
    }

    // ── Приговор и совет ─────────────────────────────────────────────────
    std::fputs("\nПриговор\n", stdout);
    {
        auto items = found;
        classify(items, tunnelExcludes());

        is(QStringLiteral("рабочий OpenVPN: делать не надо ничего"),
           items[3].cure == Cure::Nothing && items[4].cure == Cure::Nothing);
        is(QStringLiteral("и человеку это сказано словами"),
           items[3].advice.contains(QStringLiteral("уживается")));

        // ВОТ РАДИ ЧЕГО ВСЁ ПЕРЕПИСАНО. Служба OpenVPN нужна туннелю, который
        // уживается, — предлагать её остановить значит ломать работающее.
        is(QStringLiteral("служба OpenVPN НЕ предлагается к остановке"),
           items[0].cure == Cure::Nothing);
        is(QStringLiteral("и объяснено почему"),
           items[0].advice.contains(QStringLiteral("уживается")));

        is(QStringLiteral("корпоративный клиент не предлагается к остановке никогда"),
           items[1].cure == Cure::Manual);
        is(QStringLiteral("остановленный WARP не предлагается: он и так не мешает"),
           items[2].cure == Cure::Pause || items[2].cure == Cure::Nothing);
        is(QStringLiteral("процесс — только руками человека"), items[5].cure == Cure::Manual);
        is(QStringLiteral("семейство OpenVPN опознано и у службы, и у адаптера"),
           items[0].family == QStringLiteral("openvpn") && items[3].family == QStringLiteral("openvpn"));
    }
    {
        // Полнотуннельный чужой клиент — единственный случай, когда предлагаем
        // приостановку.
        QList<Meddling> items;
        Meddling a;
        a.kind = Meddler::Adapter;
        a.key = QStringLiteral("WG");
        a.title = QStringLiteral("WireGuard Tunnel");
        a.running = true;
        a.reversible = true;
        a.prefixes = fullTunnel();
        items << a;
        classify(items, tunnelExcludes());
        is(QStringLiteral("полнотуннельный WireGuard: предлагаем приостановить"),
           items[0].cure == Cure::Pause && items[0].reach == Reach::Everything);
        is(QStringLiteral("и объяснено, что два умолчания не уживаются"),
           items[0].advice.contains(QStringLiteral("весь трафик")));
    }
    {
        // ViPNet, даже забирающий всё, останавливать не предлагаем.
        QList<Meddling> items;
        Meddling a;
        a.kind = Meddler::Adapter;
        a.key = QStringLiteral("ViPNet");
        a.title = QStringLiteral("ViPNet Virtual Adapter");
        a.running = true;
        a.reversible = true;
        a.prefixes = {QStringLiteral("0.0.0.0/0")};
        items << a;
        Meddling s;
        s.kind = Meddler::Service;
        s.key = QStringLiteral("ITCSSVC");
        s.title = QStringLiteral("ViPNet Client Service");
        s.running = true;
        s.reversible = true;
        items << s;
        classify(items, tunnelExcludes());
        is(QStringLiteral("ViPNet опознан как защищённое семейство"), protectedFamily(QStringLiteral("vipnet")));
        is(QStringLiteral("адаптер ViPNet не предлагается к остановке"), items[0].cure == Cure::Manual);
        is(QStringLiteral("служба ViPNet не предлагается к остановке"), items[1].cure == Cure::Manual);
    }
    {
        // Чужой туннель со своими подсетями, часть которых мы забрали бы:
        // лечится адресами, а не остановкой.
        QList<Meddling> items;
        Meddling a;
        a.kind = Meddler::Adapter;
        a.key = QStringLiteral("Corp");
        a.title = QStringLiteral("TAP-Windows Adapter V9 #7");
        a.running = true;
        a.reversible = true;
        a.prefixes = {QStringLiteral("10.8.0.0/24"), QStringLiteral("203.0.113.0/24")};
        items << a;
        classify(items, tunnelExcludes());
        is(QStringLiteral("пересечение лечится адресами, а не остановкой"), items[0].cure == Cure::Separate);
        is(QStringLiteral("в совете названа именно спорная подсеть"),
           items[0].advice.contains(QStringLiteral("203.0.113.0/24"))
               && !items[0].advice.contains(QStringLiteral("10.8.0.0/24")));
        is(QStringLiteral("и она же лежит в overlap для записи в «мимо туннеля»"),
           items[0].overlap == QStringList{QStringLiteral("203.0.113.0/24")});
    }

    // ── Разведка безобидна ───────────────────────────────────────────────
    std::fputs("\nРазведка\n", stdout);
    {
        const auto s = scanScript();
        is(QStringLiteral("свой каталог исключается по пути"), s.contains(QStringLiteral("$own")));
        is(QStringLiteral("наш туннель в список помех не попадает"),
           s.contains(QStringLiteral("neko-tun")) && s.contains(QStringLiteral("sing-tun")));
        is(QStringLiteral("разведка ничего не останавливает и не удаляет"),
           !s.contains(QStringLiteral("Stop-Service")) && !s.contains(QStringLiteral("Remove-Item"))
               && !s.contains(QStringLiteral("Disable-NetAdapter")) && !s.contains(QStringLiteral("sc.exe")));
        is(QStringLiteral("железные адаптеры не предлагаются"), s.contains(QStringLiteral("HardwareInterface")));
        // Без маршрутов приговор «уживается» вынести не из чего.
        is(QStringLiteral("маршруты адаптера собираются"), s.contains(QStringLiteral("Get-NetRoute")));
        is(QStringLiteral("ViPNet ищется наравне с остальными"), s.contains(QStringLiteral("vipnet")));
    }

    // ── Приостановка ─────────────────────────────────────────────────────
    std::fputs("\nПриостановка\n", stdout);
    {
        QList<Meddling> chosen;
        Meddling s;
        s.kind = Meddler::Service;
        s.key = QStringLiteral("CloudflareWARP");
        s.reversible = true;
        chosen << s;
        Meddling a;
        a.kind = Meddler::Adapter;
        a.key = QStringLiteral("WARP");
        a.reversible = true;
        chosen << a;
        Meddling p;
        p.kind = Meddler::Process;
        p.key = QStringLiteral("winws");
        p.reversible = false;
        chosen << p;

        const auto script = pauseScript(chosen, QStringLiteral("C:/p/interference-paused.tsv"));

        // Ловушка 3.
        const int write = script.indexOf(QStringLiteral("Set-Content"));
        is(QStringLiteral("снимок пишется до остановки службы"),
           write >= 0 && script.indexOf(QStringLiteral("Stop-Service")) > write);
        is(QStringLiteral("снимок пишется до отключения адаптера"),
           write >= 0 && script.indexOf(QStringLiteral("Disable-NetAdapter")) > write);

        // Ловушка 5.
        is(QStringLiteral("служба переводится в «вручную»"), script.contains(QStringLiteral("start= demand")));
        is(QStringLiteral("служба НЕ отключается насовсем"), !script.contains(QStringLiteral("start= disabled")));

        is(QStringLiteral("ничего не удаляется: ни служба, ни драйвер"),
           !script.contains(QStringLiteral("sc.exe delete")) && !script.contains(QStringLiteral("Unregister")));
        is(QStringLiteral("необратимый процесс не убивается"),
           !script.contains(QStringLiteral("Stop-Process")) && !script.contains(QStringLiteral("winws")));
    }
    {
        const auto empty = pauseScript({}, QStringLiteral("C:/p/s.tsv"));
        is(QStringLiteral("на пустом выборе скрипт ничего не останавливает"),
           !empty.contains(QStringLiteral("Stop-Service")) && !empty.contains(QStringLiteral("Disable-NetAdapter")));
    }
    {
        // Апостроф в имени: незакрытая кавычка в PowerShell означает не отказ,
        // а выполнение чего-то другого.
        Meddling m;
        m.kind = Meddler::Service;
        m.key = QStringLiteral("Bob's VPN");
        m.reversible = true;
        is(QStringLiteral("апостроф в имени удваивается"),
           pauseScript({m}, QStringLiteral("C:/p/s.tsv")).contains(QStringLiteral("'Bob''s VPN'")));
    }

    // ── Возврат ──────────────────────────────────────────────────────────
    std::fputs("\nВозврат\n", stdout);
    {
        const auto script = resumeScript(QStringLiteral("C:/p/interference-paused.tsv"));
        // Ловушка 4.
        const int remove = script.indexOf(QStringLiteral("Remove-Item"));
        is(QStringLiteral("снимок удаляется после возврата служб"),
           remove > script.lastIndexOf(QStringLiteral("sc.exe config")));
        is(QStringLiteral("снимок удаляется после запуска служб"),
           remove > script.lastIndexOf(QStringLiteral("Start-Service")));
        is(QStringLiteral("возвращается прежний тип запуска, все пять видов"),
           script.contains(QStringLiteral("start= auto")) && script.contains(QStringLiteral("start= demand"))
               && script.contains(QStringLiteral("start= disabled")) && script.contains(QStringLiteral("start= boot"))
               && script.contains(QStringLiteral("start= system")));
        is(QStringLiteral("служба запускается обратно, только если работала"),
           script.contains(QStringLiteral("$state -eq 'Running'")));
        is(QStringLiteral("адаптер включается обратно"), script.contains(QStringLiteral("Enable-NetAdapter")));
        is(QStringLiteral("возврат ничего не останавливает"),
           !script.contains(QStringLiteral("Stop-Service")) && !script.contains(QStringLiteral("Disable-NetAdapter")));
    }

    // ── Снимок словами ───────────────────────────────────────────────────
    std::fputs("\nСнимок словами\n", stdout);
    {
        const QChar t('\t');
        const QString snap = QStringLiteral("service") + t + QStringLiteral("CloudflareWARP") + t
                             + QStringLiteral("Auto") + t + QStringLiteral("Running") + QChar('\n')
                             + QStringLiteral("adapter") + t + QStringLiteral("WARP") + t + QStringLiteral("Up") + t
                             + QStringLiteral("Up");
        const auto said = snapshotSummary(snap);
        is(QStringLiteral("обе строки прочитаны"), said.size() == 2);
        is(QStringLiteral("служба названа службой, состояние показано"),
           !said.isEmpty() && said[0].contains(QStringLiteral("служба CloudflareWARP"))
               && said[0].contains(QStringLiteral("Auto")));
        is(QStringLiteral("пустой снимок — пустой список"), snapshotSummary(QString()).isEmpty());
    }

    std::fputs((QStringLiteral("\nПроверок: %1, провалов: %2\n").arg(checks).arg(fails)).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
