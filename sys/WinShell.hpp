#pragma once

#include <QString>

#ifdef Q_OS_WIN

/**
 * Полный путь к powershell.exe.
 *
 * ЗАЧЕМ НЕ ПРОСТО "powershell". Неполное имя Windows разрешает в том числе через
 * каталог приложения и ТЕКУЩИЙ каталог, и лишь потом через System32. Текущий
 * каталог у нас — config: приложение само его создаёт, само складывает туда
 * пользовательские данные и предлагает открыть пунктом меню. Положенный туда
 * powershell.exe выполнится вместо системного — а один из наших вызовов идёт с
 * правами администратора.
 *
 * Правило в этом репозитории уже записано, только для другой платформы:
 * sys/macos/MacProxyController.cpp — «путь к networksetup абсолютный: PATH нам
 * не принадлежит». До Windows оно не дошло, хотя цена ошибки здесь выше.
 *
 * SystemRoot берётся из окружения, а не прибит: система бывает и не на C:.
 * Запасное значение — на случай пустого окружения, а не как ожидаемый путь.
 */
inline QString System32Exe(const QString &name) {
    const auto root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    return root + QStringLiteral("/System32/") + name;
}

inline QString PowerShellPath() {
    return System32Exe(QStringLiteral("WindowsPowerShell/v1.0/powershell.exe"));
}

/**
 * Скрипт в виде, пригодном для -EncodedCommand: UTF-16LE в Base64.
 *
 * ЗАЧЕМ ЭТО ВООБЩЕ. Так исполняемого файла не существует — значит, его нечем
 * подменить между записью и запуском. Прежде тело скрипта писалось в общий
 * %TEMP% под постоянным именем и оттуда запускалось с правами администратора,
 * причём между записью и запуском проходило до минуты.
 */
inline QString PowerShellEncode(const QString &script) {
    const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()),
                           static_cast<int>(script.size()) * 2);
    return QString::fromLatin1(utf16.toBase64());
}

#endif
