#include "ForeignTunnels.hpp"
#include "sys/WinShell.hpp"

#include <QDir>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

namespace NekoGui_sys {

    namespace {

        /** Наш собственный интерфейс. Себя в чужие записывать не надо. */
        bool isOurs(const QString &name) {
            return name.contains("neko-tun", Qt::CaseInsensitive) ||
                   name.contains("sing-tun", Qt::CaseInsensitive);
        }

        /**
         * Половина адресного пространства.
         *
         * Пара 0.0.0.0/1 + 128.0.0.0/1 — приём, которым перехватывают весь
         * трафик, не трогая маршрут по умолчанию. Владелец такой пары и есть
         * полнотуннельный VPN.
         */
        bool isHalfTheInternet(const QString &prefix) {
            return prefix == "0.0.0.0/1" || prefix == "128.0.0.0/1";
        }

        /** Запустить команду и вернуть её вывод. Пусто — не получилось. */
        QString ask(const QString &program, const QStringList &args, int waitMs = 5000) {
            QProcess p;
            p.start(program, args);
            if (!p.waitForFinished(waitMs)) {
                p.kill();
                return {};
            }
            return QString::fromLocal8Bit(p.readAllStandardOutput());
        }

        /**
         * Служебный ли это маршрут.
         *
         * Такие есть у КАЖДОГО интерфейса и ничего не говорят о том, что через
         * него ходит трафик: широковещательный /32, групповая рассылка,
         * link-local. Без этого отсева в «сторонние туннели» попадает всё
         * подряд, а человеку показывается, что через интерфейс якобы идут
         * маршруты. Ровно такой же отсев уже стоит в support/fix-network.ps1 —
         * здесь его просто забыли повторить.
         */
        bool isHousekeepingRoute(const QString &prefix) {
            return prefix.endsWith("/32") || prefix.endsWith("/128") ||
                   prefix == "224.0.0.0/4" || prefix == "ff00::/8" ||
                   prefix.startsWith("fe80::");
        }

#ifdef Q_OS_WIN
        /**
         * Виртуальный коммутатор, а не туннель.
         *
         * Hyper-V, WSL2 и Docker Desktop заводят адаптеры vEthernet (...), и они
         * подходят под «не физический». Предложить человеку выключить их —
         * значит предложить выключить WSL и Docker; это ровно та же ошибка, из-за
         * которой раньше гас домашний WireGuard, только в другую сторону.
         */
        bool isVirtualSwitch(const QString &description, const QString &name) {
            return description.contains("Hyper-V Virtual Ethernet", Qt::CaseInsensitive) ||
                   description.contains("Virtual Ethernet Adapter", Qt::CaseInsensitive) ||
                   name.startsWith("vEthernet", Qt::CaseInsensitive);
        }

