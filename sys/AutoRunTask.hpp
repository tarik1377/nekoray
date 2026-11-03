#pragma once

#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

/**
 * Автозапуск на Windows задачей планировщика — с наивысшими правами.
 *
 * ЗАЧЕМ. Туннель в Windows по умолчанию встроенный (vpn_internal_tun), и ему
 * нужна вся программа с правами администратора. Из раздела Run программа
 * стартует без них, и при каждом входе в систему человек видел «Please run
 * GreenRhythm as admin», а за ним окно UAC — у каждого, кто включил
 * автозапуск и пользуется туннелем. Задача «при входе этого человека, с
 * наивысшими правами» запускает программу сразу с нужными правами; разрешение
 * Windows спрашивает один раз — когда задачу создают.
 *
 * УМОЛЧАНИЯ ПЛАНИРОВЩИКА ЗДЕСЬ ВРЕДНЫ, поэтому заданы явно: без них задача не
 * стартует на батарее (ноутбук без зарядки остался бы без VPN) и убивается
 * через 72 часа работы (у автозапуска это обычный срок).
 *
 * Сценарий — текст для powershell -EncodedCommand (sys/WinShell.hpp): файла со
 * сценарием нет, подменить между записью и запуском с правами нечего.
 *
 * Чистые функции, без Windows: сторож — test/AutoRunTaskTest.cpp.
 */
namespace GreenRhythm::AutoRunTask {
    struct Launch {
        QString executable, arguments, directory, user;
        bool valid = false, present = false, queryKnown = false;
    };
    inline QString normalized(QString path) {
        path = path.trimmed();
        if (path.startsWith('"') && path.endsWith('"')) path = path.mid(1, path.size() - 2);
        path.replace('\\', '/');
        while (path.endsWith('/') && path.size() > 3) path.chop(1);
        return path.toCaseFolded();
    }
    inline bool ours(const QString &exe) { return normalized(exe).endsWith("/greenrhythm.exe"); }
    inline Launch parse(const QByteArray &xml) {
        if (xml.size() > 1024 * 1024) return {};
        auto payload = xml;
        if (payload.startsWith(QByteArray::fromHex("efbbbf"))) payload.remove(0, 3);
        // Export-ScheduledTask returns a Unicode string declaring UTF-16;
        // our PowerShell transport deliberately encodes that string as UTF-8.
        if (!payload.contains('\0') && payload.startsWith("<?xml")) {
            const auto end = payload.indexOf("?>");
            if (end >= 0 && end < 256) payload.remove(0, end + 2);
        }
        QXmlStreamReader reader(payload); Launch launch; int actions = 0, principals = 0;
        bool exec = false, principal = false, otherAction = false;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isDTD() || reader.isEntityReference()) return {};
            if (reader.isStartElement()) {
                const auto name = reader.name();
                if (name == QLatin1String("Exec")) { exec = true; ++actions; }
                else if (name == QLatin1String("Principal")) { principal = true; ++principals; }
                else if (name == QLatin1String("ComHandler")) otherAction = true;
                else if (exec && name == QLatin1String("Command")) launch.executable = reader.readElementText();
                else if (exec && name == QLatin1String("Arguments")) launch.arguments = reader.readElementText();
                else if (exec && name == QLatin1String("WorkingDirectory")) launch.directory = reader.readElementText();
                else if (principal && name == QLatin1String("UserId")) launch.user = reader.readElementText();
            } else if (reader.isEndElement()) {
                if (reader.name() == QLatin1String("Exec")) exec = false;
                if (reader.name() == QLatin1String("Principal")) principal = false;
            }
        }
        launch.valid = !reader.hasError() && actions == 1 && principals == 1 && !otherAction
            && ours(launch.executable) && !launch.user.isEmpty();
        return launch;
    }
    inline bool owned(const Launch &launch, const QStringList &identities) {
        return launch.valid && identities.contains(launch.user, Qt::CaseInsensitive);
    }
    inline bool matches(const Launch &launch, const QStringList &identities, const QString &exe,
                        const QString &directory, const QString &arguments) {
        return owned(launch, identities) && normalized(launch.executable) == normalized(exe)
            && normalized(launch.directory) == normalized(directory) && launch.arguments.trimmed() == arguments.trimmed();
    }

    /// Имя задачи в планировщике.
    inline const QString kTaskName = QStringLiteral("GreenRhythm");

    /** Строка в одинарных кавычках PowerShell: сама кавычка удваивается. */
    inline QString quoted(const QString &text) {
        return QLatin1Char('\'') + QString(text).replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\'');
    }

    /**
     * Сценарий регистрации: при входе этого человека (user — «ДОМЕН\имя», как его
     * видит программа до повышения прав: при чужих учётных данных в окне UAC
     * задача иначе досталась бы администратору), с наивысшими правами, запуск
     * в трей. -Force — повторное включение переписывает задачу, если программа
     * переехала в другую папку.
     */
    inline QString registerScript(const QString &exe, const QString &workingDir, const QString &user,
                                  const QString &arguments = QStringLiteral("-tray"), bool replaceExisting = true) {
        return QStringLiteral(
                   "$ErrorActionPreference = 'Stop'\n"
                   "$action = New-ScheduledTaskAction -Execute %1 -Argument %5 -WorkingDirectory %2\n"
                   "$trigger = New-ScheduledTaskTrigger -AtLogOn -User %3\n"
                   "$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries"
                   " -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew\n"
                   "$principal = New-ScheduledTaskPrincipal -UserId %3 -LogonType Interactive -RunLevel Highest\n"
                   "Register-ScheduledTask -TaskName %4 -Action $action -Trigger $trigger -Settings $settings"
                   " -Principal $principal%6 | Out-Null\n")
            .arg(quoted(exe), quoted(workingDir), quoted(user), quoted(kTaskName), quoted(arguments),
                 replaceExisting ? QStringLiteral(" -Force") : QString());
    }

    /** Сценарий снятия задачи. */
    inline QString unregisterScript() {
        return QStringLiteral("$ErrorActionPreference = 'Stop'\n"
                              "Unregister-ScheduledTask -TaskName %1 -Confirm:$false\n")
            .arg(quoted(kTaskName));
    }

} // namespace GreenRhythm::AutoRunTask
