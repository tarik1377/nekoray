#include "DeviceCredentials.hpp"
#include "SealedStore.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
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

        /**
         * Записать блоб так, чтобы неудача НЕ СТОИЛА идентичности устройства.
         *
         * Здесь было «во временный, потом remove(kFile) + rename». Замысел
         * верный, примитив — нет: QFile::rename отказывается писать поверх
         * существующего файла, поэтому remove был не небрежностью, а
         * необходимостью, и он оставлял окно, в котором нет НИ ОДНОГО файла.
         *
         * Окно открывается не только обрывом питания. Если временный файл
         * держит открытым антивирус или индексатор — обычное состояние сразу
         * после close(), — Qt уходит на запасной путь: копирует временный в
         * целевой, не может удалить источник и удаляет ТОЛЬКО ЧТО СОЗДАННЫЙ
         * приёмник, возвращая false. Ни одного сбоя не произошло, а device.dat
         * пропал.
         *
         * Цена пропажи не «активируйтесь заново», а хуже: DeviceId() заводит
         * НОВЫЙ идентификатор, сайт видит незнакомое устройство и отвечает 409
         * «лимит устройств». Человек упирается в потолок тарифа на ровном месте.
         *
         * QSaveFile делает ровно то, чего не хватало: commit() заменяет файл
         * атомарно (MoveFileEx с MOVEFILE_REPLACE_EXISTING на Windows, rename(2)
         * на остальных), а при любой неудаче временный файл убирается и прежний
         * device.dat остаётся целым. Прямые вызовы Win32 сюда класть нельзя —
         * рядом живёт сборка под Linux.
         */
        bool writeBlob() {
            const auto plain = QJsonDocument(g_blob).toJson(QJsonDocument::Compact);
            const auto sealed = SealedStore::Seal(plain);
            if (sealed.isEmpty()) return false;

            QSaveFile f(kFile);
            if (!f.open(QIODevice::WriteOnly)) return false;
            // 0600 до записи, а не после: между созданием и правкой прав файл
            // иначе успевает полежать читаемым для всех.
            f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            if (f.write(sealed) != sealed.size()) {
                f.cancelWriting();
                return false;
            }
            return f.commit();
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

    bool Remember(State state, const QString &detail) {
        Load();
        g_blob["state"] = stateName(state);
        g_blob["detail"] = detail;
        // Результат возвращается, а не выбрасывается. Само по себе незаписанное
        // состояние стоит немного — в памяти оно верное, и человек видит его
        // сегодня же. Дорого другое: неудача записи здесь означает, что и
        // реквизиты сохранить не выйдет, и знать об этом лучше сразу.
        return writeBlob();
    }

    bool Wipe() {
        Load();
        // deviceId переживает стирание намеренно. Он занимает место по тарифу,
        // и новый после каждого «отключить» съедал бы человеку слоты — ровно
        // та же ловушка, что на Android при переустановке.
        const auto id = g_blob.value("deviceId").toString();
        g_blob = QJsonObject();
        if (!id.isEmpty()) g_blob["deviceId"] = id;
        g_blob["state"] = stateName(SignedOut);
        // Результат возвращается, а не выбрасывается: это единственное место,
        // где молчащая неудача ломает обещание, данное человеку ВСЛУХ. Ему
        // сказали «ключи забыты», а они остались на диске — и он уйдёт с этим,
        // не имея никакой причины проверять.
        return writeBlob();
    }

} // namespace DeviceCredentials
