#include "DeviceCredentials.hpp"
#include "SealedStore.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>

namespace DeviceCredentials {

    namespace {
        /**
         * Файл лежит в рабочем каталоге настроек, куда main.cpp делает
         * QDir::setCurrent. Не в profiles/ — те открываются пунктом меню
         * «Открыть папку конфигурации» и пишутся открытым текстом.
         */
        const QString kFile = QStringLiteral("device.dat");

        /** Шесть часов — тот же срок, что REFRESH_AFTER_MS на Android. */
        constexpr qint64 kRefreshAfterMs = 6 * 60 * 60 * 1000;

        /** Поля, без которых выданная конфигурация бессмысленна. */
        const QStringList kRequired = {
            QStringLiteral("endpoint"), QStringLiteral("bucket"),
            QStringLiteral("key"), QStringLiteral("secret"),
            QStringLiteral("psk"), QStringLiteral("tag"),
        };

        QJsonObject g_blob;
        bool g_loaded = false;

        QString stateName(State s) {
            switch (s) {
                case Active: return QStringLiteral("active");
                case SignedOut: return QStringLiteral("signed_out");
                case Expired: return QStringLiteral("expired");
                case Limit: return QStringLiteral("limit");
                case Closed: return QStringLiteral("closed");
                default: return QStringLiteral("unknown");
            }
        }

        State stateFrom(const QString &name) {
            if (name == "active") return Active;
            if (name == "signed_out") return SignedOut;
            if (name == "expired") return Expired;
            if (name == "limit") return Limit;
            if (name == "closed") return Closed;
            return Unknown;
        }

        bool writeBlob() {
            const auto plain = QJsonDocument(g_blob).toJson(QJsonDocument::Compact);
            const auto sealed = SealedStore::Seal(plain);
            if (sealed.isEmpty()) return false;

            // Пишем во временный и переименовываем. Иначе прерванная запись
            // (батарея, выключение) оставляет обрезанный файл, и человек
            // получает «активируйтесь заново» вместо рабочего доступа.
            const QString tmp = kFile + ".tmp";
            QFile::remove(tmp);
            QFile f(tmp);
            if (!f.open(QIODevice::WriteOnly)) return false;
            // 0600 до записи, а не после: между созданием и правкой прав файл
            // иначе успевает полежать читаемым для всех.
            f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            const bool ok = f.write(sealed) == sealed.size();
            f.close();
            if (!ok) {
                QFile::remove(tmp);
                return false;
            }
            QFile::remove(kFile);
            return QFile::rename(tmp, kFile);
        }
    } // namespace

    void Load() {
        if (g_loaded) return;
        g_loaded = true;

        QFile f(kFile);
        if (!f.open(QIODevice::ReadOnly)) return;
        const auto sealed = f.readAll();
        f.close();

        const auto plain = SealedStore::Unseal(sealed);
        if (plain.isEmpty()) {
            // Файл есть, но не распечатывается: принесён с другой машины или
            // из-под другого пользователя. Это не поломка — это ровно то, ради
            // чего запечатка и заводилась. Молча начинаем с пустого.
            return;
        }
        const auto doc = QJsonDocument::fromJson(plain);
        if (doc.isObject()) g_blob = doc.object();
    }

    bool Complete(const QJsonObject &issued) {
        for (const auto &name: kRequired) {
            if (issued.value(name).toString().trimmed().isEmpty()) return false;
        }
        return true;
    }

    bool IsProvisioned() {
        Load();
        const auto issued = g_blob.value("issued").toObject();
        return Complete(issued);
    }

    bool NeedsRefresh() {
        Load();
        const auto checked = g_blob.value("checkedAt").toVariant().toLongLong();
        return QDateTime::currentMSecsSinceEpoch() - checked > kRefreshAfterMs;
    }

    State CurrentState() {
        Load();
        return stateFrom(g_blob.value("state").toString());
    }

    QString StateDetail() {
        Load();
        return g_blob.value("detail").toString();
    }

    QString DeviceId() {
        Load();
        auto id = g_blob.value("deviceId").toString();
        if (!id.isEmpty()) return id;

        // Заводится один раз за установку и переживает всё, кроме Wipe.
        // Модель машины идентичностью не является — считать устройства по
        // тарифу можно только по чему-то устойчивому и своему. Персональных
        // данных не несёт и из железа не выводится; сайт хранит только хеш.
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        g_blob["deviceId"] = id;
        writeBlob();
        return id;
    }

    QString Token() {
        Load();
        return g_blob.value("token").toString();
    }

    QString Field(const QString &name) {
        Load();
        return g_blob.value("issued").toObject().value(name).toString();
    }

    bool SaveToken(const QString &token) {
        Load();
        if (token.isEmpty()) return false;
        // deviceId читается ДО правки блоба — он мог ещё не существовать, и
        // DeviceId() его заведёт и запишет; своя же запись ниже иначе затёрла
        // бы только что выданный идентификатор.
        const auto id = DeviceId();
        g_blob["deviceId"] = id;
        g_blob["token"] = token;
        return writeBlob();
    }

    bool Save(const QString &token, const QJsonObject &issued) {
        Load();
        if (!Complete(issued)) return false;

        // deviceId читается ДО правки блоба: он мог ещё не существовать, и
        // DeviceId() его заведёт и запишет. Иначе своя же запись ниже затёрла
        // бы только что выданный идентификатор, и следующая активация заняла
        // бы второй слот по тарифу.
        const auto id = DeviceId();

        g_blob["deviceId"] = id;
        if (!token.isEmpty()) g_blob["token"] = token;
        g_blob["issued"] = issued;
        g_blob["checkedAt"] = QDateTime::currentMSecsSinceEpoch();
        g_blob["state"] = stateName(Active);
        g_blob["detail"] = QStringLiteral("Доступ активен");
        return writeBlob();
    }

    void Remember(State state, const QString &detail) {
        Load();
        g_blob["state"] = stateName(state);
        g_blob["detail"] = detail;
        writeBlob();
    }

    void Wipe() {
        Load();
        // deviceId переживает стирание намеренно. Он занимает место по тарифу,
        // и новый после каждого «отключить» съедал бы человеку слоты — ровно
        // та же ловушка, что на Android при переустановке.
        const auto id = g_blob.value("deviceId").toString();
        g_blob = QJsonObject();
        if (!id.isEmpty()) g_blob["deviceId"] = id;
        g_blob["state"] = stateName(SignedOut);
        writeBlob();
    }

} // namespace DeviceCredentials
