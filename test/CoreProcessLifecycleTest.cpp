#include "main/NekoGui.hpp"
#include "sys/ExternalProcess.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

namespace NekoGui {
    DataStore::DataStore() : JsonStore() {}
    ExtraCore::ExtraCore() : JsonStore() {}
    InboundAuthorization::InboundAuthorization() : JsonStore() {}
    DataStore *dataStore = new DataStore;
}
namespace NekoGui_ConfigItem {
    void JsonStore::_add(configItem *item) { _map[item->name] = std::shared_ptr<configItem>(item); }
}
static bool fastStartupWatchdog = false;
void setTimeout(const std::function<void()> &callback, QObject *object, int timeout) {
    if (fastStartupWatchdog && timeout == 60000) timeout = 1000;
    QTimer::singleShot(timeout, object, callback);
}
static int failures = 0, coreReady = 0, coreFailed = 0, coreCrashed = 0;
static int readyProfile = -1;
static void check(const char *name, bool ok) {
    failures += !ok;
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name); std::fflush(stdout);
}
static void spin(int milliseconds) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
}
static void startupRecoveryTests() {
#ifdef Q_OS_WIN
    fastStartupWatchdog = true;
    NekoGui::dataStore->prepare_exit = false;
    {
        QTemporaryDir dir;
        check("a private fixture directory is available", dir.isValid());
        const auto failedBefore = coreFailed, readyBefore = coreReady;
        NekoGui::dataStore->core_running = false;
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath(), {"--fixture-recover", dir.filePath("attempt")});
        core.start_profile_when_core_is_up = 75;
        core.Start(); spin(1800);
        check("one startup timeout retries the core and releases the original queued profile exactly once",
              core.generation() == 2 && core.state() == QProcess::Running && NekoGui::dataStore->core_running &&
              coreFailed == failedBefore && coreReady == readyBefore + 1 && readyProfile == 75 && core.start_profile_when_core_is_up == -1);
        NekoGui::dataStore->prepare_exit = true;
        core.kill(); core.waitForFinished(500);
        NekoGui::dataStore->prepare_exit = false;
    }
    {
        const auto failedBefore = coreFailed;
        NekoGui::dataStore->core_running = false;
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath(), {"--fixture-silent"});
        core.start_profile_when_core_is_up = 76;
        core.Start(); spin(3200);
        check("a second startup timeout reports one final failure and stops after two processes",
              core.generation() == 2 && core.state() == QProcess::NotRunning &&
              coreFailed == failedBefore + 1 && core.start_profile_when_core_is_up == -1);
    }
    {
        const auto failedBefore = coreFailed;
        NekoGui::dataStore->core_running = false;
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath(), {"--fixture-silent"});
        core.start_profile_when_core_is_up = 79;
        core.Start(); spin(1600);
        core.Restart();
        const auto replacement = core.generation();
        spin(700); // After generation 2's watchdog, before generation 3's deadline.
        check("an old recovery watchdog cannot stop a newer unready core",
              replacement == 3 && core.generation() == replacement && core.state() == QProcess::Running &&
              !NekoGui::dataStore->core_running && coreFailed == failedBefore);
        NekoGui::dataStore->prepare_exit = true;
        core.kill(); core.waitForFinished(500);
        NekoGui::dataStore->prepare_exit = false;
    }
    {
        const auto readyBefore = coreReady;
        NekoGui::dataStore->core_running = false;
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath(), {"--fixture-partial"});
        core.start_profile_when_core_is_up = 80;
        // Keep real child output buffered until waitForFinished drains retirement.
        core.blockSignals(true);
        core.Start(); spin(200);
        core.blockSignals(false);
        bool drained = false;
        QObject::connect(&core, &QProcess::stateChanged, &core, [&](QProcess::ProcessState state) {
            if (state == QProcess::NotRunning && !drained) {
                drained = true;
                QMetaObject::invokeMethod(&core, "readyReadStandardOutput", Qt::DirectConnection);
            }
        });
        core.arguments = {"--fixture-silent"};
        core.Restart(); spin(100);
        check("retired process output cannot consume the replacement's queued connection",
              coreReady == readyBefore && !NekoGui::dataStore->core_running && core.start_profile_when_core_is_up == 80);
        NekoGui::dataStore->prepare_exit = true;
        core.kill(); core.waitForFinished(500);
        NekoGui::dataStore->prepare_exit = false;
    }
    {
        const auto readyBefore = coreReady;
        NekoGui::dataStore->core_running = false;
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath(), {"--fixture-silent"});
        core.start_profile_when_core_is_up = 77;
        core.Start();
        core.start_profile_when_core_is_up = -1; // The user's disconnect path cancels this queue.
        spin(1400);
        check("cancelled startup cannot retry or revive its queued profile",
              core.generation() == 1 && coreReady == readyBefore && core.start_profile_when_core_is_up == -1);
    }
    {
        const auto failedBefore = coreFailed, readyBefore = coreReady;
        NekoGui::dataStore->core_running = false;
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath(), {"--fixture-silent"});
        core.start_profile_when_core_is_up = 78;
        core.Start();
        NekoGui::dataStore->prepare_exit = true;
        spin(1400);
        check("application exit suppresses watchdog recovery and failure notifications",
              core.generation() == 1 && coreReady == readyBefore && coreFailed == failedBefore);
        core.kill(); core.waitForFinished(500);
        NekoGui::dataStore->prepare_exit = false;
    }
    fastStartupWatchdog = false;
