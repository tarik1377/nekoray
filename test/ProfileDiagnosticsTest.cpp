#include "main/ProfileProbe.hpp"
#include "main/ServiceHealth.hpp"
#include "main/StunProbe.hpp"
#include "ServiceHealthFixture.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QPointer>
#include <QSslConfiguration>
#include <QSslKey>
#include <QTcpServer>
#include <cstdio>
using namespace GreenRhythm;
namespace {
int failures = 0, checks = 0;
void check(bool value, const char *message) {
    ++checks; if (!value) ++failures;
    std::fprintf(stderr, "%s %d %s\n", value ? "PASS" : "FAIL", checks, message); std::fflush(stderr);
}
void wait(QObject *object, const char *signal, int deadline = 4000) {
    QEventLoop loop; QObject::connect(object, signal, &loop, SLOT(quit()));
    QTimer::singleShot(deadline, &loop, &QEventLoop::quit); loop.exec();
}
struct Gateway : QTcpServer {
    QString mode = "hello"; int requests = 0;
    QByteArray body = "fixture-fresh-private";
    Gateway() { listen(QHostAddress::LocalHost, 0); }
    void incomingConnection(qintptr descriptor) override {
        auto *socket = new QSslSocket(this); socket->setSocketDescriptor(descriptor);
        socket->setLocalCertificate(QSslCertificate(healthFixtureCertificate)); socket->setPrivateKey(QSslKey(healthFixtureKey, QSsl::Rsa));
        auto data = std::make_shared<QByteArray>();
        QObject::connect(socket, &QSslSocket::readyRead, socket, [this, socket, data] {
            *data += socket->readAll(); if (!data->contains("\r\n\r\n") || socket->property("answered").toBool()) return;
            socket->setProperty("answered", true); ++requests;
            QByteArray key;
            for (const auto &line : data->split('\n')) if (line.toLower().startsWith("sec-websocket-key:")) key = line.mid(line.indexOf(':') + 1).trimmed();
            if (key.isEmpty()) {
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost(); return;
            }
            auto accept = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
            if (mode == "bad_accept") accept = "wrong";
            const QByteArray hello = mode == "wrong_opcode" ? "{\"op\":0,\"d\":{}}" : "{\"op\":10,\"d\":{\"heartbeat_interval\":41250}}";
            QByteArray frame;
            if (mode == "oversize") frame = QByteArray::fromHex("817e7fff");
            else if (mode == "fragmented") {
                frame.append(char(1)); frame.append(char(10)); frame += hello.left(10);
                frame += QByteArray::fromHex("890170"); // ping between fragments
                frame.append(char(128)); frame.append(char(hello.size() - 10)); frame += hello.mid(10);
            } else { frame.append(char(129)); frame.append(char(hello.size())); frame += hello; }
            const auto response = "HTTP/1.1 101 Switching Protocols\r\nConnection: keep-alive, Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n" + frame;
            // Split the upgrade header and frame across reads.
            socket->write(response.left(23));
            QTimer::singleShot(8, socket, [socket, response] { socket->write(response.mid(23)); });
        });
        QObject::connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater); socket->startServerEncryption();
    }
};
struct Proxy : QTcpServer {
    QUdpSocket relay; quint16 gatewayPort = 0; QString udpMode = "replies"; bool reject = false;
    int packets = 0, associations = 0;
    Proxy() {
        relay.bind(QHostAddress::LocalHost, 0); listen(QHostAddress::LocalHost, 0);
        QObject::connect(&relay, &QUdpSocket::readyRead, this, [this] {
            int budget = 64;
            while (relay.hasPendingDatagrams() && budget-- > 0) {
                QByteArray data(4096, Qt::Uninitialized); QHostAddress sender; quint16 port;
                const auto count = relay.readDatagram(data.data(), data.size(), &sender, &port);
                if (count < 0) { relay.close(); break; }
                data.resize(int(count)); ++packets;
                if (udpMode == "drop" || data.size() < 30) continue;
                if (udpMode == "partial" && quint8(data[4]) == 8) continue;
                const int headerSize = quint8(data[3]) == 3 ? 7 + quint8(data[4]) : quint8(data[3]) == 4 ? 22 : 10;
                if (data.size() < headerSize + 20) continue;
                if (udpMode == "dns_only") {
                    // The former DNS-based probe would succeed here even when
                    // every non-DNS datagram is dropped or hijacked.
                    if (data.mid(headerSize + 4, 4) == QByteArray::fromHex("2112a442")) continue;
                    data[headerSize + 2] = char(quint8(data[headerSize + 2]) | 128);
                    relay.writeDatagram(data, sender, port); continue;
                }
                data = data.left(headerSize) + QByteArray::fromHex("0101000c2112a442")
                    + data.mid(headerSize + 8, 12) + QByteArray::fromHex("0020000800011234e112a443");
                if (udpMode == "bad_length") data[headerSize + 3] = 8;
                if (udpMode == "wrong_source") data[headerSize - 1] ^= 1;
                if (udpMode == "fragment") data[2] = 1;
                auto invalid = data; invalid[headerSize + 8] ^= 1;
                relay.writeDatagram(invalid, sender, port);
                if (udpMode == "wrong_nonce") continue;
                relay.writeDatagram(data, sender, port); relay.writeDatagram(data, sender, port); // duplicate is ignored
            }
        });
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = nextPendingConnection()) {
                auto data = std::make_shared<QByteArray>(); auto stage = std::make_shared<int>(0);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, data, stage] {
                    if (*stage == 10) return;
                    *data += socket->readAll();
                    if (*stage == 0 && !data->isEmpty() && quint8((*data)[0]) != 5) {
                        if (!data->contains("\r\n\r\n")) return;
                        if (!data->startsWith("CONNECT ")) {
                            socket->write("HTTP/1.1 503 Fixture\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                            socket->disconnectFromHost(); *stage = 99; return;
                        }
                        auto *remote = new QTcpSocket(socket); remote->setProxy(QNetworkProxy::NoProxy);
                        QObject::connect(remote, &QTcpSocket::connected, socket, [socket, remote] {
                            socket->write("HTTP/1.1 200 Connection established\r\n\r\n");
                            QObject::connect(socket, &QTcpSocket::readyRead, remote, [socket, remote] { remote->write(socket->readAll()); });
                        });
                        QObject::connect(remote, &QTcpSocket::readyRead, socket, [socket, remote] { socket->write(remote->readAll()); });
                        QObject::connect(remote, &QTcpSocket::disconnected, socket, &QTcpSocket::disconnectFromHost);
                        *stage = 10; data->clear(); remote->connectToHost(QHostAddress::LocalHost, gatewayPort); return;
                    }
                    if (*stage == 10) return; // forwarding connection owns subsequent reads
                    if (*stage == 0 && data->size() >= 3) {
                        data->clear(); *stage = reject ? 99 : 1;
                        socket->write(reject ? QByteArray::fromHex("05ff") : QByteArray::fromHex("0500")); return;
                    }
                    if (*stage == 1 && data->size() >= 10) {
                        ++associations; data->clear(); *stage = 2;
                        QByteArray response = QByteArray::fromHex("0500000100000000");
                        response.append(char(relay.localPort() >> 8)); response.append(char(relay.localPort()));
                        socket->write(response.left(3)); QTimer::singleShot(3, socket, [socket, response] { socket->write(response.mid(3)); });
                    }
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};
int fakeCore(const QStringList &arguments, QCoreApplication &app) {
    for (const auto &name : {"HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY"}) if (qEnvironmentVariableIsSet(name)) return 6;
    QFile file(arguments.value(arguments.indexOf("-c") + 1)); if (!file.open(QIODevice::ReadOnly)) return 4;
    const auto config = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    const auto outbound = config.value("outbounds").toArray().first().toObject();
    const auto mode = outbound.value("server").toString();
    if (mode == "fail") return 7;
    if (mode == "hang") return app.exec();
    const auto inbound = config.value("inbounds").toArray().first().toObject();
    if (inbound.value("type") != "mixed" || inbound.value("listen") != "127.0.0.1"
        || config.contains("experimental") || config.value("route").toObject().contains("rules")) return 8;
    const auto auth = inbound.value("users").toArray().first().toObject();
    const auto expected = "Basic " + (auth.value("username").toString() + ':' + auth.value("password").toString()).toUtf8().toBase64();
    QTcpServer server; if (!server.listen(QHostAddress::LocalHost, quint16(inbound.value("listen_port").toInt()))) return 9;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, expected] {
                const auto data = socket->readAll();
                QByteArray authorization;
                for (const auto &line : data.split('\n')) if (line.toLower().startsWith("proxy-authorization:")) authorization = line.mid(line.indexOf(':') + 1).trimmed();
                socket->write(authorization == expected ? "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                    : !authorization.isEmpty() ? "HTTP/1.1 403 Wrong credentials\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                    : "HTTP/1.1 407 Authentication Required\r\nProxy-Authenticate: Basic realm=\"health\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    std::printf("running %s\n", qPrintable(file.fileName())); std::fflush(stdout);
    if (mode == "crash") QTimer::singleShot(20, &app, &QCoreApplication::quit);
    return app.exec();
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.contains("run")) return fakeCore(arguments, app);
    if (arguments.contains("--sentinel")) return app.exec();
    if (arguments.contains("--live-transport")) {
        // Explicit developer run only. Uses the already running loopback
        // proxy; does not start/stop any core or change application settings.
        DiscordTransport probe; DiscordProbeOptions options;
        options.proxy = QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 2080);
        probe.start(options); wait(&probe, SIGNAL(completed()), 15000);
        const auto result = probe.report();
        std::printf("%s\n", QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
        return result.value("udp_control") == "replies" ? 0 : 1;
    }
    if (arguments.contains("--live-private")) {
        QFile file(arguments.value(arguments.indexOf("--live-private") + 1)); if (!file.open(QIODevice::ReadOnly)) return 2;
        HealthCandidate candidate; candidate.protocol = "vless"; candidate.nodeId = "private-live-fixture";
        candidate.outbound = QJsonDocument::fromJson(file.readAll()).object();
        ProfileProbe probe;
        probe.start(candidate, "C:/GreenRhythm/greenrhythm_core.exe", {{"cloudflare", QUrl("https://cp.cloudflare.com/generate_204"), 204},
            {"google", QUrl("https://www.gstatic.com/generate_204"), 204}, {"ip_control", QUrl("https://1.1.1.1/cdn-cgi/trace"), 302},
            {"discord", QUrl("https://discord.com/api/v10/gateway"), 200}}, true);
        wait(&probe, SIGNAL(completed()), 25000); std::puts(QJsonDocument(probe.report()).toJson().constData());
        return probe.report().value("state") == "complete" ? 0 : 1;
    }
    std::fprintf(stderr, "TLS backend: %s; available: %s\n", qPrintable(QSslSocket::activeBackend()), qPrintable(QSslSocket::availableBackends().join(','))); std::fflush(stderr);
    Gateway gateway; Proxy proxy; proxy.gatewayPort = gateway.serverPort();
    const auto savedTls = QSslConfiguration::defaultConfiguration(); auto trusted = savedTls;
    auto authorities = trusted.caCertificates(); authorities.append(QSslCertificate(healthFixtureCaCertificate)); trusted.setCaCertificates(authorities);
    QSslConfiguration::setDefaultConfiguration(trusted);
    DiscordProbeOptions options; options.proxy = QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", proxy.serverPort());
    options.gateway = QUrl("wss://127.0.0.1:" + QString::number(gateway.serverPort()) + "/?v=10&encoding=json");
    options.udpEndpoints = {{"1.1.1.1", 3478}, {"8.8.8.8", 19302}}; // Owned relay replies; no public packets are sent.
    options.timeoutMs = 1500; options.udpIntervalMs = 100;
    auto run = [&] { DiscordTransport probe; probe.start(options); wait(&probe, SIGNAL(completed())); return probe.report(); };
    auto result = run();
    check(result.value("gateway_wss") == "hello", "verified TLS CONNECT and split RFC6455 handshake receive Discord opcode 10");
    check(result.value("udp_received") == 6 && result.value("udp_sent") == 6 && result.value("udp_control") == "replies", "six nonce-matched UDP replies are counted once despite bad nonces and duplicates");
    check(result.value("voice_verified") == false && result.value("udp_probe") == "stun_binding", "gateway and STUN never claim voice verification");
    const auto endpointReports = result.value("udp_endpoints").toArray();
    check(endpointReports.size() == 2 && endpointReports.first().toObject().value("rtt_median_ms").toInt(-1) >= 0
          && endpointReports.last().toObject().value("received").toInt() == 3, "UDP timing and response counts are reported separately for each destination");
    const auto safeTransport = QJsonDocument(result).toJson();
    check(!safeTransport.contains("1.1.1.1") && !safeTransport.contains("2112a442") && !safeTransport.contains("secret"), "STUN addresses, transactions and credentials are excluded from the report");
    gateway.mode = "fragmented"; check(run().value("gateway_wss") == "hello", "fragmented Gateway Hello with intervening ping is supported");
    for (const auto &mode : {"bad_accept", "wrong_opcode", "oversize"}) {
        gateway.mode = mode; check(run().value("gateway_wss") == "protocol", "invalid WebSocket handshake, opcode or oversized frame fails closed");
    }
    gateway.mode = "hello"; proxy.udpMode = "wrong_nonce"; result = run();
    check(result.value("udp_received") == 0 && result.value("udp_control") == "timeout", "wrong STUN transactions cannot pass a UDP probe");
    for (const auto &mode : {"bad_length", "wrong_source", "fragment", "dns_only"}) {
        proxy.udpMode = mode; result = run();
        check(result.value("udp_received") == 0 && result.value("udp_control") == "timeout", "malformed, fragmented, wrong-source and DNS-only responses never pass STUN");
    }
    proxy.udpMode = "partial"; result = run();
    check(result.value("udp_received") == 3 && result.value("udp_control") == "partial", "failure of one independent UDP destination remains visible");
    proxy.udpMode = "replies";
    const auto oldEndpoints = options.udpEndpoints;
    options.udpEndpoints = {{"fixture-one.invalid", 3478}, {"fixture-two.invalid", 19302}};
    check(run().value("udp_received") == 6, "SOCKS domain addressing checks STUN without local hostname lookups");
    options.udpEndpoints = {{"2001:db8::1", 3478}, {"2001:db8::2", 19302}};
    check(run().value("udp_received") == 6, "SOCKS IPv6 destination and reply headers are supported through the owned relay");
    options.udpEndpoints = oldEndpoints;
    proxy.udpMode = "replies"; proxy.reject = true;
    check(run().value("udp_control") == "proxy", "SOCKS negotiation rejection is not a UDP network loss verdict"); proxy.reject = false;
    QSslConfiguration::setDefaultConfiguration(savedTls);
    check(run().value("gateway_wss") == "tls", "untrusted Gateway certificate is rejected without ignoreSslErrors");
    DiscordTransport cancelled; cancelled.start(options); cancelled.cancel();
    check(cancelled.report().value("gateway_wss") == "cancelled" && cancelled.report().value("udp_control") == "cancelled", "cancel aborts both transports");
    const auto transaction = QByteArray(12, 'x');
    const auto validStun = QByteArray::fromHex("0101000c2112a442") + transaction + QByteArray::fromHex("0020000800011234e112a443");
    check(StunProbe::validReply(validStun, transaction), "RFC Binding success with XOR-MAPPED-ADDRESS passes");
    for (int size = 0; size < validStun.size(); ++size)
        check(!StunProbe::validReply(validStun.left(size), transaction), "every truncated STUN response is rejected");
    check(!StunProbe::validReply(validStun + '\0', transaction)
          && !StunProbe::validReply(QByteArray::fromHex("010100002112a442") + transaction, transaction), "extra bytes and success without a mapped address are rejected");
    QProcess sentinel; sentinel.start(app.applicationFilePath(), {"--sentinel"}); check(sentinel.waitForStarted(), "unrelated process started for isolation check");
    const auto before = QDir(QDir::tempPath()).entryList({"greenrhythm-health-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    const QList<ProfileProbeTarget> targets{{"control", QUrl("http://fixture.invalid/"), 200}};
    HealthCandidate candidate; candidate.protocol = "vless"; candidate.nodeId = "fixture";
    candidate.outbound = {{"type", "socks"}, {"server", "normal"}, {"server_port", 1234}, {"password", "fixture-secret"}};
    const auto savedProxy = qgetenv("HTTP_PROXY"); qputenv("HTTP_PROXY", "http://fixture.invalid:9");
    ProfileProbe probe; probe.start(candidate, app.applicationFilePath(), targets, false, 500);
    wait(&probe, SIGNAL(completed())); result = probe.report();
    if (savedProxy.isNull()) qunsetenv("HTTP_PROXY"); else qputenv("HTTP_PROXY", savedProxy);
    check(result.value("state") == "complete" && result.value("samples").toArray().size() == 3, "owned child performs three authenticated single-path HTTP probes");
    for (const auto &sample : result.value("samples").toArray()) check(sample.toObject().value("ok").toBool(), "407 challenge completes proxy authentication before HTTP verdict");
    check(!QJsonDocument(result).toJson().contains("fixture-secret"), "isolated report excludes private outbound and inbound credentials");
    candidate.outbound["server"] = "fail"; probe.start(candidate, app.applicationFilePath(), targets, false);
    wait(&probe, SIGNAL(completed())); check(probe.report().value("state") == "startup_failed", "core startup failure is explicit and reaped");
    candidate.outbound["server"] = "hang"; probe.start(candidate, app.applicationFilePath(), targets, false);
    QTimer::singleShot(50, &probe, &ProfileProbe::cancel); wait(&probe, SIGNAL(completed()));
    check(probe.report().value("state") == "cancelled", "cancelling a child before readiness stops only that child");
    probe.start(candidate, app.applicationFilePath(), targets, false);
    wait(&probe, SIGNAL(completed()), 8000);
    check(probe.report().value("state") == "startup_timeout", "a child that never signals readiness has a bounded startup deadline");
    candidate.outbound["server"] = "crash"; probe.start(candidate, app.applicationFilePath(), targets, false);
    wait(&probe, SIGNAL(completed()));
    check(probe.report().value("state") == "core_exited", "a core crash during probing cannot be reported as a completed comparison");
    probe.start(candidate, QDir::tempPath() + "/missing-greenrhythm-core-fixture", targets, false);
    check(probe.report().value("state") == "core_missing", "missing core is distinguished from a failed remote profile");
    check(sentinel.state() == QProcess::Running, "unrelated core/client process is never stopped");
    check(before == QDir(QDir::tempPath()).entryList({"greenrhythm-health-*"}, QDir::Dirs | QDir::NoDotAndDotDot), "all owned temporary credential files are removed after success, failure and cancel");
    sentinel.kill(); sentinel.waitForFinished();
    // Full model: active path stays up while current + two fresh alternatives run sequentially.
    Proxy active; active.gatewayPort = gateway.serverPort();
    ServiceHealth health; HealthContext context; context.profileRunning = true; context.profileProxy = QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", active.serverPort());
    context.coreExecutable = app.applicationFilePath(); candidate.outbound["server"] = "normal"; context.currentCandidate = candidate;
    // Offline read callback is covered by the actual importer integration test separately.
    context.subscription = QUrl();
    const QList<HealthTarget> healthTargets{{"control", "Control", QUrl("http://127.0.0.1:1/"), 200, true}};
    health.start(context, "general", 1, healthTargets, 0, 100); wait(&health, SIGNAL(completed()));
    result = health.report();
    check(result.value("profile_comparison").toArray().size() == 1 && result.value("profile_comparison").toArray().first().toObject().value("source") == "current_clean", "model includes clean current profile and never substitutes cached alternatives");
    check(health.summary().contains(QString::fromUtf8("Изолированный текущий профиль прошёл")), "model distinguishes active rules failure from an isolated successful profile without blaming ISP");
    QSslConfiguration::setDefaultConfiguration(trusted);
    Gateway subscription;
    context.subscription = QUrl("https://127.0.0.1:" + QString::number(subscription.serverPort()) + "/fixture-private-key");
    context.currentCandidate.outbound["server"] = "fail";
    bool parsed = false;
    health.readSubscription = [&](const QByteArray &body) {
        parsed = body == subscription.body;
        HealthSubscription fresh; fresh.count = 3; fresh.currentPresence = "absent";
        auto bad = candidate; bad.outbound["server"] = "fail";
        fresh.alternatives = {bad, candidate}; return fresh;
    };
    health.start(context, "general", 1, healthTargets, 0, 500); wait(&health, SIGNAL(completed())); result = health.report();
    const auto compared = result.value("profile_comparison").toArray();
    check(parsed && compared.size() == 3 && compared[0].toObject().value("state") == "startup_failed"
        && compared[1].toObject().value("source") == "fresh_alternative" && compared[2].toObject().value("state") == "complete",
        "fresh verified subscription feeds current plus two sequential isolated alternatives including a failing node");
    check(health.summary().contains(QString::fromUtf8("Альтернатива 2 прошла")), "successful fresh alternative is actionable without switching the live profile");
    check(!QJsonDocument(result).toJson().contains("fixture-private-key") && !QJsonDocument(result).toJson().contains(subscription.body), "fresh subscription keys and bodies never enter comparison report");
    context.currentCandidate.outbound["server"] = "hang";
    health.start(context, "general", 1, healthTargets, 0, 100);
    QTimer::singleShot(200, &health, &ServiceHealth::invalidate); wait(&health, SIGNAL(completed()));
    check(health.report().value("cancelled").toBool() && health.report().value("stale").toBool(), "context invalidation during an owned core probe cancels it and marks all results stale");
    check(before == QDir(QDir::tempPath()).entryList({"greenrhythm-health-*"}, QDir::Dirs | QDir::NoDotAndDotDot), "cancelling full comparison also reaps its child and removes credentials");
    QSslConfiguration::setDefaultConfiguration(savedTls);
    std::printf("%d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
