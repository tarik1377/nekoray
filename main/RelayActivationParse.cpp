#include "RelayActivation.hpp"

#include <QJsonDocument>
#include <QJsonObject>

/**
 * Чистый разбор ответов сайта — отдельным файлом ради проверяемости.
 *
 * Сюда НЕ включается ни HTTPRequestHelper, ни NekoGui: они тянут за собой
 * половину приложения, и набор проверок пришлось бы линковать с GUI. А
 * проверять здесь надо именно эти ветки: 402 требует стереть реквизиты, а
 * сетевой сбой — не трогать ничего, и перепутать их значит либо отключить
 * человека на ровном месте, либо оставить ему нерабочие ключи.
 */
namespace RelayActivation {

    namespace {
        const QString kSite = QStringLiteral("https://verdantvibe.ru");

        QJsonObject parse(const QByteArray &body) {
            const auto doc = QJsonDocument::fromJson(body);
            return doc.isObject() ? doc.object() : QJsonObject();
        }

        /** Сообщение сайта, если оно есть и на человеческом языке. */
        QString saidBySite(const QJsonObject &o) {
            for (const auto &field: {"error", "message", "detail"}) {
                const auto v = o.value(field).toString().trimmed();
                if (!v.isEmpty()) return v;
            }
            return {};
        }

        Outcome fail(DeviceCredentials::State state, const QString &detail,
                     const QString &url = {}, const QString &action = {},
                     bool revoked = false) {
            Outcome o;
            o.ok = false;
            o.state = state;
            o.detail = detail;
            o.actionUrl = url;
            o.actionText = action;
            o.revoked = revoked;
            return o;
        }
    } // namespace

    QString ProfileUrl() { return kSite + "/profile#app-code"; }

    Outcome InterpretRedeem(int status, const QByteArray &body, const QString &networkError,
                            QString *token) {
        if (token) token->clear();

        // Сеть не ответила — это НЕ отказ в доступе. Состояние не трогаем.
        if (status == 0) {
            return fail(DeviceCredentials::CurrentState(),
                        networkError.isEmpty() ? QStringLiteral("Не удалось связаться с сайтом")
                                               : QStringLiteral("Не удалось связаться с сайтом: ") + networkError);
        }

        const auto o = parse(body);

        if (status == 401 || status == 400) {
            // Код одноразовый и живёт пять минут — самая частая причина отказа
            // не «код неверный», а «код уже истёк». Говорим об этом сразу,
            // иначе человек будет вводить тот же код по третьему разу.
            const auto said = saidBySite(o);
            return fail(DeviceCredentials::SignedOut,
                        said.isEmpty() ? QStringLiteral("Код не подошёл. Он одноразовый и живёт 5 минут — возьмите новый.")
                                       : said,
                        ProfileUrl(), QStringLiteral("Взять новый код"));
        }
        if (status == 429) {
            return fail(DeviceCredentials::SignedOut,
                        QStringLiteral("Слишком много попыток. Подождите минуту и попробуйте снова."));
        }
        if (status < 200 || status >= 300) {
            const auto said = saidBySite(o);
            return fail(DeviceCredentials::CurrentState(),
                        said.isEmpty() ? QStringLiteral("Сайт ответил ошибкой %1").arg(status) : said);
        }

        const auto got = o.value("token").toString().trimmed();
        if (got.isEmpty()) {
            // Поле называется именно token. Читать не то поле — значит получить
            // пустую строку из ответа 200 и узнать об этом шагом позже, где
            // отказ будет выглядеть как неверный код.
            return fail(DeviceCredentials::CurrentState(),
                        QStringLiteral("Сайт не вернул ключ сессии"));
        }

        if (token) *token = got;
        Outcome ok;
        ok.ok = true;
        ok.state = DeviceCredentials::Unknown; // решится следующим шагом
        ok.detail = o.value("email").toString();
        return ok;
    }

    Outcome InterpretDevice(int status, const QByteArray &body, const QString &networkError,
                            QJsonObject *issued) {
        if (issued) *issued = QJsonObject();

        if (status == 0) {
            // Сбой сети не доказывает, что доступ кончился: реквизиты остаются
            // на месте, состояние прежнее. Ровно этим 0 отличается от 402.
            return fail(DeviceCredentials::CurrentState(),
                        networkError.isEmpty() ? QStringLiteral("Не удалось связаться с сайтом")
                                               : QStringLiteral("Не удалось связаться с сайтом: ") + networkError);
        }

        const auto o = parse(body);

        if (status == 402) {
            // Подписка кончилась. Реквизиты надо СТЕРЕТЬ: с ними подключение
            // формально возможно, а по существу уже нет, и человек будет
            // упираться в отказ узла, не понимая причины.
            const bool everHad = o.value("canRenew").toBool(false);
            const auto code = o.value("code").toString();
            if (code == "traffic_exhausted") {
                return fail(DeviceCredentials::Expired,
                            QStringLiteral("Трафик по подписке закончился"),
                            kSite + "/subscriptions/my", QStringLiteral("Сбросить трафик"),
                            /*revoked=*/true);
            }
            return fail(DeviceCredentials::Expired,
                        everHad ? QStringLiteral("Подписка закончилась — продлите её, чтобы продолжить")
                                : QStringLiteral("Нужна активная подписка на резервное подключение"),
                        kSite + "/subscriptions/my",
                        everHad ? QStringLiteral("Продлить") : QStringLiteral("Оформить"),
                        /*revoked=*/true);
        }
        if (status == 409) {
            const auto said = saidBySite(o);
            return fail(DeviceCredentials::Limit,
                        said.isEmpty() ? QStringLiteral("Достигнут лимит устройств по тарифу") : said,
                        kSite + "/subscriptions/my", QStringLiteral("Отключить лишнее устройство"));
        }
        if (status == 403) {
            const auto code = o.value("code").toString();
            if (code == "relay_admin_only") {
                return fail(DeviceCredentials::Closed,
                            QStringLiteral("Резервное подключение пока не открыто для вашего аккаунта"));
            }
            const auto said = saidBySite(o);
            return fail(DeviceCredentials::Closed,
                        said.isEmpty() ? QStringLiteral("Доступ запрещён") : said);
        }
        if (status == 401) {
            // Токен протух. Это не «нет подписки» — надо просто активироваться
            // заново, и сказать надо именно так.
            return fail(DeviceCredentials::SignedOut,
                        QStringLiteral("Вход устарел — активируйте заново"),
                        ProfileUrl(), QStringLiteral("Взять код"));
        }
        if (status < 200 || status >= 300) {
            const auto said = saidBySite(o);
            return fail(DeviceCredentials::CurrentState(),
                        said.isEmpty() ? QStringLiteral("Сайт ответил ошибкой %1").arg(status) : said);
        }

        if (!DeviceCredentials::Complete(o)) {
            // Половина полей хуже, чем ничего: подключение с ними молча не
            // поднимется, и разбираться будут с узлом, а не с ответом сайта.
            return fail(DeviceCredentials::CurrentState(),
                        QStringLiteral("Сайт вернул неполную конфигурацию"));
        }

        if (issued) *issued = o;
        Outcome ok;
        ok.ok = true;
        ok.state = DeviceCredentials::Active;
        ok.detail = QStringLiteral("Доступ активен");
        return ok;
    }


} // namespace RelayActivation
