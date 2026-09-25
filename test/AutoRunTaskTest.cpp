/**
 * АВТОЗАПУСК: WINDOWS — ЗАДАЧЕЙ С НАИВЫСШИМИ ПРАВАМИ, МАК — ЧЕРЕЗ SMAppService.
 *
 * ЖИВОЙ СЛУЧАЙ (аудит 25.09.2026). Переключатель «Запуск вместе с системой»
 * вынесен на страницу «Настройки», и два его изъяна стали видны всем:
 *
 *  — Windows. Туннель встроенный и требует прав администратора, а из раздела
 *    Run программа стартует без них: при каждом входе — «Please run GreenRhythm
 *    as admin» и окно UAC. Теперь автозапуск — задача планировщика «при входе,
 *    с наивысшими правами»; разрешение Windows спрашивает один раз;
 *  — мак. LSSharedFileList устарел ещё в 10.11 и на macOS 13+ молча ничего не
 *    делает. Теперь — SMAppService, прежний путь остаётся для 12-й.
 *
 * Первая часть проверяет сценарий регистрации (sys/AutoRunTask.hpp): умолчания
 * планировщика, которые здесь вредны, заданы явно; кавычки в пути не ломают
 * сценарий. Вторая — сторож исходников: кто создаёт задачу, где окно UAC, что
 * делает мак и что окно не зависает, пока человек отвечает Windows.
 *
 * Только чтение файлов и строки: ни планировщика, ни окон.
 *
 * Запуск: ninja autorun_task_test && ./autorun_task_test
 */

#include "sys/AutoRunTask.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstdio>

static int checks = 0;
static int fails = 0;

static void is(const QString &what, bool ok) {
    checks++;
    if (!ok) fails++;
    std::fputs((QString(ok ? QStringLiteral("  ok    ") : QStringLiteral("  ПРОВАЛ "))
                + what + QStringLiteral("\n"))
                   .toUtf8()
                   .constData(),
               stdout);
}

/** Исходник целиком, переводы строк — «\n». */
static QString slurp(const QString &path) {
    for (const QString &prefix: {QStringLiteral(""), QStringLiteral("../"), QStringLiteral("../../")}) {
        QFile f(prefix + path);
        if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    }
    return {};
}

/** Кусок исходника между двумя метками; пусто — меток нет. */
static QString between(const QString &text, const QString &from, const QString &to) {
    const int a = text.indexOf(from);
    if (a < 0) return {};
    const int b = text.indexOf(to, a + from.size());
    return b > a ? text.mid(a, b - a) : QString();
}

static void registerScriptIsRight() {
    std::puts("Сценарий регистрации задачи");
    namespace T = GreenRhythm::AutoRunTask;
    const QString script = T::registerScript(QStringLiteral("C:/Program Files/GreenRhythm/greenrhythm.exe"),
                                             QStringLiteral("C:/Program Files/GreenRhythm"),
                                             QStringLiteral("DESKTOP-1\\anna"));
    is(QStringLiteral("при входе этого человека"), script.contains(QStringLiteral("-AtLogOn -User 'DESKTOP-1\\anna'")));
    is(QStringLiteral("с наивысшими правами — ради этого задача и нужна"),
       script.contains(QStringLiteral("-RunLevel Highest")) &&
           script.contains(QStringLiteral("-UserId 'DESKTOP-1\\anna' -LogonType Interactive")));
    is(QStringLiteral("стартует и на батарее"), script.contains(QStringLiteral("-AllowStartIfOnBatteries")));
    is(QStringLiteral("не останавливается, когда ноутбук снимают с зарядки"),
       script.contains(QStringLiteral("-DontStopIfGoingOnBatteries")));
    is(QStringLiteral("не убивается через 72 часа"), script.contains(QStringLiteral("-ExecutionTimeLimit ([TimeSpan]::Zero)")));
    is(QStringLiteral("вторая копия не запускается"), script.contains(QStringLiteral("-MultipleInstances IgnoreNew")));
    is(QStringLiteral("запуск в трей, из папки программы"),
       script.contains(QStringLiteral("-Execute 'C:/Program Files/GreenRhythm/greenrhythm.exe' -Argument '-tray'")) &&
           script.contains(QStringLiteral("-WorkingDirectory 'C:/Program Files/GreenRhythm'")));
    is(QStringLiteral("повторное включение переписывает задачу (-Force)"),
       script.contains(QStringLiteral("Register-ScheduledTask -TaskName 'GreenRhythm'")) &&
           script.contains(QStringLiteral("-Force")));
    is(QStringLiteral("ошибка любого шага — ненулевой код, а не молчаливый успех"),
       script.startsWith(QStringLiteral("$ErrorActionPreference = 'Stop'")));

    const QString quote = T::registerScript(QStringLiteral("C:/Users/O'Neil/GreenRhythm/greenrhythm.exe"),
                                            QStringLiteral("C:/Users/O'Neil/GreenRhythm"), QStringLiteral("PC\\O'Neil"));
    is(QStringLiteral("кавычка в пути и имени удваивается — сценарий не ломается"),
       quote.contains(QStringLiteral("-Execute 'C:/Users/O''Neil/GreenRhythm/greenrhythm.exe'")) &&
           quote.contains(QStringLiteral("-User 'PC\\O''Neil'")));

    const QString off = T::unregisterScript();
    is(QStringLiteral("снятие — та же задача, без вопроса «вы уверены»"),
       off.contains(QStringLiteral("Unregister-ScheduledTask -TaskName 'GreenRhythm' -Confirm:$false")) &&
           off.startsWith(QStringLiteral("$ErrorActionPreference = 'Stop'")));
}

