#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/**
 * Какой адрес назвать другим устройствам, когда VPN раздаётся по сети.
 *
 * Строка «Раздавать VPN другим устройствам» на странице «Настройки» говорит
 * человеку, что ввести на телефоне или телевизоре. Адаптеров у машины много, и
 * почти все — не те: наш же туннель (neko-tun, 172.19.0.0/28), виртуальные сети
 * WSL, Hyper-V, Docker и VirtualBox, выключенные и петлевые. Назвать один из
 * них — значит отправить человека вводить адрес, который никуда не ведёт.
 *
 * Правило: живой непетлевой адаптер не из виртуальных, частный IPv4; 192.168/16
 * лучше 10/8, а тот лучше 172.16/12 — домашние сети почти всегда первого вида,
 * а 172.x у Windows чаще виртуальный. Точка доступа iPhone (172.20.10.x) при
 * этом годится: другого адреса у такой машины и нет.
 *
 * Чистая функция: адреса собирает окно (QNetworkInterface), здесь только выбор.
 * Сторож — test/SettingsPageTest.cpp.
 */
namespace GreenRhythm::Lan {

    struct Candidate {
        QString name;    ///< имя адаптера, как его показывает система
        QString address; ///< адрес в текстовом виде
        bool up = true;
        bool loopback = false;
    };

    namespace detail {
        inline bool virtualAdapter(const QString &name) {
            static const QStringList virtualNames = {
                QStringLiteral("neko-tun"),  QStringLiteral("tun"),       QStringLiteral("tap"),
                QStringLiteral("wintun"),    QStringLiteral("wireguard"), QStringLiteral("vethernet"),
                QStringLiteral("hyper-v"),   QStringLiteral("wsl"),       QStringLiteral("docker"),
                QStringLiteral("virtualbox"), QStringLiteral("vbox"),     QStringLiteral("vmware"),
                QStringLiteral("vmnet"),     QStringLiteral("loopback")};
            const QString lower = name.toLower();
            for (const auto &v: virtualNames) {
                if (lower.contains(v)) return true;
            }
            return false;
        }

        /** Ранг частного IPv4: 0 — 192.168/16, 1 — 10/8, 2 — 172.16/12; -1 — не годится. */
        inline int rank(const QString &address) {
            const QStringList parts = address.split(QLatin1Char('.'));
            if (parts.size() != 4) return -1;
            int octet[4] = {};
            for (int i = 0; i < 4; i++) {
                bool ok = false;
                octet[i] = parts[i].toInt(&ok);
                if (!ok || octet[i] < 0 || octet[i] > 255) return -1;
            }
            if (octet[0] == 192 && octet[1] == 168) return 0;
            if (octet[0] == 10) return 1;
            if (octet[0] == 172 && octet[1] >= 16 && octet[1] <= 31) {
                // 172.19.0.0/28 — адреса нашего же туннеля (db/ConfigBuilder.cpp).
                if (octet[1] == 19 && octet[2] == 0 && octet[3] < 16) return -1;
                return 2;
            }
            return -1;
        }
    } // namespace detail

    /** Адрес для других устройств; пусто — подходящего нет. */
    inline QString pick(const QList<Candidate> &candidates) {
        QString best;
        int bestRank = 3;
        for (const auto &c: candidates) {
            if (!c.up || c.loopback || detail::virtualAdapter(c.name)) continue;
            const int r = detail::rank(c.address);
            if (r >= 0 && r < bestRank) {
                best = c.address;
                bestRank = r;
            }
        }
        return best;
    }

} // namespace GreenRhythm::Lan
