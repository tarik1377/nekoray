#include "main/NekoGui.hpp"
#include "main/HTTPRequestHelper.hpp"
#include <QApplication>
#include <QBuffer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>

// Actual HTTP helper; only the settings bindings are isolated. All endpoints
// are owned loopback sockets, all output goes to in-memory buffers.
namespace NekoGui {
DataStore::DataStore() : JsonStore() {}
ExtraCore::ExtraCore() : JsonStore() {}
InboundAuthorization::InboundAuthorization() : JsonStore() {}
bool InboundAuthorization::NeedAuth() const { return !username.isEmpty() && !password.isEmpty(); }
QString DataStore::GetUserAgent(bool) const { return "offline-fixture"; }
DataStore *dataStore = new DataStore;
}
static int checks = 0, failures = 0;
static void check(const char *name, bool ok) { ++checks; failures += !ok; std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name); std::fflush(stdout); }
struct Endpoint {
    enum Mode { Unavailable, AbruptClose, Truncated, Good, Silent, TlsClose } mode;
    QTcpServer server;
    explicit Endpoint(Mode mode) : mode(mode) {
        if (!server.listen(QHostAddress::LocalHost, 0)) std::abort();
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                socket->setParent(&server);
                if (this->mode == TlsClose) { socket->abort(); socket->deleteLater(); continue; }
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    socket->readAll();
                    if (this->mode == Silent || socket->property("sent").toBool()) return;
                    socket->setProperty("sent", true);
                    if (this->mode == AbruptClose) { socket->abort(); return; }
                    if (this->mode == Unavailable) socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 12\r\nConnection: close\r\n\r\nfixture down");
                    if (this->mode == Truncated) socket->write("HTTP/1.1 200 OK\r\nContent-Length: 40\r\nConnection: close\r\n\r\npartial");
                    if (this->mode == Good) socket->write("HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\nabcdefgh");
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url(bool tls = false) const { return QUrl(QString(tls ? "https" : "http") + "://127.0.0.1:" + QString::number(server.serverPort()) + "/fixture"); }
};
static NekoGui_network::NekoHTTPResponse request(int method, const QUrl &url, QByteArray &body) {
    if (method == 0) return NetworkRequestHelper::HttpGet(url);
    if (method == 1) return NetworkRequestHelper::HttpPost(url, "{}", {}, true);
    QBuffer sink(&body); sink.open(QIODevice::WriteOnly);
    return NetworkRequestHelper::HttpDownload(url, &sink, 4096);
}
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    NekoGui::dataStore->sub_use_proxy = false;
    NekoGui::dataStore->sub_insecure = false;
    MW_show_log = [](const QString &) {};
    for (int method = 0; method < 3; ++method) {
        QByteArray body;
        QTcpServer reserved; reserved.listen(QHostAddress::LocalHost, 0);
        const auto url = QUrl("http://127.0.0.1:" + QString::number(reserved.serverPort()) + "/fixture"); reserved.close();
        auto result = request(method, url, body);
        check("connection refusal returns an error without an application failure", !result.error.isEmpty());
        Endpoint unavailable(Endpoint::Unavailable);
        result = request(method, unavailable.url(), body);
        check("HTTP 503 is propagated as a failure", !result.error.isEmpty() && (method == 0 || result.status == 503));
        Endpoint closed(Endpoint::AbruptClose);
        result = request(method, closed.url(), body);
        check("a server closing before its response fails safely", !result.error.isEmpty());
    }
    {
        Endpoint tls(Endpoint::TlsClose); QByteArray body;
        const auto result = request(0, tls.url(true), body);
        check("a failed TLS handshake is returned as an error", !result.error.isEmpty());
    }
    {
        Endpoint partial(Endpoint::Truncated); QByteArray body;
        const auto result = request(2, partial.url(), body);
        check("a truncated component body is never accepted as a completed download", !result.error.isEmpty());
    }
    {
        Endpoint good(Endpoint::Good); QByteArray body;
        const auto result = request(2, good.url(), body);
        check("a healthy download still produces its complete body", result.error.isEmpty() && result.status == 200 && body == "abcdefgh");
        QBuffer sink; sink.open(QIODevice::WriteOnly);
        const auto oversized = NetworkRequestHelper::HttpDownload(good.url(), &sink, 4);
        check("an oversized dependency cannot overflow its destination", !oversized.error.isEmpty() && sink.data().isEmpty());
    }
    {
        NekoGui::dataStore->sub_use_proxy = true;
        NekoGui::dataStore->started_id = -1;
        const auto get = NetworkRequestHelper::HttpGet(QUrl("http://fixture.invalid/"));
        check("a missing required local proxy fails before requesting a subscription", !get.error.isEmpty());
        NekoGui::dataStore->sub_use_proxy = false;
    }
    {
        Endpoint silent(Endpoint::Silent); QByteArray body;
        QElapsedTimer elapsed; elapsed.start();
        auto result = request(0, silent.url(), body);
        check("a silent metadata endpoint reaches the real ten-second timeout", !result.error.isEmpty() && elapsed.elapsed() >= 8000 && elapsed.elapsed() < 14000);
    }
    {
        Endpoint silent(Endpoint::Silent); QByteArray body;
        QElapsedTimer elapsed; elapsed.start();
        int ticks = 0; QTimer heartbeat; heartbeat.setInterval(100);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; }); heartbeat.start();
        auto result = request(2, silent.url(), body);
        check("a silent dependency endpoint reaches the real twenty-second idle timeout", !result.error.isEmpty() && elapsed.elapsed() >= 17000 && elapsed.elapsed() < 26000);
        check("Qt continues handling events while the request waits", ticks > 100 && body.isEmpty());
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
