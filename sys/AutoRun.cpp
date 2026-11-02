#include "AutoRun.hpp"

#include <QApplication>
#include <QDir>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
// WinCommander exposes these constants as class members.
#undef SW_HIDE
#undef SW_NORMAL
#undef SW_SHOWMINIMIZED
#endif

#include "3rdparty/fix_old_qt.h"
#include "main/NekoGui.hpp"
#include "main/UserDataPaths.hpp"

// macOS headers (possibly OBJ-c)
#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#endif

#ifdef Q_OS_WIN

#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QRegularExpression>
#include <QStandardPaths>

#include "3rdparty/WinCommander.hpp"
#include "sys/AutoRunTask.hpp"
#include "sys/WinShell.hpp"

QString Windows_GenAutoRunArguments() {
    QStringList encoded;
    const auto *store = NekoGui::dataStore;
    for (const auto &argument : GreenRhythm::UserData::guiArguments(store->appdataDir, store->flag_use_appdata, true))
        encoded << (argument.contains(QRegularExpression("[\\s\"]")) || argument.endsWith('\\')
            ? GreenRhythm::UserData::quoteWindowsArgument(argument) : argument);
    return encoded.join(' ');
}
QString Windows_GenAutoRunString() {
    auto appPath = QApplication::applicationFilePath();
    appPath = "\"" + QDir::toNativeSeparators(appPath) + "\"";
    appPath += ' ' + Windows_GenAutoRunArguments();
    return appPath;
}

namespace {
    const QString kRunKey = QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run");

    /** Имя записи в Run — как было всегда: имя файла программы. */
    QString runName() { return QFileInfo(QApplication::applicationFilePath()).baseName(); }

    bool runKeySet() {
        QSettings settings(kRunKey, QSettings::NativeFormat);
        const auto command = settings.value(runName()).toString().trimmed();
        const auto match = QRegularExpression(QStringLiteral("^(?:\"([^\"]+)\"|(\\S+))")).match(command);
        return match.hasMatch() && GreenRhythm::AutoRunTask::ours(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1));
    }

    QStringList identities() {
        QStringList values{qEnvironmentVariable("USERDOMAIN") + '\\' + qEnvironmentVariable("USERNAME")};
        HANDLE token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            DWORD needed = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
            QByteArray bytes(int(needed), 0);
            if (needed && GetTokenInformation(token, TokenUser, bytes.data(), needed, &needed)) {
                LPWSTR sid = nullptr;
                if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid, &sid)) {
                    values << QString::fromWCharArray(sid); LocalFree(sid);
                }
            }
            CloseHandle(token);
        }
        return values;
    }
    GreenRhythm::AutoRunTask::Launch taskLaunch() {
        QProcess query;
        const auto script = QStringLiteral("$ErrorActionPreference='Stop'\n"
            "try { $xml=Export-ScheduledTask -TaskPath '\\' -TaskName %1 -ErrorAction Stop; [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); [Console]::Write($xml) }\n"
            "catch { if ($_.CategoryInfo.Category -eq [Management.Automation.ErrorCategory]::ObjectNotFound) { exit 3 }; exit 4 }\n")
            .arg(GreenRhythm::AutoRunTask::quoted(GreenRhythm::AutoRunTask::kTaskName));
        query.start(PowerShellPath(), {"-NoProfile", "-NonInteractive", "-EncodedCommand", PowerShellEncode(script)});
        if (!query.waitForFinished(5000) || query.exitStatus() != QProcess::NormalExit) return {};
        GreenRhythm::AutoRunTask::Launch result;
        if (query.exitCode() == 0) {
            result = GreenRhythm::AutoRunTask::parse(query.readAllStandardOutput());
            result.present = result.queryKnown = true;
        } else result.queryKnown = query.exitCode() == 3;
        return result;
    }

    /** Задача в планировщике есть. Смотреть её можно и без прав — проверено на живой машине. */
    bool taskExists() {
        return GreenRhythm::AutoRunTask::owned(taskLaunch(), identities());
    }

    /**
     * Сценарий PowerShell с правами администратора: уже с правами — сразу, нет —
     * через окно UAC (runProcessElevated), человек разрешает один раз. Отказ в
     * окне — процесс не запущен, код не ноль. Сценарий — через -EncodedCommand:
     * файла, который можно подменить до запуска с правами, нет.
     */
    bool runElevatedScript(const QString &script) {
        const QStringList args{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                               QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                               QStringLiteral("-EncodedCommand"), PowerShellEncode(script)};
        if (NekoGui::IsAdmin()) {
            QProcess shell;
            shell.start(PowerShellPath(), args);
            return shell.waitForFinished(60000) && shell.exitStatus() == QProcess::NormalExit && shell.exitCode() == 0;
        }
        return WinCommander::runProcessElevated(PowerShellPath(), args, QString(), WinCommander::SW_HIDE, true) == 0;
    }
} // namespace

