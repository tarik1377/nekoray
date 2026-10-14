#pragma once

#include <QString>
#include <QStringList>

namespace GreenRhythm::WindowsProxy {
constexpr unsigned Direct = 1;
constexpr unsigned Manual = 2;
constexpr unsigned Pac = 4;
constexpr unsigned AutoDetect = 8;
struct State {
    unsigned flags = Direct;
    QString server;
};
inline bool same(const State &a, const State &b) {
    return a.flags == b.flags && a.server.trimmed().compare(b.server.trimmed(), Qt::CaseInsensitive) == 0;
}
class Api {
public:
    virtual ~Api() = default;
    virtual bool query(State &state, QString &error) = 0;
    virtual bool write(const State &state, QString &error) = 0;
    virtual bool notify(QString &error) = 0;
};
// All calls are explicit GUI actions. This controller never reconciles on a
// timer and never writes RAS/connectoid, bypass, or PAC URL settings.
class Controller {
public:
    explicit Controller(Api &api) : api(api) {}
    bool enable(const QString &server, QString &error) {
        if (server.trimmed().isEmpty()) { error = QStringLiteral("Адрес системного прокси пуст."); return false; }
        State before;
        if (!api.query(before, error)) return false;
        const auto oldServers = ownedServers;
        const auto oldPrevious = previous;
        if (!owns(before)) {
            previous = before;
            // A remembered endpoint left by our earlier instance must not be
            // restored as a dangling enabled localhost proxy on normal exit.
            if (ownedServers.isEmpty() && (before.flags & Manual) && !(before.flags & (Pac | AutoDetect))
                && before.server.trimmed().compare(server.trimmed(), Qt::CaseInsensitive) == 0)
                previous.flags = Direct;
        }
        if (!ownedServers.contains(server, Qt::CaseInsensitive)) ownedServers.append(server);
        const State wanted{Direct | Manual, server};
        if (!applyAndVerify(wanted, error)) {
            const auto operationError = error;
            QString restoreError;
            State current;
            if (!api.query(current, restoreError)) {
                error = operationError + QStringLiteral("\nНе удалось проверить состояние для восстановления: ") + restoreError;
                return false;
            }
            if (!same(current, before) && !owns(current)) {
                // A different actor replaced the setting during SET/notify.
                // Never roll our older snapshot over that foreign change.
                ownedServers.clear();
                error = operationError + QStringLiteral("\nНастройки прокси изменены извне; новое состояние сохранено.");
                return false;
            }
            if (same(current, before) || applyAndVerify(before, restoreError)) {
                ownedServers = oldServers;
                previous = oldPrevious;
                error = operationError + QStringLiteral("\nПредыдущее состояние прокси восстановлено.");
            } else {
                // Retain ownership candidates for explicit cleanup if a partial
                // write cannot be reverted now. A foreign endpoint is excluded.
                error = operationError + QStringLiteral("\nНе удалось вернуть предыдущее состояние: ") + restoreError;
            }
            return false;
        }
        ownedServers = {server};
        return true;
    }
    bool disable(QString &error) {
        if (ownedServers.isEmpty()) return true;
        State current;
        if (!api.query(current, error)) return false;
        if (!owns(current)) {
            // Someone changed the endpoint or enabled PAC/autodetection. Do not
            // undo that change or overwrite a foreign client's settings.
            ownedServers.clear();
            return true;
        }
        if (!applyAndVerify(previous, error)) return false;
        ownedServers.clear();
        return true;
    }
private:
    bool owns(const State &state) const {
        return state.flags == (Direct | Manual) && ownedServers.contains(state.server.trimmed(), Qt::CaseInsensitive);
    }
    bool applyAndVerify(const State &wanted, QString &error) {
        QString writeError, notifyError, queryError;
        const bool written = api.write(wanted, writeError);
        // Always notify after a write attempt, including partial/failing SETs.
        const bool notified = api.notify(notifyError);
        State actual;
        const bool queried = api.query(actual, queryError);
        if (written && notified && queried && same(actual, wanted)) return true;
        if (!written) error = writeError;
        else if (!notified) error = notifyError;
        else if (!queried) error = queryError;
        else error = QStringLiteral("Windows не подтвердил выбранные флаги и адрес прокси.");
        return false;
    }
    Api &api;
    State previous;
    QStringList ownedServers;
};
}