        QList<ForeignTunnel> detectWindows() {
            /*
             * ОДИН Get-NetAdapter НА ВСЁ, а не по вызову на каждый маршрут.
             *
             * Здесь стоял Get-NetAdapter внутри ForEach-Object по маршрутам: на
             * обычной машине это сотня запусков командлета, и замер показал 6.6
             * секунды. Вызывается всё это из потока интерфейса, то есть окно на
             * эти секунды переставало перерисовываться, и Windows успевала
             * повесить на него «Не отвечает».
             *
             * Теперь адаптеры читаются один раз в таблицу по ifIndex, а маршруты
             * присоединяются к ней. Плюс ifIndex выносится наружу: выключать
             * адаптер по имени нельзя — см. ForeignTunnel::ifIndex.
             */
            const auto out = ask(PowerShellPath(),
                                 {"-NoProfile", "-NonInteractive", "-Command",
                                  "$ad = @{}; "
                                  "Get-NetAdapter -ErrorAction SilentlyContinue | "
                                  "ForEach-Object { $ad[[string]$_.ifIndex] = $_ }; "
                                  "Get-NetRoute -AddressFamily IPv4 -ErrorAction SilentlyContinue | "
                                  "ForEach-Object { "
                                  "$a = $ad[[string]$_.InterfaceIndex]; "
                                  "if ($a -and -not $a.HardwareInterface) { "
                                  "$a.ifIndex.ToString() + [char]9 + $a.Name + [char]9 + "
                                  "$a.InterfaceDescription + [char]9 + $_.DestinationPrefix } }"},
                                 15000);

            QMap<int, ForeignTunnel> byIndex;
            for (const auto &line: out.split('\n')) {
                const auto parts = line.trimmed().split('\t');
                if (parts.size() < 4) continue;

                bool ok = false;
                const int idx = parts[0].trimmed().toInt(&ok);
                if (!ok) continue;
                const auto name = parts[1].trimmed();
                const auto description = parts[2].trimmed();
                const auto prefix = parts[3].trimmed();

                if (name.isEmpty() || isOurs(name) || isOurs(description)) continue;
                if (isVirtualSwitch(description, name)) continue;

                auto &t = byIndex[idx];
                t.ifIndex = idx;
                t.name = name;
                t.description = description;
                if (isHalfTheInternet(prefix)) t.ownsHalfTheInternet = true;
                if (prefix.isEmpty() || isHousekeepingRoute(prefix)) continue;
                if (!t.prefixes.contains(prefix)) t.prefixes << prefix;
            }
            return byIndex.values();
        }
#endif

#ifdef Q_OS_MACOS
        QList<ForeignTunnel> detectMacos() {
            // netstat, а не route: route показывает по одному маршруту за раз, а
            // нам нужна вся таблица.
            const auto out = ask("/usr/sbin/netstat", {"-rn", "-f", "inet"});
            QMap<QString, ForeignTunnel> byName;

            // Столбцы: назначение, шлюз, флаги, интерфейс (последний). Разбор по
            // пробелам, а не по позициям: ширина колонок пляшет от длины адресов.
            static const QRegularExpression spaces("\\s+");
            for (const auto &raw: out.split('\n')) {
                const auto line = raw.trimmed();
                if (line.isEmpty() || line.startsWith("Destination") || line.startsWith("Routing"))
                    continue;
                const auto cols = line.split(spaces, Qt::SkipEmptyParts);
                if (cols.size() < 4) continue;

                const auto iface = cols.last();
                // Нас интересуют только утилитарные интерфейсы: физические сюда
                // попадать не должны, а lo0 — это мы сами.
                if (!iface.startsWith("utun") && !iface.startsWith("ipsec") &&
                    !iface.startsWith("tap") && !iface.startsWith("tun"))
                    continue;
                if (isOurs(iface)) continue;

                const auto dest = cols[0];
                auto &t = byName[iface];
                t.name = iface;
                // Служебные маршруты есть у каждого интерфейса и о трафике
                // ничего не говорят — тот же отсев, что и на Windows.
                if (!isHousekeepingRoute(dest) && !t.prefixes.contains(dest)) t.prefixes << dest;
                // На маке половина пространства записывается так же.
                if (isHalfTheInternet(dest) || dest == "0/1" || dest == "128.0/1")
                    t.ownsHalfTheInternet = true;
            }
            return byName.values();
        }
#endif

    } // namespace

    QList<ForeignTunnel> DetectForeignTunnels() {
#ifdef Q_OS_WIN
        return detectWindows();
#elif defined(Q_OS_MACOS)
        return detectMacos();
#else
        // На Linux этого пока нет. Пустой список честнее выдуманного разбора:
        // «ничего не нашли» и «не умеем смотреть» ведут к одному и тому же
        // действию — никакому.
        return {};
#endif
    }

    QString DescribeForeignTunnels(const QList<ForeignTunnel> &found) {
        if (found.isEmpty()) return {};

        QStringList parts;
        for (const auto &t: found) {
            QString one = t.name;
            if (t.ownsHalfTheInternet) {
                // Единственный случай, где надо не сообщить, а предупредить.
                one += QObject::tr(" — через него идёт ВЕСЬ трафик; с нашим туннелем "
                                   "он ужиться не сможет");
            } else if (!t.prefixes.isEmpty()) {
                // Не больше пяти: у Tailscale их бывают десятки, и строка на
                // весь экран перестаёт читаться.
                auto shown = t.prefixes.mid(0, 5);
                if (t.prefixes.size() > 5) shown << QStringLiteral("…");
                one += QObject::tr(" — маршруты: ") + shown.join(", ");
            }
            parts << one;
        }
        return QObject::tr("Найдены сторонние туннели (не тронуты): ") + parts.join("; ");
    }

} // namespace NekoGui_sys

namespace NekoGui_sys {

    bool OurTunnelHeldByAnother() {
#ifdef Q_OS_WIN
        // Спрашиваем ровно про наш адаптер и ровно про его состояние. Живой
        // (Up) значит, что им кто-то пользуется прямо сейчас; отключённый
        // остаток от прошлого запуска безвреден — wintun его переиспользует.
        QProcess p;
        p.start(PowerShellPath(),
                {"-NoProfile", "-NonInteractive", "-Command",
                 "$a = Get-NetAdapter -Name 'neko-tun' -ErrorAction SilentlyContinue; "
                 "if ($a -and $a.Status -eq 'Up') { 'held' } else { 'free' }"});
        if (!p.waitForFinished(10000)) {
            p.kill();
            // Не смогли спросить — не повод запрещать. Отказ здесь стоил бы
            // человеку туннеля из-за медленной оболочки.
            return false;
        }
        return QString::fromLocal8Bit(p.readAllStandardOutput()).contains("held");
#else
        return false;
#endif
    }


