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

#ifdef Q_OS_WIN
        QList<ForeignTunnel> detectWindows() {
            // Одним вызовом: каждый запуск PowerShell стоит около секунды, и
            // три вызова подряд человек заметит как задержку при старте.
            const auto out = ask("powershell",
                                 {"-NoProfile", "-NonInteractive", "-Command",
                                  "Get-NetRoute -AddressFamily IPv4 -ErrorAction SilentlyContinue | "
                                  "ForEach-Object { "
                                  "$a = Get-NetAdapter -InterfaceIndex $_.InterfaceIndex "
                                  "-ErrorAction SilentlyContinue; "
                                  "if ($a -and -not $a.HardwareInterface) { "
                                  "\"$($a.Name)`t$($_.DestinationPrefix)\" } }"},
                                 15000);
            QMap<QString, ForeignTunnel> byName;
            for (const auto &line: out.split('\n')) {
                const auto parts = line.trimmed().split('\t');
                if (parts.size() < 2) continue;
                const auto name = parts[0].trimmed();
                const auto prefix = parts[1].trimmed();
                if (name.isEmpty() || isOurs(name)) continue;

                auto &t = byName[name];
                t.name = name;
                if (!prefix.isEmpty() && !t.prefixes.contains(prefix)) t.prefixes << prefix;
                if (isHalfTheInternet(prefix)) t.ownsHalfTheInternet = true;
            }
            return byName.values();
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
                if (!t.prefixes.contains(dest)) t.prefixes << dest;
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