static void sourcesWired() {
    std::puts("Исходники: Windows, мак, окно");
    const QString autorun = slurp(QStringLiteral("sys/AutoRun.cpp"));
    const QString header = slurp(QStringLiteral("sys/AutoRun.hpp"));
    const QString window = slurp(QStringLiteral("ui/mainwindow.cpp"));
    const QString mac = slurp(QStringLiteral("cmake/macos/macos.cmake"));
    is(QStringLiteral("исходники прочитаны"), !autorun.isEmpty() && !header.isEmpty() && !window.isEmpty() && !mac.isEmpty());

    const QString win = between(autorun, QStringLiteral("#ifdef Q_OS_WIN"), QStringLiteral("#ifdef Q_OS_MACOS"));
    is(QStringLiteral("Windows: задача — сценарием через -EncodedCommand, без файла"),
       win.contains(QStringLiteral("GreenRhythm::AutoRunTask::registerScript(")) &&
           win.contains(QStringLiteral("PowerShellEncode(")) && win.contains(QStringLiteral("-EncodedCommand")));
    is(QStringLiteral("Windows: без прав — окно UAC через runProcessElevated, с правами — сразу"),
       win.contains(QStringLiteral("WinCommander::runProcessElevated(")) && win.contains(QStringLiteral("NekoGui::IsAdmin()")));
    is(QStringLiteral("Windows: задача создана — запись в Run снимается, двух запусков нет"),
       win.contains(QStringLiteral("settings.remove(name)")));
    is(QStringLiteral("Windows: отказ в UAC — прежний запуск через Run, автозапуск всё равно работает"),
       win.contains(QStringLiteral("settings.setValue(name, Windows_GenAutoRunString())")));
    is(QStringLiteral("Windows: включён ли — по Run и по задаче в планировщике"),
       win.contains(QStringLiteral("schtasks.exe")) && win.contains(QStringLiteral("/Query")));
    is(QStringLiteral("Windows: окно узнаёт, что автозапуск идёт с правами"),
       header.contains(QStringLiteral("bool AutoRun_IsElevated();")));

    const QString macSrc = between(autorun, QStringLiteral("#ifdef Q_OS_MACOS"), QStringLiteral("#ifdef Q_OS_LINUX"));
    is(QStringLiteral("мак: SMAppService.mainAppService, класс ищется по имени — на 12-й его нет"),
       macSrc.contains(QStringLiteral("objc_getClass(\"SMAppService\")")) &&
           macSrc.contains(QStringLiteral("\"mainAppService\"")));
    is(QStringLiteral("мак: регистрация и снятие через SMAppService"),
       macSrc.contains(QStringLiteral("\"registerAndReturnError:\"")) &&
           macSrc.contains(QStringLiteral("\"unregisterAndReturnError:\"")));
    is(QStringLiteral("мак: нужно одобрение в Настройках — открываем их, а не молчим"),
       macSrc.contains(QStringLiteral("\"openSystemSettingsLoginItems\"")));
    is(QStringLiteral("мак: на 12-й остаётся прежний LSSharedFileList"),
       macSrc.contains(QStringLiteral("LSSharedFileListCreate")));
    is(QStringLiteral("мак: ServiceManagement прилинкован явно, а не транзитивно через Qt"),
       mac.contains(QStringLiteral("-framework ServiceManagement")));

    is(QStringLiteral("окно: переключатель на странице не вешает окно, пока человек отвечает Windows"),
       between(window, QStringLiteral("MainShell::autostartToggled"), QStringLiteral("});"))
           .contains(QStringLiteral("runOnNewThread")));
    is(QStringLiteral("окно: пункт меню — так же"),
       between(window, QStringLiteral("actionStart_with_system, &QAction::triggered"), QStringLiteral("});"))
           .contains(QStringLiteral("runOnNewThread")));
    is(QStringLiteral("окно: программа с правами, а автозапуск ещё через Run — разово переносим в задачу"),
       window.contains(QStringLiteral("AutoRun_IsElevated()")) && window.contains(QStringLiteral("NekoGui::IsAdmin()")));
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    registerScriptIsRight();
    sourcesWired();
    std::printf("\n%d проверок, провалено %d\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
