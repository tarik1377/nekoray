#pragma once

#include <QRegularExpression>
#include <QString>

/**
 * Тихая проверка обновлений: когда спрашивать и как назвать найденное.
 *
 * Раньше проверка шла только по кнопке, и человек на сборке со сломанной
 * кнопкой питания о починке не узнавал вовсе. Теперь окно спрашивает само —
 * минуту после запуска и дальше раз в сутки, — а найденную версию показывает
 * строкой в колонке, без окон: окно посреди работы раздражает, а строка ждёт,
 * пока человек сам решит обновиться.
 *
 * Ядро отвечает именем пакета, а не номером версии (go/grpc_server/update.go:
 * имя берётся из адреса, чтобы совпадать с тем, что человек увидит в
 * загрузках). Номер для строки достаётся отсюда же.
 *
 * Сторож — test/SettingsPageTest.cpp.
 */
namespace GreenRhythm::Update {

    /// Первая проверка — через минуту: ядро успеет подняться, а человек —
    /// начать работать, не упираясь в сеть на старте.
    inline constexpr int kFirstCheckMs = 60 * 1000;
    /// Дальше — раз в сутки, пока программа работает: с автозапуском это недели.
    inline constexpr int kCheckEveryMs = 24 * 60 * 60 * 1000;

    /** «GreenRhythm-v1.8.4-windows-x64.zip» → «1.8.4»; пусто — номера в имени нет. */
    inline QString versionFromAsset(const QString &asset) {
        static const QRegularExpression version(QStringLiteral(R"re((?:^|[^0-9.])v?(\d+\.\d+(?:\.\d+)?)(?![0-9]))re"));
        const auto match = version.match(asset);
        return match.hasMatch() ? match.captured(1) : QString();
    }

} // namespace GreenRhythm::Update
