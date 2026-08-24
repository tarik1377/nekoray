#include "ForeignTunnels.hpp"

#include <QProcess>
#include <QRegularExpression>

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
            const auto out = ask("powershell",
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
