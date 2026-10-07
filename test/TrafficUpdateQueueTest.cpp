#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

#include "db/traffic/TrafficLooper.hpp"
#include "rpc/gRPC.h"
#include "ui/mainwindow_interface.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

// Настоящий опрос TrafficLooper; вместо RPC и отрисовки — счётчики.
// Профили, установленный клиент и сеть тесту не нужны.
static int fetched = 0, rendered = 0, statusUpdates = 0, failures = 0;
static bool pollDuringRender = false;
static bool blockedDuringRender = false;
static std::string payload;
namespace NekoGui {
    DataStore::DataStore() : JsonStore() {}
    ExtraCore::ExtraCore() : JsonStore() {}
    InboundAuthorization::InboundAuthorization() : JsonStore() {}
    DataStore *dataStore = new DataStore;
}
namespace NekoGui_ConfigItem {
    void JsonStore::_add(configItem *item) { _map[item->name] = std::shared_ptr<configItem>(item); }
}
namespace QtGrpc { class Http2GrpcChannelPrivate {}; }
namespace NekoGui_rpc {
    Client::Client(std::function<void(const QString &)>, const QString &, const QString &) {}
    long long Client::QueryStats(const std::string &, const std::string &) { return 1; }
    std::string Client::ListConnections() { ++fetched; return payload; }
}
namespace NekoGui_traffic { extern QElapsedTimer elapsedTimer; }
QString ReadableSize(const long long &number) { return QString::number(number); }
void runOnUiThread(const std::function<void()> &callback, QObject *parent) {
    QMetaObject::invokeMethod(parent ? parent : qApp, callback, Qt::QueuedConnection);
}
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) { mainwindow = this; }
MainWindow::~MainWindow() = default;
void MainWindow::refresh_status(const QString &) { ++statusUpdates; }
void MainWindow::refresh_proxy_list(const int &) {}
void MainWindow::refresh_connection_list(const QJsonArray &) {
    ++rendered;
    if (pollDuringRender) {
        pollDuringRender = false;
        const int before = fetched;
        NekoGui_traffic::trafficLooper->Poll();
        blockedDuringRender = fetched == before;
    }
}
void MainWindow::on_commitDataRequest() { std::abort(); }
void MainWindow::on_menu_exit_triggered() { std::abort(); }
void MainWindow::on_menu_interference_triggered() { std::abort(); }

static void check(const char *name, bool ok) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}
static void drain() {
    QEventLoop loop;
    QTimer::singleShot(0, &loop, &QEventLoop::quit);
    loop.exec();
}
static void memory(const char *phase) {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters {};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters));
    std::printf("memory,%s,%llu\n", phase, static_cast<unsigned long long>(counters.PrivateUsage));
    std::fflush(stdout);
#endif
}
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    MainWindow window;
    NekoGui_rpc::defaultClient = new NekoGui_rpc::Client({}, "", "");
    NekoGui::dataStore->connection_statistics = true;
    QJsonArray connections;
    for (int i = 0; i < 512; ++i) connections += QJsonObject {
        {"ID", i}, {"Process", "fixture.exe"}, {"Tag", "proxy"},
        {"Dest", QString("fixture-%1.example:443").arg(i)}, {"Rule", QString(1024, 'x')}
    };
    payload = QJsonDocument(connections).toJson(QJsonDocument::Compact).toStdString();
    auto *looper = NekoGui_traffic::trafficLooper;
    looper->loop_enabled = true;
    NekoGui_traffic::elapsedTimer.start();
    memory("before");
    // GUI занят: ни один из 64 опросов не может пока вызвать отрисовку.
    for (int i = 0; i < 64; ++i) { QThread::msleep(1); looper->Poll(); }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--stall-stress") {
        const int seconds = QString::fromLocal8Bit(argv[2]).toInt();
        if (seconds < 1 || seconds > 7200) return 2;
        QElapsedTimer stress;
        stress.start();
        int nextSample = 60;
        while (stress.elapsed() < seconds * 1000ll) {
            looper->Poll();
            if (stress.elapsed() >= nextSample * 1000ll) {
                const auto label = QString("stall_%1_seconds").arg(nextSample).toLatin1();
                memory(label.constData());
                nextSample += 60;
            }
            QThread::msleep(1000);
        }
        memory("stall_finished");
    }
    memory("gui_blocked");
    std::printf("fetched_while_blocked=%d\n", fetched);
    check("one connection response while GUI is busy", fetched == 1);
    check("no rendering before event loop resumes", rendered == 0);
    drain();
    check("one delivery when GUI resumes", rendered == 1);
    looper->Poll();
    pollDuringRender = true;
    drain();
    check("polling resumes after delivery", fetched == 2 && rendered == 2);
    check("no next snapshot during rendering", blockedDuringRender);
    looper->Poll();
    const int beforeStop = rendered;
    looper->loop_enabled = false;
    looper->Poll();
    drain();
    check("stop state reaches GUI", statusUpdates > 0 && !looper->looping);
    check("stop discards a previously queued connection response", rendered == beforeStop);
    looper->loop_enabled = true;
    looper->Poll();
    drain();
    check("polling resumes after a stopped delivery", rendered == beforeStop + 1);
    mainwindow = nullptr;
    looper->Poll();
    drain();
    mainwindow = &window;
    looper->Poll();
    drain();
    check("an absent window also releases the pending update", rendered == beforeStop + 2);
    memory("after_drain");
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
