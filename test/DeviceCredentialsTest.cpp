/**
 * Проверка хранилища реквизитов доступа.
 *
 * Главное здесь — не «сохраняется и читается», а два свойства, которые молча
 * теряются при правке: файл на диске не содержит читаемых ключей, и
 * идентификатор установки переживает стирание. Первое — то, ради чего слой
 * заводился; второе — потому что новый идентификатор после каждого «отключить»
 * съедал бы человеку слоты по тарифу.
 *
 * Запуск: ninja credentials_test && ./credentials_test
 */

#include "main/DeviceCredentials.hpp"
#include "main/RelayActivation.hpp"
#include "main/SealedStore.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static int checks = 0;
static int fails = 0;

static void say(const QString &s) {
    std::fputs(s.toUtf8().constData(), stdout);
    std::fputc('\n', stdout);
}

static void is(const QString &what, bool ok) {
    checks++;
    if (ok) {
        say("  ок    " + what);
    } else {
        fails++;
        say("  ПЛОХО " + what);
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
    say("");
    say("SealedStore и DeviceCredentials");
    say(QString("  запечатка: %1").arg(SealedStore::Kind()));

    // Хранилище пишет в текущий каталог — уводим его во временный, чтобы не
    // тронуть настоящий device.dat разработчика.
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        say("не удалось завести временный каталог");
        return 1;
    }
    QDir::setCurrent(tmp.path());

    // ---- запечатка ----

    {
        const QByteArray plain = "S3_SECRET=очень-секретная-строка";
        const auto sealed = SealedStore::Seal(plain);
        is("запечатанное не пусто", !sealed.isEmpty());
        is("запечатанное не равно исходному", sealed != plain);
        is("в запечатанном нет исходной подстроки", !sealed.contains("очень-секретная-строка"));
        is("распечатывается обратно", SealedStore::Unseal(sealed) == plain);
    }

    is("мусор не распечатывается", SealedStore::Unseal("это не наш блоб").isEmpty());
    is("пустое остаётся пустым", SealedStore::Unseal({}).isEmpty());
    is("пустое не запечатывается", SealedStore::Seal({}).isEmpty());

    // ---- хранилище ----

    QJsonObject issued;
    issued["endpoint"] = "storage.example.net";
    issued["bucket"] = "media-assets-example";
    issued["key"] = "AKIAEXAMPLEKEY";
    issued["secret"] = "ochen-sekretnyj-secret-12345";
    issued["psk"] = "derived-device-psk-abcdef";
    issued["tag"] = "abc123";

    is("до активации реквизитов нет", !DeviceCredentials::IsProvisioned());
    is("и состояние неизвестно либо «не активировано»",
       DeviceCredentials::CurrentState() == DeviceCredentials::Unknown ||
           DeviceCredentials::CurrentState() == DeviceCredentials::SignedOut);

    const auto idBefore = DeviceCredentials::DeviceId();
    is("идентификатор установки заводится", !idBefore.isEmpty());
    is("и не меняется при повторном чтении", DeviceCredentials::DeviceId() == idBefore);

    is("неполная конфигурация не принимается",
       !DeviceCredentials::Save("token-123", QJsonObject{{"endpoint", "x"}}));
    is("и после отказа реквизитов по-прежнему нет", !DeviceCredentials::IsProvisioned());

    is("полная конфигурация сохраняется", DeviceCredentials::Save("token-123", issued));
    is("реквизиты появились", DeviceCredentials::IsProvisioned());
    is("состояние стало активным", DeviceCredentials::CurrentState() == DeviceCredentials::Active);
    is("поле читается", DeviceCredentials::Field("bucket") == "media-assets-example");
    is("токен читается", DeviceCredentials::Token() == "token-123");
    is("свежесохранённое не требует обновления", !DeviceCredentials::NeedsRefresh());

    // ---- ГЛАВНОЕ: на диске нет читаемых ключей ----

    {
        QFile f(QDir::current().filePath("device.dat"));
        is("файл создан", f.exists());
        f.open(QIODevice::ReadOnly);
        const auto raw = f.readAll();
        f.close();

        is("в файле нет секрета", !raw.contains("ochen-sekretnyj-secret-12345"));
        is("в файле нет ключа", !raw.contains("AKIAEXAMPLEKEY"));
        is("в файле нет производного ключа", !raw.contains("derived-device-psk-abcdef"));
        is("в файле нет адреса хранилища", !raw.contains("storage.example.net"));
        is("в файле нет имени корзины", !raw.contains("media-assets-example"));
        is("в файле нет токена", !raw.contains("token-123"));
        // Имена полей — тоже подсказка об устройстве, и они внутри того же
        // запечатанного блоба, а не рядом с ним.
        is("в файле нет даже имён полей", !raw.contains("endpoint") && !raw.contains("bucket"));
    }

    // ---- стирание ----

    DeviceCredentials::Wipe();
    is("после стирания реквизитов нет", !DeviceCredentials::IsProvisioned());
    is("поле пусто", DeviceCredentials::Field("bucket").isEmpty());
    is("токен пуст", DeviceCredentials::Token().isEmpty());
    is("состояние — не активировано", DeviceCredentials::CurrentState() == DeviceCredentials::SignedOut);
    // Ради этого пункта Wipe и написан так, как написан: новый идентификатор
    // после каждого «отключить» занимал бы человеку новый слот по тарифу.
    is("идентификатор установки пережил стирание", DeviceCredentials::DeviceId() == idBefore);

    // ---- состояние без реквизитов ----

    DeviceCredentials::Remember(DeviceCredentials::Expired, "Подписка закончилась");
    is("состояние запоминается отдельно от реквизитов",
       DeviceCredentials::CurrentState() == DeviceCredentials::Expired);
    is("и объяснение вместе с ним", DeviceCredentials::StateDetail() == "Подписка закончилась");
    is("реквизиты при этом не появились", !DeviceCredentials::IsProvisioned());


    // ---- разбор ответов сайта ----
    //
    // Ровно тот же протокол, что на Android (Provisioning.java). Здесь важно не
    // «разобралось», а КАКАЯ ветка выбрана: 402 требует стереть реквизиты, а
    // сетевой сбой — не трогать ничего. Перепутать значит либо отключить
    // человека на ровном месте, либо оставить ему нерабочие ключи.

    say("");
    say("RelayActivation");

    {
        QJsonObject full;
        full["endpoint"] = "storage.example.net";
        full["bucket"] = "b";
        full["key"] = "k";
        full["secret"] = "s";
        full["psk"] = "p";
        full["tag"] = "t";
        const auto fullBody = QJsonDocument(full).toJson(QJsonDocument::Compact);

        QJsonObject issued;

        // ГЛАВНОЕ РАЗЛИЧЕНИЕ. Ноль — сеть не ответила.
        auto net = RelayActivation::InterpretDevice(0, "", "host not found", &issued);
        is("сбой сети — не успех", !net.ok);
        is("сбой сети НЕ ведёт к стиранию", !net.revoked);
        is("и объясняет, что дело в связи", net.detail.contains("связаться"));

        auto gone = RelayActivation::InterpretDevice(
            402, R"({"canRenew":true})", "", &issued);
        is("402 — подписка кончилась", gone.state == DeviceCredentials::Expired);
        is("и предлагает продлить", gone.actionText == "Продлить");

        auto never = RelayActivation::InterpretDevice(402, R"({})", "", &issued);
        is("402 без canRenew — предлагает оформить", never.actionText == "Оформить");

        auto traffic = RelayActivation::InterpretDevice(
            402, R"({"code":"traffic_exhausted"})", "", &issued);
        is("402 про трафик говорит про трафик", traffic.detail.contains("Трафик"));

        auto limit = RelayActivation::InterpretDevice(409, R"({})", "", &issued);
        is("409 — лимит устройств", limit.state == DeviceCredentials::Limit);
        is("409 НЕ ведёт к стиранию", !limit.revoked);

        auto closed = RelayActivation::InterpretDevice(
            403, R"({"code":"relay_admin_only"})", "", &issued);
        is("403 relay_admin_only — канал не открыт", closed.state == DeviceCredentials::Closed);

        auto stale = RelayActivation::InterpretDevice(401, R"({})", "", &issued);
        is("401 — вход устарел, а не «нет подписки»", stale.state == DeviceCredentials::SignedOut);
        is("и не путается с подпиской", !stale.detail.contains("одписк"));
        is("401 НЕ ведёт к стиранию", !stale.revoked);
        is("а 402 ведёт, во всех трёх видах", gone.revoked && traffic.revoked && never.revoked);

        auto half = RelayActivation::InterpretDevice(200, R"({"endpoint":"x"})", "", &issued);
        is("неполная конфигурация отвергается", !half.ok);
        is("и наружу ничего не отдаёт", issued.isEmpty());

        auto good = RelayActivation::InterpretDevice(200, fullBody, "", &issued);
        is("полная конфигурация принимается", good.ok);
        is("состояние активно", good.state == DeviceCredentials::Active);
        is("и конфигурация отдана наружу", issued.value("bucket").toString() == "b");
    }

    {
        QString token;

        auto net = RelayActivation::InterpretRedeem(0, "", "timeout", &token);
        is("код: сбой сети — не отказ в доступе", !net.ok && token.isEmpty());

        auto bad = RelayActivation::InterpretRedeem(401, R"({})", "", &token);
        is("код не подошёл — сказано про 5 минут", bad.detail.contains("5 минут"));
        is("и предложено взять новый", bad.actionText == "Взять новый код");
        is("токен при отказе пуст", token.isEmpty());

        auto many = RelayActivation::InterpretRedeem(429, R"({})", "", &token);
        is("429 — про попытки, а не про код", many.detail.contains("попыток"));

        // Поле называется именно token. Прочитать не то — значит получить пустую
        // строку из ответа 200 и узнать об этом шагом позже, где отказ будет
        // выглядеть как неверный код. Ровно на этом сгорела публикация 4.7.
        auto wrongField = RelayActivation::InterpretRedeem(
            200, R"({"accessToken":"abc"})", "", &token);
        is("ответ без поля token — отказ, а не молчаливый успех", !wrongField.ok);
        is("и токен не выдуман", token.isEmpty());

        auto ok = RelayActivation::InterpretRedeem(
            200, R"({"token":"tok-1","email":"a@b.c"})", "", &token);
        is("успех отдаёт токен", ok.ok && token == "tok-1");
        is("и почту для показа", ok.detail == "a@b.c");
    }

    // ---- ключ сессии живёт отдельно от конфигурации ----
    //
    // ПОЧЕМУ ЭТО ОТДЕЛЬНАЯ ПРОВЕРКА. Активация идёт двумя шагами: код меняется
    // на ключ, ключом запрашиваются реквизиты. Между шагами ключ уже надо иметь
    // на руках — код одноразовый и второй раз не сработает.
    //
    // Сначала Redeem сохранял его через Save(token, {}), а Save отвергает
    // неполную конфигурацию и не пишет НИЧЕГО. Ключ молча терялся, Provision()
    // отвечал «не активировано», и выглядело это как неверный код. Активация
    // была сломана целиком, и заметить это без разбора было нечем.
    {
        DeviceCredentials::Wipe();
        is("ключ сохраняется без конфигурации", DeviceCredentials::SaveToken("tok-redeem"));
        is("и читается обратно", DeviceCredentials::Token() == "tok-redeem");
        is("но реквизитами это ещё не является", !DeviceCredentials::IsProvisioned());
        is("пустой ключ не сохраняется", !DeviceCredentials::SaveToken(""));
        is("и прежний при этом цел", DeviceCredentials::Token() == "tok-redeem");
    }

    say("");
    say(QString("проверок: %1, провалов: %2").arg(checks).arg(fails));
    std::fflush(stdout);
    return fails > 0 ? 1 : 0;
}