    /**
     * Состояние нашего туннеля словами системы.
     *
     * Отвечает на то, что поддержка иначе выясняет перепиской: поднят ли
     * адаптер, идут ли через него маршруты. Спрашивается по-разному, потому
     * что туннель на платформах разный: на Windows это именованный
     * wintun-адаптер, на маке — utun со случайным номером, который система
     * выдаёт сама, и по имени его не найти.
     */
    bool HelperAlive(qint64 pid) {
#ifdef Q_OS_WIN
        Q_UNUSED(pid)
        return false;
#else
        if (pid <= 0) return false;
        // Имя, а не просто наличие: ps печатает путь запуска, поэтому сравнение
        // по концу строки.
        return ask("ps", {"-p", QString::number(pid), "-o", "comm="}, 5000)
            .trimmed()
            .endsWith(QStringLiteral("greenrhythm_core"));
#endif
    }

    GreenRhythm::TunHelper::Chain ProbeTunChain(qint64 helperPid, const QString &socksAddr, int socksPort) {
        GreenRhythm::TunHelper::Chain c;
#ifdef Q_OS_WIN
        Q_UNUSED(helperPid)
        Q_UNUSED(socksAddr)
        Q_UNUSED(socksPort)
        return c;
#else
        c.helperAlive = HelperAlive(helperPid);
#ifdef Q_OS_MACOS
        c.adapter = GreenRhythm::TunHelper::interfaceWithAddress(ask("ifconfig", {"-a"}, 8000),
                                                                 GreenRhythm::TunHelper::ourAddress());
        c.routeVia = GreenRhythm::TunHelper::routeInterface(ask("route", {"-n", "get", "1.1.1.1"}, 8000));
#else
        // Linux: `ip -br addr` даёт «имя состояние адреса», `ip route get` — «… dev имя …».
        for (const auto &ln: ask("ip", {"-br", "addr", "show"}, 8000).split('\n')) {
            if (ln.contains(GreenRhythm::TunHelper::ourAddress() + "/")) {
                c.adapter = ln.section(' ', 0, 0).trimmed();
                break;
            }
        }
        const auto rg = ask("ip", {"route", "get", "1.1.1.1"}, 8000);
        const auto devAt = rg.indexOf(QStringLiteral(" dev "));
        if (devAt >= 0) c.routeVia = rg.mid(devAt + 5).section(' ', 0, 0).trimmed();
#endif
        {
            QTcpSocket s;
            s.connectToHost(socksAddr, quint16(socksPort));
            c.socksOpen = s.waitForConnected(1500);
            s.abort();
        }
        {
            /*
             * ОТВЕТ, А НЕ РУКОПОЖАТИЕ, И ЭТО ИСПРАВЛЕНИЕ СОБСТВЕННОЙ ОШИБКИ.
             *
             * Здесь стоял простой connect к 1.1.1.1:443, и он доказывал ровно
             * ничего. Стек «system» — тот, что стоит у тестировщика, — принимает
             * соединение СВОИМ слушателем в ядре (sing-tun, stack_system.go:
             * listener.Accept, и только потом NewConnectionEx). Рукопожатие
             * заканчивается до того, как помощник вообще попробует дозвониться
             * до SOCKS основного ядра. То есть проверка отвечала «туннель
             * работает» при наглухо мёртвом канале — хуже, чем не проверять.
             *
             * Ответ по HTTP пройти без всей цепочки не может: устройство,
             * помощник, SOCKS основного ядра, сервер. Адрес тот же, что у
             * собственной проверки связи приложения (ui/Diagnostics.cpp), чтобы
             * два ответа об одном и том же не расходились.
             *
             * Прокси снимается явно: приложение может держать системный, и
             * тогда запрос ушёл бы мимо маршрутов — мимо того, что проверяем.
             */
            QNetworkAccessManager nam;
            nam.setProxy(QNetworkProxy::NoProxy);
            auto *r = nam.get(QNetworkRequest(QUrl(QStringLiteral("http://cp.cloudflare.com/generate_204"))));
            QEventLoop loop;
            QTimer::singleShot(7000, r, &QNetworkReply::abort);
            QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
            c.throughTunnel = r->error() == QNetworkReply::NoError;
            r->deleteLater();
        }
        return c;
#endif
    }

