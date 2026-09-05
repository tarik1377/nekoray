#include "main/ConnectionRow.hpp"

#include <QCoreApplication>

namespace GreenRhythm {

    QString hostWithoutPort(const QString &hostPort) {
        const auto s = hostPort.trimmed();
        // IPv6 в скобках: порт стоит после «]».
        if (s.startsWith(QChar('['))) {
            const int close = s.indexOf(QChar(']'));
            return close > 0 ? s.mid(1, close - 1) : s;
        }
        // Голый IPv6 без скобок — два и больше двоеточий, порта тут нет.
        if (s.count(QChar(':')) > 1) return s;
        const int colon = s.lastIndexOf(QChar(':'));
        if (colon <= 0) return s;
        bool isPort = false;
        s.mid(colon + 1).toInt(&isPort);
        return isPort ? s.left(colon) : s;
    }

    QString destinationLabel(const QString &dest, const QString &resolved) {
        const auto d = dest.trimmed();
        const auto r = resolved.trimmed();
        if (r.isEmpty() || r == d) return d;
        // Имя впереди, адрес хвостом и без порта: порт у них один и тот же, и
        // второй раз он только удлиняет строку.
        return r + QStringLiteral("  ·  ") + hostWithoutPort(d);
    }

    QString tunnelLabel() { return QCoreApplication::translate("ConnectionRow", "наш туннель"); }

    QString programLabel(const QString &process, const QString &dest, const QString &serverHost) {
        if (!process.trimmed().isEmpty()) return process.trimmed();
        const auto server = serverHost.trimmed();
        if (!server.isEmpty() && hostWithoutPort(dest).compare(server, Qt::CaseInsensitive) == 0) {
            return tunnelLabel();
        }
        return QStringLiteral("—");
    }

} // namespace GreenRhythm
