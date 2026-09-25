#pragma once

#include <QString>

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
    inline QString registerScript(const QString &exe, const QString &workingDir, const QString &user) {
        return QStringLiteral(
                   "$ErrorActionPreference = 'Stop'\n"
                   "$action = New-ScheduledTaskAction -Execute %1 -Argument '-tray' -WorkingDirectory %2\n"
                   "$trigger = New-ScheduledTaskTrigger -AtLogOn -User %3\n"
                   "$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries"
                   " -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew\n"
                   "$principal = New-ScheduledTaskPrincipal -UserId %3 -LogonType Interactive -RunLevel Highest\n"
                   "Register-ScheduledTask -TaskName %4 -Action $action -Trigger $trigger -Settings $settings"
                   " -Principal $principal -Force | Out-Null\n")
            .arg(quoted(exe), quoted(workingDir), quoted(user), quoted(kTaskName));
    }

    /** Сценарий снятия задачи. */
    inline QString unregisterScript() {
        return QStringLiteral("$ErrorActionPreference = 'Stop'\n"
                              "Unregister-ScheduledTask -TaskName %1 -Confirm:$false\n")
            .arg(quoted(kTaskName));
    }

} // namespace GreenRhythm::AutoRunTask
