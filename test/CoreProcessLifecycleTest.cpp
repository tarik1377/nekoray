#include "main/NekoGui.hpp"
#include "sys/ExternalProcess.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
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
void setTimeout(const std::function<void()> &callback, QObject *object, int timeout) {
    QTimer::singleShot(timeout, object, callback);
}
static int failures = 0, coreReady = 0, coreFailed = 0, coreCrashed = 0;
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
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
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
        QTimer::singleShot(20000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    MW_show_log = [](const QString &) {};
    MW_dialog_message = [](const QString &, const QString &event) {
        coreReady += event.startsWith("CoreStarted,");
        coreFailed += event == "CoreStartFailed";
        coreCrashed += event == "CoreCrashed";
    };
    NekoGui::dataStore->core_token = "fixture";
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
        core.start_profile_when_core_is_up = 73;
        core.Start(); spin(16000);
        check("a core that never opens RPC times out and cancels its pending profile", coreFailed == failedBefore + 1 && core.state() == QProcess::NotRunning && core.start_profile_when_core_is_up == -1);
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
    return failures ? 1 : 0;
}
