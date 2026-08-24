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
#include "main/SealedStore.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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

    say("");
    say(QString("проверок: %1, провалов: %2").arg(checks).arg(fails));
    std::fflush(stdout);
    return fails > 0 ? 1 : 0;
}