void AutoRun_SetEnabled(bool enable) {
    QSettings settings(kRunKey, QSettings::NativeFormat);
    const QString name = runName();
    if (enable) {
        // Сначала — задача с наивысшими правами (sys/AutoRunTask.hpp): туннелю
        // при входе нужны права администратора, а из Run программа стартует без
        // них — и при каждом входе спрашивала «запустите от администратора» и UAC.
        const QString exe = QDir::toNativeSeparators(QApplication::applicationFilePath());
        const QString dir = QDir::toNativeSeparators(QApplication::applicationDirPath());
        const QString user = qEnvironmentVariable("USERDOMAIN") + QLatin1Char('\\') + qEnvironmentVariable("USERNAME");
        const auto task = taskLaunch();
        const bool owned = GreenRhythm::AutoRunTask::owned(task, identities());
        if (task.present && !task.valid) { qWarning("autorun: task XML is unknown; existing registration retained"); return; }
        // An unknown/foreign task must never be overwritten with -Force.
        if ((!task.valid || owned) && runElevatedScript(GreenRhythm::AutoRunTask::registerScript(exe, dir, user, Windows_GenAutoRunArguments(), owned))) {
            settings.remove(name); // запускает задача — второй запуск из Run не нужен
            return;
        }
        // Отказ в окне UAC или сбой — прежний путь: автозапуск работает, туннель
        // при входе спросит разрешение, как и раньше.
        if (!owned && task.queryKnown) settings.setValue(name, Windows_GenAutoRunString());
        else qWarning("autorun: existing task could not be repaired; no duplicate Run entry was added");
    } else {
        settings.remove(name);
        if (taskExists()) runElevatedScript(GreenRhythm::AutoRunTask::unregisterScript());
    }
}

bool AutoRun_IsEnabled() {
    return runKeySet() || taskExists();
}

bool AutoRun_IsElevated() {
    return GreenRhythm::AutoRunTask::matches(taskLaunch(), identities(), QApplication::applicationFilePath(),
        QApplication::applicationDirPath(), Windows_GenAutoRunArguments());
}

QStringList AutoRun_PreviousConfigDirectories() {
    QStringList configs;
    const auto standard = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto add = [&](QString executable, const QStringList &arguments) {
        executable.replace('\\', '/');
        for (const auto &path : {GreenRhythm::UserData::legacyConfigDirectory(executable, arguments, standard),
                                QFileInfo(executable).absoluteDir().absoluteFilePath("config")}) {
            if (!configs.contains(path, Qt::CaseInsensitive)) configs << path;
        }
    };
    QSettings settings(kRunKey, QSettings::NativeFormat);
    const auto command = settings.value(runName()).toString().trimmed();
    const auto match = QRegularExpression(QStringLiteral("^(?:\"([^\"]+)\"|(\\S+))")).match(command);
    const auto run = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    if (GreenRhythm::AutoRunTask::ours(run)) add(run, QProcess::splitCommand(command.mid(match.capturedLength()).trimmed()));
    const auto task = taskLaunch();
    if (GreenRhythm::AutoRunTask::owned(task, identities())) add(task.executable, QProcess::splitCommand(task.arguments));
    return configs;
}

#endif

#ifndef Q_OS_WIN
QStringList AutoRun_PreviousConfigDirectories() { return {}; }
#endif

#ifdef Q_OS_MACOS

#include <objc/message.h>
#include <objc/runtime.h>

namespace {
    /*
     * ПУНКТ ВХОДА — ЧЕРЕЗ SMAppService (macOS 13+). LSSharedFileList ниже
     * устарел ещё в 10.11 и на 13+ молча ничего не делает, а переключатель
     * теперь на странице «Настройки». Рантайм Objective-C, а не .mm: класс
     * ищется по имени, и на 12-й (минимальная версия сборки) его просто нет —
     * тогда остаётся прежний путь. Проверяет тестировщик на живом маке.
     */
    template<class R, class... A>
    R send(id target, const char *selector, A... args) {
        return reinterpret_cast<R (*)(id, SEL, A...)>(objc_msgSend)(target, sel_registerName(selector), args...);
    }