    QString DescribeTunAdapter() {
        QStringList out;
#ifdef Q_OS_WIN
        // Одним заходом: состояние адаптера и сколько маршрутов через него.
        // Два запуска powershell стоили бы лишних секунд на ровном месте.
        const auto raw = ask(PowerShellPath(),
                             {"-NoProfile", "-NonInteractive", "-Command",
                              "$a = Get-NetAdapter -Name 'neko-tun' -ErrorAction SilentlyContinue; "
                              "if (-not $a) { 'adapter=absent' } else { "
                              "'adapter=' + $a.Status + ' ifIndex=' + $a.ifIndex; "
                              "$r = @(Get-NetRoute -InterfaceIndex $a.ifIndex -ErrorAction SilentlyContinue); "
                              "'routes=' + $r.Count }"},
                             12000);
        for (const auto &ln: raw.split('\n')) {
            const auto t = ln.trimmed();
            if (!t.isEmpty()) out << t;
        }
        if (out.isEmpty()) out << QStringLiteral("adapter=unknown (система не ответила)");
#elif defined(Q_OS_MACOS)
        // На маке имя не наше: sing-tun берёт первый свободный utun. Поэтому
        // ищем не по имени, а по НАШЕМУ адресу — он из шаблона и постоянен.
        const auto adapter = GreenRhythm::TunHelper::interfaceWithAddress(ask("ifconfig", {"-a"}, 8000),
                                                                          GreenRhythm::TunHelper::ourAddress());
        out << (adapter.isEmpty() ? QStringLiteral("adapter=absent (нашего адреса нет ни на одном utun)")
                                  : QStringLiteral("adapter=%1 (наш адрес поднят)").arg(adapter));
        const auto routes = ask("netstat", {"-rn", "-f", "inet"}, 8000);
        int viaUtun = 0;
        for (const auto &ln: routes.split('\n')) {
            if (ln.contains(QStringLiteral("utun"))) viaUtun++;
        }
        out << QStringLiteral("routes-via-utun=%1").arg(viaUtun);
        // Куда система поведёт обычный адрес: через туннель или мимо. Это и
        // есть ответ на «включено, но не работает» — один взгляд вместо круга
        // переписки.
        const auto via = GreenRhythm::TunHelper::routeInterface(ask("route", {"-n", "get", "1.1.1.1"}, 8000));
        out << QStringLiteral("route-1.1.1.1=%1").arg(via.isEmpty() ? QStringLiteral("?") : via);
        // ШЕСТАЯ ВЕРСИЯ СПРАШИВАЕТСЯ ОТДЕЛЬНО. При выключенном IPv6 в настройках
        // туннеля sing-tun не трогает его маршруты вовсе: браузер на сети с
        // двумя стеками ходит мимо туннеля, а всё остальное показывает
        // «работает». Одна строка отвечает на это заранее.
        const auto via6 = GreenRhythm::TunHelper::routeInterface(
            ask("route", {"-n", "get", "-inet6", "2606:4700:4700::1111"}, 8000));
        out << QStringLiteral("route6=%1").arg(via6.isEmpty() ? QStringLiteral("нет маршрута") : via6);
        // Помощник: номер из файла скрипта и жив ли он. Журнал — путём, чтобы
        // поддержка попросила ровно его.
        const auto dir = QDir::currentPath();
        const auto pid = GreenRhythm::TunHelper::readPid(dir);
        if (pid > 0) {
            const bool alive = !ask("ps", {"-p", QString::number(pid), "-o", "comm="}, 5000).trimmed().isEmpty();
            out << QStringLiteral("helper-pid=%1 alive=%2").arg(pid).arg(alive ? "yes" : "no");
        } else {
            out << QStringLiteral("helper-pid=none");
        }
        out << QStringLiteral("helper-log=%1").arg(GreenRhythm::TunHelper::logPath(dir));
        // От root приложение работать не должно (main.cpp отказывает), но
        // старые сборки могли, и поддержке важно это видеть сразу.
        out << QStringLiteral("uid=%1").arg(geteuid());
#else
        const auto raw = ask("ip", {"-br", "addr", "show"}, 8000);
        bool found = false;
        for (const auto &ln: raw.split('\n')) {
            if (ln.contains(QStringLiteral("172.19.0.1"))) {
                out << QStringLiteral("adapter=%1").arg(ln.section(' ', 0, 0).trimmed());
                found = true;
                break;
            }
        }
        if (!found) out << QStringLiteral("adapter=absent");
#endif
        return out.join(QStringLiteral(", "));
    }

} // namespace NekoGui_sys