#endif
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--fixture-recover")) {
        const auto marker = app.arguments().value(app.arguments().indexOf("--fixture-recover") + 1);
        if (QFile::exists(marker)) {
            QTimer::singleShot(40, &app, [] { std::fputs("grpc server listening\n", stdout); std::fflush(stdout); });
        } else {
            QFile firstAttempt(marker);
            if (!firstAttempt.open(QIODevice::WriteOnly)) return 2;
            firstAttempt.write("first");
        }
        QTimer::singleShot(75000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (app.arguments().contains("--fixture-partial")) {
        std::fputs("grpc server lis", stdout); std::fflush(stdout);
        QTimer::singleShot(40, &app, [] { std::fputs("tening\n", stdout); std::fflush(stdout); });
        QTimer::singleShot(5000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (app.arguments().contains("--fixture-crash")) {
        std::fputs("grpc server listening\n", stdout); std::fflush(stdout);
        QTimer::singleShot(40, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (app.arguments().contains("--fixture-silent")) {
        QTimer::singleShot(75000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (app.arguments().contains("--fixture-delayed")) {
        QTimer::singleShot(16000, &app, [] {
            std::fputs("grpc server listening\n", stdout); std::fflush(stdout);
        });
        QTimer::singleShot(75000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    MW_show_log = [](const QString &) {};
    MW_dialog_message = [](const QString &, const QString &event) {
        coreReady += event.startsWith("CoreStarted,");
        if (event.startsWith("CoreStarted,")) readyProfile = event.section(',', 1).toInt();
        coreFailed += event == "CoreStartFailed";
        coreCrashed += event == "CoreCrashed";
    };
    NekoGui::dataStore->core_token = "fixture";
    if (app.arguments().contains("--startup-recovery-tests")) {
        startupRecoveryTests();
        return failures ? 1 : 0;
    }
    {
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath() + ".missing", {});
        core.start_profile_when_core_is_up = 71;
        core.Start(); spin(100);
        check("a missing core reports failure without stranding a queued profile", coreFailed == 1 && core.start_profile_when_core_is_up == -1);
        core.program = QCoreApplication::applicationFilePath();
        core.arguments = {"--fixture-partial"};
        core.start_profile_when_core_is_up = 72;
        core.Restart(); spin(150);
        check("a failed core can be retried after its executable is repaired", core.state() == QProcess::Running && NekoGui::dataStore->core_running);
        check("fragmented readiness output releases the queued profile once", coreReady == 1 && core.start_profile_when_core_is_up == -1);
        NekoGui::dataStore->prepare_exit = true;
        core.kill(); core.waitForFinished(500);
        NekoGui::dataStore->prepare_exit = false;
    }
    {
        const auto failedBefore = coreFailed;
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath(), {"--fixture-silent"});
        core.Start(); spin(62000);
        check("a silent core without a queued connection still fails after one finite readiness deadline", coreFailed == failedBefore + 1 && core.state() == QProcess::NotRunning && core.start_profile_when_core_is_up == -1);
        check("a startup timeout does not enter an endless restart loop", core.generation() == 1);
    }
    {
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath(), {"--fixture-crash"});
        core.Start(); spin(120);
        check("a synthetic core crash is observed", coreCrashed >= 1 && core.state() == QProcess::NotRunning);
        const auto oldGeneration = core.generation();
        core.arguments = {"--fixture-partial"};
        core.Restart(); spin(150);
        const auto replacement = core.generation();
        check("a replacement core has a new process generation", replacement > oldGeneration);
        spin(1000);
        check("a queued restart from the dead core cannot replace the live core", core.generation() == replacement && core.state() == QProcess::Running);
        NekoGui::dataStore->prepare_exit = true;
        core.kill(); core.waitForFinished(500);
    }
    {
        const auto failedBefore = coreFailed, readyBefore = coreReady;
        NekoGui::dataStore->prepare_exit = false;
        NekoGui_sys::CoreProcess core(QCoreApplication::applicationFilePath(), {"--fixture-delayed"});
        core.start_profile_when_core_is_up = 74;
        NekoGui::dataStore->core_running = false;
        core.Start(); spin(17000);
        check("slow startup completes after 15 seconds without killing the core or losing its pending profile",
              core.state() == QProcess::Running && NekoGui::dataStore->core_running &&
              coreFailed == failedBefore && coreReady == readyBefore + 1 && core.start_profile_when_core_is_up == -1);
        NekoGui::dataStore->prepare_exit = true;
        core.kill(); core.waitForFinished(500);
        NekoGui::dataStore->prepare_exit = false;
    }
    startupRecoveryTests();
    return failures ? 1 : 0;
}
