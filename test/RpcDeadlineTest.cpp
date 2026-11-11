#include "main/NekoGui.hpp"
#include "main/StartupPolicy.hpp"
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
    int responseDelayMs = 0;
    QStringList responseErrors;
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
                    const auto error = responseErrors.isEmpty() ? QString() : responseErrors.takeFirst();
                    const auto reply = [socket, stream, error] {
                        QByteArray headers("\x88", 1); // HPACK :status 200
                        headers += QByteArray("\0\x0c", 2) + "content-type" + QByteArray("\x10", 1) + "application/grpc";
                        socket->write(frame(1, 4, stream, headers));
                        libcore::ErrorResp response;
                        response.set_error(error.toStdString());
                        const auto payload = response.SerializeAsString();
                        QByteArray body(5, '\0');
                        body[1] = char(payload.size() >> 24); body[2] = char(payload.size() >> 16);
                        body[3] = char(payload.size() >> 8); body[4] = char(payload.size());
                        body += QByteArray(payload.data(), int(payload.size()));
                        socket->write(frame(0, 1, stream, body));
                    };
                    if (responseDelayMs > 0) QTimer::singleShot(responseDelayMs, socket, reply);
                    else reply();
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
static qint64 call(const std::function<void()> &fn, int deadline = 2500) {
    QElapsedTimer timer; timer.start();
    std::atomic_bool done{false};
    auto *worker = QThread::create([&] { fn(); done = true; });
    worker->start();
    while (!done && timer.elapsed() < deadline) {
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
    const QString staleAdapter = QStringLiteral("start inbound/tun[diag]: configure tun interface: "
        "(create adapter: Cannot create a file when that file already exists. | open existing adapter: Element not found.)");
    const auto tryStart = [&] {
        QString error;
        bool adapterRecoveryUsed = false;
        for (int attempt = 0; attempt < GreenRhythm::StartupPolicy::maxStartAttempts; ++attempt) {
            call([&] { error = client->Start(&ok, request, 500); });
            if (!ok || error.isEmpty()) break;
            const auto retry = GreenRhythm::StartupPolicy::retryAfterError(error, adapterRecoveryUsed, true);
            if (retry == GreenRhythm::StartupPolicy::Retry::None) break;
            if (retry == GreenRhythm::StartupPolicy::Retry::AdapterReopen) adapterRecoveryUsed = true;
        }
        return error;
    };
    int requestsBefore = peer.requests;
    peer.responseErrors = QStringList{staleAdapter};
    const auto recoveredError = tryStart();
    check("Windows stale-adapter cleanup is retried on the same live core before success is published",
          ok && recoveredError.isEmpty() && peer.requests == requestsBefore + 2);
    requestsBefore = peer.requests;
    peer.responseErrors = QStringList{staleAdapter, staleAdapter};
    const auto persistentError = tryStart();
    check("persistent stale-adapter errors are returned after one recovery attempt",
          ok && persistentError == staleAdapter && peer.requests == requestsBefore + 2);
    requestsBefore = peer.requests;
    peer.responseErrors = QStringList{QStringLiteral("listen tcp: bind: address already in use"), staleAdapter};
    const auto mixedError = tryStart();
    check("a port collision does not consume the one adapter-recovery attempt",
          ok && mixedError.isEmpty() && peer.requests == requestsBefore + 3);
    QString pairedPermission = staleAdapter;
    pairedPermission.replace(QStringLiteral("Element not found."), QStringLiteral("Access is denied."));
    requestsBefore = peer.requests;
    peer.responseErrors = QStringList{pairedPermission, pairedPermission};
    const auto deniedError = tryStart();
    check("paired create/open failures preserve a persistent permission error after bounded recovery",
          ok && deniedError == pairedPermission && peer.requests == requestsBefore + 2);
    peer.responseErrors.clear();
    check("non-Windows starts do not get Windows adapter recovery",
          GreenRhythm::StartupPolicy::retryAfterError(staleAdapter, false, false) == GreenRhythm::StartupPolicy::Retry::None);
    check("unpaired permission failures are not retried as adapter-reopen failures",
          GreenRhythm::StartupPolicy::retryAfterError("start inbound/tun[diag]: configure tun interface: Access is denied.", false, true)
              == GreenRhythm::StartupPolicy::Retry::None);
    check("existing port recovery remains available",
          GreenRhythm::StartupPolicy::retryAfterError("listen tcp: bind: address already in use", false, false)
              == GreenRhythm::StartupPolicy::Retry::Port);
    peer.responseDelayMs = 16000;
    elapsed = call([&] { client->Start(&ok, request); }, 19000);
    check("a delayed native TUN setup can reply after 15 seconds without Start being cancelled",
          ok && elapsed >= 15000 && elapsed < 19000);
    const int before = peer.requests;
    NekoGui::dataStore->core_running = false;
    elapsed = call([&] { client->Stop(&ok, 150); });
    check("an absent core fails promptly without another socket", !ok && elapsed < 100 && peer.requests == before);
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
