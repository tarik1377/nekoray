#include "dpi/DpiPlan.hpp"

#include <QCryptographicHash>
#include <QSet>

namespace GreenRhythm::Dpi {

    namespace {
        // Сторожевые строки. У zapret пустой список = отсутствие списка = «дурить
        // всех». 203.0.113.0/24 и .invalid — зарезервированные, ни с чем живым не
        // совпадут.
        const QString kHostSentinel = QStringLiteral("sentinel.invalid");
        const QString kIpSentinel = QStringLiteral("203.0.113.113/32");
        const QString kExcludeSentinel = QStringLiteral("203.0.113.114/32");

        QString joinLines(QStringList lines, const QString &sentinel) {
            lines.removeDuplicates();
            lines.removeAll(QString());
            lines << sentinel;
            return lines.join(QChar('\n')) + QChar('\n');
        }

        QString slashes(QString p) {
            p.replace(QChar('\\'), QChar('/'));
            return p;
        }
    } // namespace

    QStringList hostsFromDirectDomains(const QString &directDomain, QStringList *uncovered) {
        QStringList out;
        for (const auto &raw: directDomain.split(QChar('\n'), Qt::SkipEmptyParts)) {
            const auto line = raw.trimmed();
            if (line.isEmpty() || line.startsWith(QChar('#'))) continue;
            if (line.startsWith(QStringLiteral("domain:"))) {
                // «domain:» у ядра — домен и все поддомены; у hostlist то же
                // самое по умолчанию.
                out << line.mid(7).trimmed();
            } else if (line.startsWith(QStringLiteral("full:"))) {
                // «full:» — только это имя; у hostlist это префикс «^».
                out << QStringLiteral("^") + line.mid(5).trimmed();
            } else if (line.startsWith(QStringLiteral("geosite:")) || line.startsWith(QStringLiteral("keyword:"))
                       || line.startsWith(QStringLiteral("regexp:"))) {
                // Категории и шаблоны hostlist не выражает. Не молчим об этом.
                if (uncovered != nullptr) *uncovered << line;
            } else {
                out << line;
            }
        }
        // Две зоны верхнего уровня одной страны в списке защиты значат «дурить
        // весь рунет» — это не защита игры, а нагрузка на процессор впустую.
        // Оставляем их человеку видимыми в uncovered, но не в списке.
        QStringList kept;
        for (const auto &h: out) {
            const auto bare = h.startsWith(QChar('^')) ? h.mid(1) : h;
            if (!bare.contains(QChar('.')) || bare == QStringLiteral("xn--p1ai")) {
                if (uncovered != nullptr) *uncovered << QStringLiteral("domain:") + bare;
                continue;
            }
            kept << h;
        }
        return kept;
    }

    Plan buildPlan(const PlanInput &in) {
        Plan plan;
        const QString dir = slashes(in.listDir).endsWith(QChar('/')) ? slashes(in.listDir) : slashes(in.listDir) + QChar('/');
        plan.hostlistPath = dir + QStringLiteral("bypass.hosts");
        plan.ipsetPath = dir + QStringLiteral("bypass.ips");
        plan.excludePath = dir + QStringLiteral("exclude.ips");

        plan.hostlistText = joinLines(in.hosts, kHostSentinel);
        plan.ipsetText = joinLines(in.ips, kIpSentinel);
        plan.excludeText = joinLines(in.excludeIps, kExcludeSentinel);

        const auto tcp = expandArgs(in.strategy.tcpDesync, in.binDir);
        const auto udp = expandArgs(in.strategy.udpDesync, in.binDir);

        QStringList a;
        // Только физический адаптер — см. заголовок. Ноль оставлен для тестов.
        if (in.ifIndex > 0) a << QStringLiteral("--wf-iface=%1.0").arg(in.ifIndex);
        // Порты захвата. Игровых 1024-65535 здесь нет намеренно: профиль на
        // любые протоколы с подделками к игровому серверу без TTL шлёт ему мусор
        // внутри сессии, и это отдельное решение, а не умолчание.
        a << QStringLiteral("--wf-tcp=80,443,8443");
        if (!udp.isEmpty()) a << QStringLiteral("--wf-udp=443");

        // Домены и адреса — РАЗНЫМИ профилями: внутри одного профиля hostlist и
        // ipset складываются по «и», а нужно «или».
        auto profile = [&](const QStringList &filter, const QString &listArg, const QStringList &desync) {
            a << filter;
            a << listArg;
            a << QStringLiteral("--ipset-exclude=") + plan.excludePath;
            a << desync;
        };

        profile({QStringLiteral("--filter-tcp=80,443,8443")}, QStringLiteral("--hostlist=") + plan.hostlistPath, tcp);
        a << QStringLiteral("--new");
        profile({QStringLiteral("--filter-tcp=80,443,8443")}, QStringLiteral("--ipset=") + plan.ipsetPath, tcp);
        if (!udp.isEmpty()) {
            a << QStringLiteral("--new");
            profile({QStringLiteral("--filter-udp=443")}, QStringLiteral("--hostlist=") + plan.hostlistPath, udp);
        }
        plan.args = a;

        QCryptographicHash h(QCryptographicHash::Sha256);
        h.addData(plan.args.join(QChar('\n')).toUtf8());
        h.addData(plan.hostlistText.toUtf8());
        h.addData(plan.ipsetText.toUtf8());
        h.addData(plan.excludeText.toUtf8());
        plan.hash = QString::fromLatin1(h.result().toHex().left(16));
        return plan;
    }

} // namespace GreenRhythm::Dpi
