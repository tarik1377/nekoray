#include "RelayActivation.hpp"
#include "HTTPRequestHelper.hpp"
#include "NekoGui.hpp"
#include "db/Database.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

/**
 * Сетевая половина активации. Разбор ответов живёт в RelayActivationParse.cpp
 * и проверяется отдельно, без сети.
 */
namespace RelayActivation {

    namespace {
        const QString kSite = QStringLiteral("https://verdantvibe.ru");
        const QString kRedeemPath = QStringLiteral("/api/auth/app-code/redeem");
        const QString kDevicePath = QStringLiteral("/api/relay/device");

        Outcome fail(DeviceCredentials::State state, const QString &detail,
                     const QString &url = {}, const QString &action = {}) {
            Outcome o;
            o.ok = false;
            o.state = state;
            o.detail = detail;
            o.actionUrl = url;
            o.actionText = action;
            return o;
        }
    } // namespace

    Outcome Redeem(const QString &code) {
        // Верхний регистр и без пробелов — ровно так же приводит Android.
        // Остальное (похожие символы) сайт приводит к хранимому виду сам.
        const auto tidy = code.trimmed().toUpper();
        if (tidy.isEmpty()) {
            return fail(DeviceCredentials::SignedOut, QStringLiteral("Введите код из личного кабинета"),
                        ProfileUrl(), QStringLiteral("Взять код"));
        }

        QJsonObject req;
        req["code"] = tidy;

        const auto r = NetworkRequestHelper::HttpPost(
            QUrl(kSite + kRedeemPath), QJsonDocument(req).toJson(QJsonDocument::Compact),
            {}, /*bypassProxy=*/true);

        QString token;
        auto out = InterpretRedeem(r.status, r.data, r.error, &token);
        if (!out.ok) {
            DeviceCredentials::Remember(out.state, out.detail);
            return out;
        }

        // Токен сохраняется отдельным шагом, ДО запроса реквизитов: иначе при
        // сбое на втором шаге человеку пришлось бы брать новый код, хотя
        // обменянный уже потрачен и второй раз не сработает.
        //
        // ИМЕННО SaveToken, А НЕ Save(token, {}). Save требует полной выданной
        // конфигурации и на пустой возвращает false, ничего не записав. Сначала
        // здесь стоял именно Save — ключ молча не сохранялся, Provision() не
        // находил его и отвечал «не активировано», то есть активация была
        // сломана целиком, а выглядело это как неверный код.
        if (!DeviceCredentials::SaveToken(token)) {
            return fail(DeviceCredentials::CurrentState(),
                        QStringLiteral("Не удалось сохранить вход на этом устройстве"));
        }
        return out;
    }

    Outcome Provision() {
        const auto token = DeviceCredentials::Token();
        if (token.isEmpty()) {
            return fail(DeviceCredentials::SignedOut,
                        QStringLiteral("Резервное подключение не активировано"),
                        ProfileUrl(), QStringLiteral("Взять код"));
        }

        QJsonObject req;
        req["deviceId"] = DeviceCredentials::DeviceId();
        req["label"] = QSysInfo::prettyProductName() + " · " + QSysInfo::machineHostName();
        req["appVersion"] = QString(NKR_VERSION);

        const QList<QPair<QByteArray, QByteArray>> headers = {
            {"Authorization", ("Bearer " + token).toUtf8()},
        };

        const auto r = NetworkRequestHelper::HttpPost(
            QUrl(kSite + kDevicePath), QJsonDocument(req).toJson(QJsonDocument::Compact),
            headers, /*bypassProxy=*/true);

        QJsonObject issued;
        auto out = InterpretDevice(r.status, r.data, r.error, &issued);

        if (out.ok) {
            if (!DeviceCredentials::Save(token, issued)) {
                return fail(DeviceCredentials::CurrentState(),
                            QStringLiteral("Не удалось сохранить настройки на этом устройстве"));
            }
            return out;
        }

        // Стираем ТОЛЬКО когда сайт прямо сказал, что доступа больше нет.
        //
        // По признаку, а не по состоянию: при сбое сети состояние возвращается
        // ПРЕЖНЕЕ, и «стереть, раз Expired» сработало бы у человека, у которого
        // на секунду пропал интернет после вчерашнего отказа. Поймано набором
        // проверок, а не рассуждением.
        if (out.revoked) DeviceCredentials::Wipe();
        DeviceCredentials::Remember(out.state, out.detail);
        return out;
    }

    bool Forget() { return DeviceCredentials::Wipe(); }

    int EnsureProfile() {
        // Уже есть — возвращаем его. Ищем по типу, а не по имени: имя человек
        // мог переименовать, и поиск по нему завёл бы второй профиль.
        for (const auto &[id, ent]: NekoGui::profileManager->profiles) {
            if (ent != nullptr && ent->type == "relay") return id;
        }

        auto ent = NekoGui::ProfileManager::NewProxyEntity("relay");
        // Кладём в ту группу, которую человек сейчас смотрит: искать новый
        // профиль в чужой группе — то же самое, что не завести его вовсе.
        auto group = NekoGui::profileManager->CurrentGroup();
        if (!NekoGui::profileManager->AddProfile(ent, group == nullptr ? -1 : group->id)) return -1;
        return ent->id;
    }

} // namespace RelayActivation
