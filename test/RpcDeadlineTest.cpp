#include "main/NekoGui.hpp"
#include "rpc/gRPC.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace NekoGui {
    DataStore::DataStore() : JsonStore() {}
    ExtraCore::ExtraCore() : JsonStore() {}
    InboundAuthorization::InboundAuthorization() : JsonStore() {}
    DataStore *dataStore = new DataStore;
}
namespace NekoGui_ConfigItem {
    void JsonStore::_add(configItem *item) { _map[item->name] = std::shared_ptr<configItem>(item); }
}
void runOnUiThread(const std::function<void()> &callback, QObject *parent) {
    QMetaObject::invokeMethod(parent ? parent : qApp, callback, Qt::QueuedConnection);
}

// A minimal h2c gRPC peer on loopback. A hung request deliberately leaves its
// stream open, exactly as a dying/replaced core can. It never opens a TUN.
class CorePeer : public QTcpServer {
public:
    bool respond = false;
    bool disconnectRequest = false;
    int requests = 0;
    int accepted = 0;
    static QByteArray frame(int type, int flags, int stream, const QByteArray &body) {
        QByteArray out(9, '\0');
        out[0] = char(body.size() >> 16); out[1] = char(body.size() >> 8); out[2] = char(body.size());
        out[3] = char(type); out[4] = char(flags);
        out[5] = char(stream >> 24); out[6] = char(stream >> 16); out[7] = char(stream >> 8); out[8] = char(stream);
        return out + body;
    }
    CorePeer() {
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection(); ++accepted;
            auto bytes = std::make_shared<QByteArray>();
            auto preface = std::make_shared<bool>(false);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes, preface] {
                *bytes += socket->readAll();
                if (!*preface) {
                    if (bytes->size() < 24) return;
                    bytes->remove(0, 24); *preface = true;
                    socket->write(frame(4, 0, 0, {})); // server SETTINGS
                }
                while (bytes->size() >= 9) {
                    const auto byte = [&](int n) { return static_cast<unsigned char>(bytes->at(n)); };
                    const int len = (byte(0) << 16) | (byte(1) << 8) | byte(2);
                    if (bytes->size() < 9 + len) return;
                    const int type = byte(3), flags = byte(4);
                    const int stream = ((byte(5) & 127) << 24) | (byte(6) << 16) | (byte(7) << 8) | byte(8);
                    bytes->remove(0, 9 + len);
                    if (type == 4 && !(flags & 1)) socket->write(frame(4, 1, 0, {}));
                    if (type != 1) continue;
                    ++requests;
                    if (disconnectRequest) { socket->abort(); return; }
                    if (!respond) continue;
                    QByteArray headers("\x88", 1); // HPACK :status 200
                    headers += QByteArray("\0\x0c", 2) + "content-type" + QByteArray("\x10", 1) + "application/grpc";
                    socket->write(frame(1, 4, stream, headers));
                    socket->write(frame(0, 1, stream, QByteArray(5, '\0'))); // empty protobuf ErrorResp
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        if (!listen(QHostAddress::LocalHost, 0)) std::abort();
    }
};

static int failures = 0;
static void check(const char *name, bool ok) {
    failures += !ok; std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name); std::fflush(stdout);
}
static qint64 call(const std::function<void()> &fn) {
    QElapsedTimer timer; timer.start();
    std::atomic_bool done{false};
    auto *worker = QThread::create([&] { fn(); done = true; });
    worker->start();
    while (!done && timer.elapsed() < 2500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    if (!done) { std::fputs("FAIL: lifecycle RPC never completed its deadline\n", stderr); std::_Exit(1); }
    worker->wait(); delete worker;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return timer.elapsed();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    MW_show_log = [](const QString &) {};
    CorePeer peer;
    NekoGui::dataStore->core_running = true;
    auto *client = new NekoGui_rpc::Client([](const QString &) {}, QString("127.0.0.1:%1").arg(peer.serverPort()), "fixture");
    bool ok = true;
    auto elapsed = call([&] { client->Stop(&ok, 150); });
    check("Stop reached the loopback HTTP2 core", peer.requests == 1);
    check("a core that never replies cannot strand Stop", !ok && elapsed >= 100 && elapsed < 1500);
    peer.respond = true;
    libcore::LoadConfigReq request;
    elapsed = call([&] { client->Start(&ok, request, 500); });
    check("the same client reconnects after a hung Stop", ok && elapsed < 1500 && peer.requests == 2 && peer.accepted == 2);
    peer.respond = false;
    elapsed = call([&] { client->Start(&ok, request, 150); });
    check("a core that never replies cannot strand Start", !ok && elapsed >= 100 && elapsed < 1500);
    peer.respond = true;
    call([&] { client->Stop(&ok, 500); });
    check("a cancelled Start cannot poison the next Stop", ok);
    peer.disconnectRequest = true;
    call([&] { client->Stop(&ok, 500); });
    check("core death during Stop completes as a failure", !ok);
    peer.disconnectRequest = false;
    call([&] { client->Start(&ok, request, 500); });
    check("replacement core accepts a new Start on the same port", ok);
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 1));
    call([&] { client->Start(&ok, request, 500); });
    check("control RPC bypasses an unrelated application proxy", ok);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    const int before = peer.requests;
    NekoGui::dataStore->core_running = false;
    elapsed = call([&] { client->Stop(&ok, 150); });
    check("an absent core fails promptly without another socket", !ok && elapsed < 100 && peer.requests == before);
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