    id serviceClass() { return reinterpret_cast<id>(objc_getClass("SMAppService")); }

    id mainAppService() {
        id cls = serviceClass();
        return cls == nullptr ? nullptr : send<id>(cls, "mainAppService");
    }

    constexpr long kStatusEnabled = 1;          // SMAppServiceStatusEnabled
    constexpr long kStatusRequiresApproval = 2; // SMAppServiceStatusRequiresApproval

    /** Ошибку — в журнал: тестировщику на маке больше не на что смотреть. */
    void logError(const char *what, id error) {
        if (error == nullptr) return;
        id text = send<id>(error, "localizedDescription");
        const char *utf8 = text != nullptr ? send<const char *>(text, "UTF8String") : nullptr;
        qWarning("autorun: %s: %s", what, utf8 != nullptr ? utf8 : "?");
    }
} // namespace

bool AutoRun_IsElevated() {
    return false;
}

void AutoRun_SetEnabled(bool enable) {
    if (id service = mainAppService()) {
        id error = nullptr;
        const bool ok = send<BOOL>(service, enable ? "registerAndReturnError:" : "unregisterAndReturnError:", &error);
        if (!ok) logError(enable ? "register" : "unregister", error);
        // Система может потребовать одобрения в «Объектах входа». Молчать нельзя:
        // переключатель остался бы выключенным без объяснений.
        if (enable && send<long>(service, "status") == kStatusRequiresApproval)
            send<void>(serviceClass(), "openSystemSettingsLoginItems");
        return;
    }
    // From
    // https://github.com/nextcloud/desktop/blob/master/src/common/utility_mac.cpp
    QString filePath = QDir(QCoreApplication::applicationDirPath() + QLatin1String("/../..")).absolutePath();
    CFStringRef folderCFStr = CFStringCreateWithCString(0, filePath.toUtf8().data(), kCFStringEncodingUTF8);
    CFURLRef urlRef = CFURLCreateWithFileSystemPath(0, folderCFStr, kCFURLPOSIXPathStyle, true);
    LSSharedFileListRef loginItems = LSSharedFileListCreate(0, kLSSharedFileListSessionLoginItems, 0);

    if (loginItems && enable) {
        // Insert an item to the list.
        LSSharedFileListItemRef item =
            LSSharedFileListInsertItemURL(loginItems, kLSSharedFileListItemLast, 0, 0, urlRef, 0, 0);

        if (item) CFRelease(item);

        CFRelease(loginItems);
    } else if (loginItems && !enable) {
        // We need to iterate over the items and check which one is "ours".
        UInt32 seedValue;
        CFArrayRef itemsArray = LSSharedFileListCopySnapshot(loginItems, &seedValue);
        CFStringRef appUrlRefString = CFURLGetString(urlRef);

        for (int i = 0; i < CFArrayGetCount(itemsArray); i++) {
            LSSharedFileListItemRef item = (LSSharedFileListItemRef) CFArrayGetValueAtIndex(itemsArray, i);
            CFURLRef itemUrlRef = NULL;

            if (LSSharedFileListItemResolve(item, 0, &itemUrlRef, NULL) == noErr && itemUrlRef) {
                CFStringRef itemUrlString = CFURLGetString(itemUrlRef);

                if (CFStringCompare(itemUrlString, appUrlRefString, 0) == kCFCompareEqualTo) {
                    LSSharedFileListItemRemove(loginItems, item); // remove it!
                }

                CFRelease(itemUrlRef);
            }
        }

        CFRelease(itemsArray);
        CFRelease(loginItems);
    }

    CFRelease(folderCFStr);
    CFRelease(urlRef);
}

bool AutoRun_IsEnabled() {
    if (id service = mainAppService()) return send<long>(service, "status") == kStatusEnabled;
    // From
    // https://github.com/nextcloud/desktop/blob/master/src/common/utility_mac.cpp
    // this is quite some duplicate code with setLaunchOnStartup, at some
    // point we should fix this FIXME.
    bool returnValue = false;
    QString filePath = QDir(QCoreApplication::applicationDirPath() + QLatin1String("/../..")).absolutePath();
    CFStringRef folderCFStr = CFStringCreateWithCString(0, filePath.toUtf8().data(), kCFStringEncodingUTF8);
    CFURLRef urlRef = CFURLCreateWithFileSystemPath(0, folderCFStr, kCFURLPOSIXPathStyle, true);
    LSSharedFileListRef loginItems = LSSharedFileListCreate(0, kLSSharedFileListSessionLoginItems, 0);

    if (loginItems) {
        // We need to iterate over the items and check which one is "ours".
        UInt32 seedValue;
        CFArrayRef itemsArray = LSSharedFileListCopySnapshot(loginItems, &seedValue);
        CFStringRef appUrlRefString = CFURLGetString(urlRef); // no need for release

        for (int i = 0; i < CFArrayGetCount(itemsArray); i++) {
            LSSharedFileListItemRef item = (LSSharedFileListItemRef) CFArrayGetValueAtIndex(itemsArray, i);
            CFURLRef itemUrlRef = NULL;

            if (LSSharedFileListItemResolve(item, 0, &itemUrlRef, NULL) == noErr && itemUrlRef) {
                CFStringRef itemUrlString = CFURLGetString(itemUrlRef);

                if (CFStringCompare(itemUrlString, appUrlRefString, 0) == kCFCompareEqualTo) {
                    returnValue = true;
                }

                CFRelease(itemUrlRef);
            }
        }

        CFRelease(itemsArray);
    }

    CFRelease(loginItems);
    CFRelease(folderCFStr);
    CFRelease(urlRef);
    return returnValue;
}

#endif

#ifdef Q_OS_LINUX

#include <QStandardPaths>
#include <QProcessEnvironment>

bool AutoRun_IsElevated() {
    return false;
}
#include <QTextStream>

#define NEWLINE "\n"

//  launchatlogin.cpp
//  ShadowClash
//
//  Created by TheWanderingCoel on 2018/6/12.
//  Copyright © 2019 Coel Wu. All rights reserved.
//
QString getUserAutostartDir_private() {
    QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    config += QLatin1String("/autostart/");
    return config;
}

void AutoRun_SetEnabled(bool enable) {
    // From https://github.com/nextcloud/desktop/blob/master/src/common/utility_unix.cpp
    QString appName = QCoreApplication::applicationName();
    QString userAutoStartPath = getUserAutostartDir_private();
    QString desktopFileLocation = userAutoStartPath + appName + QLatin1String(".desktop");
    QStringList appCmdList;

    // nekoray: launcher
    if (qEnvironmentVariable("NKR_FROM_LAUNCHER") == "1") {
        appCmdList << QApplication::applicationDirPath() + "/launcher"
                   << "--";
    } else {
        if (QProcessEnvironment::systemEnvironment().contains("APPIMAGE")) {
            appCmdList << QProcessEnvironment::systemEnvironment().value("APPIMAGE");
        } else {
            appCmdList << QApplication::applicationFilePath();
        }
    }

    appCmdList << "-tray";

    if (NekoGui::dataStore->flag_use_appdata) {
        appCmdList << "-appdata";
    }

    if (enable) {
        if (!QDir().exists(userAutoStartPath) && !QDir().mkpath(userAutoStartPath)) {
            // qCWarning(lcUtility) << "Could not create autostart folder"
            // << userAutoStartPath;
            return;
        }

        QFile iniFile(desktopFileLocation);

        if (!iniFile.open(QIODevice::WriteOnly)) {
            // qCWarning(lcUtility) << "Could not write auto start entry" <<
            // desktopFileLocation;
            return;
        }

        QTextStream ts(&iniFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        ts.setCodec("UTF-8");
#endif
        ts << QLatin1String("[Desktop Entry]") << NEWLINE
           << QLatin1String("Name=") << appName << NEWLINE
           << QLatin1String("Exec=") << appCmdList.join(" ") << NEWLINE
           << QLatin1String("Terminal=") << "false" << NEWLINE
           << QLatin1String("Categories=") << "Network" << NEWLINE
           << QLatin1String("Type=") << "Application" << NEWLINE
           << QLatin1String("StartupNotify=") << "false" << NEWLINE
           << QLatin1String("X-GNOME-Autostart-enabled=") << "true" << NEWLINE;
        ts.flush();
        iniFile.close();
    } else {
        QFile::remove(desktopFileLocation);
    }
}

bool AutoRun_IsEnabled() {
    QString appName = QCoreApplication::applicationName();
    QString desktopFileLocation = getUserAutostartDir_private() + appName + QLatin1String(".desktop");
    return QFile::exists(desktopFileLocation);
}

#endif
