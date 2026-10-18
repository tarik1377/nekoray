#include "DiscordTransport.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QtEndian>
#include <algorithm>

namespace GreenRhythm {
namespace {
QByteArray randomBytes(int count) {
    QByteArray bytes(count, Qt::Uninitialized);
    for (auto &byte : bytes) byte = char(QRandomGenerator::system()->generate() & 255);
    return bytes;
}
void append16(QByteArray &data, quint16 value) { data.append(char(value >> 8)); data.append(char(value)); }
quint16 read16(const QByteArray &data, int offset) { return qFromBigEndian<quint16>(data.constData() + offset); }
QString stateText(const QString &state) {
    if (state == "hello") return QObject::tr("получен Gateway Hello");
    if (state == "replies") return QObject::tr("получены проверенные ответы");
    if (state == "partial") return QObject::tr("часть ответов не получена");
    if (state == "tls") return QObject::tr("ошибка TLS/сертификата");
    if (state == "timeout") return QObject::tr("ответ не получен до срока ожидания");
    if (state == "protocol") return QObject::tr("неожиданный ответ протокола");
    if (state == "cancelled") return QObject::tr("отменено");
    if (state == "pending") return QObject::tr("проверяется");
    if (state == "proxy") return QObject::tr("локальный прокси недоступен или отказал");
    if (state == "network") return QObject::tr("соединение не установлено или закрыто до Hello");
    return QObject::tr("не проверено");
}
}
DiscordTransport::DiscordTransport(QObject *parent) : QObject(parent) {
    wsDeadline.setSingleShot(true); udpDeadline.setSingleShot(true);
    connect(&wsDeadline, &QTimer::timeout, this, [this] { wsFinish("timeout"); });
    connect(&udpDeadline, &QTimer::timeout, this, [this] { udpFinish(received ? "partial" : "timeout"); });
    connect(&udpRound, &QTimer::timeout, this, &DiscordTransport::sendUdpRound);
    connect(&ws, &QSslSocket::encrypted, this, [this] {
        key = randomBytes(16).toBase64();
        auto path = options.gateway.path(QUrl::FullyEncoded).toUtf8();
        if (path.isEmpty()) path = "/";
        if (options.gateway.hasQuery()) path += '?' + options.gateway.query(QUrl::FullyEncoded).toUtf8();
        const auto host = options.gateway.host().toUtf8() + ':' + QByteArray::number(options.gateway.port(443));
        ws.write("GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + key + "\r\n\r\n");
    });
    connect(&ws, &QSslSocket::readyRead, this, &DiscordTransport::wsRead);
    connect(&ws, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &) { wsFinish("tls"); });
    connect(&ws, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        wsFinish(error == QAbstractSocket::SslHandshakeFailedError ? "tls"
            : error == QAbstractSocket::ProxyAuthenticationRequiredError || error == QAbstractSocket::ProxyConnectionRefusedError
                || error == QAbstractSocket::ProxyConnectionClosedError || error == QAbstractSocket::ProxyNotFoundError ? "proxy" : "network");
    });
    socks.setProxy(QNetworkProxy::NoProxy); udp.setProxy(QNetworkProxy::NoProxy);
    connect(&socks, &QTcpSocket::connected, this, [this] {
        socksStage = 1;
        socks.write(options.proxy.user().isEmpty() ? QByteArray::fromHex("050100") : QByteArray::fromHex("050102"));
    });
    connect(&socks, &QTcpSocket::readyRead, this, &DiscordTransport::socksRead);
    connect(&socks, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { udpFinish("proxy"); });
    connect(&socks, &QTcpSocket::disconnected, this, [this] { udpFinish("proxy"); });
    connect(&udp, &QUdpSocket::readyRead, this, &DiscordTransport::udpRead);
}
DiscordTransport::~DiscordTransport() { running = false; stop(); }
void DiscordTransport::start(const DiscordProbeOptions &value) {
    running = false; stop(); options = value;
    wsState = udpState = "pending"; wsBuffer.clear(); wsText.clear(); socksBuffer.clear(); questions.clear();
    upgraded = fragmented = false; socksStage = round = sent = received = 0; running = true;
    QHostAddress proxyAddress(options.proxy.hostName());
    if (!proxyAddress.isLoopback() || !options.proxy.port() || options.gateway.scheme() != "wss"
        || !options.gateway.userInfo().isEmpty() || options.resolvers.size() != 2
        || options.proxy.user().toUtf8().size() > 255 || options.proxy.password().toUtf8().size() > 255) {
        wsFinish("proxy"); udpFinish("proxy"); return;
    }
    ws.setReadBufferSize(32768); socks.setReadBufferSize(512);
    ws.setProxy(options.proxy);
    wsDeadline.start(std::clamp(options.timeoutMs, 50, 10000));
    ws.connectToHostEncrypted(options.gateway.host(), quint16(options.gateway.port(443)));
    if (!udp.bind(proxyAddress.protocol() == QAbstractSocket::IPv6Protocol ? QHostAddress::LocalHostIPv6 : QHostAddress::LocalHost, 0)) { udpFinish("proxy"); return; }
    udpDeadline.start(std::clamp(options.timeoutMs, 50, 10000) + 3 * std::clamp(options.udpIntervalMs, 20, 2000));
    socks.connectToHost(proxyAddress, options.proxy.port());
}
void DiscordTransport::sendFrame(quint8 opcode, const QByteArray &data) {
    if (data.size() > 125) return;
    const auto mask = randomBytes(4);
    QByteArray frame; frame.append(char(0x80 | opcode)); frame.append(char(0x80 | data.size())); frame += mask;
    for (int i = 0; i < data.size(); ++i) frame.append(data[i] ^ mask[i % 4]);
    ws.write(frame);
}
void DiscordTransport::wsRead() {
    if (!running || wsState != "pending") return;
    wsBuffer += ws.read(32769 - wsBuffer.size());
    if (wsBuffer.size() > 32768) { wsFinish("protocol"); return; }
    if (!upgraded) {
        const int end = wsBuffer.indexOf("\r\n\r\n");
        if (end < 0) { if (wsBuffer.size() > 8192) wsFinish("protocol"); return; }
        const auto lines = wsBuffer.left(end).split('\n');
        QHash<QByteArray, QByteArray> headers;
        for (int i = 1; i < lines.size(); ++i) {
            const int colon = lines[i].indexOf(':');
            if (colon > 0) headers[lines[i].left(colon).trimmed().toLower()] += lines[i].mid(colon + 1).trimmed();
        }
        const auto accept = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
        const auto connectionTokens = headers.value("connection").toLower().split(',');
        bool connectionUpgrade = false;
        for (const auto &token : connectionTokens) connectionUpgrade |= token.trimmed() == "upgrade";
        if (lines.first().trimmed().split(' ').value(1) != "101" || headers.value("sec-websocket-accept") != accept
            || headers.value("upgrade").toLower() != "websocket" || !connectionUpgrade
            || headers.contains("sec-websocket-extensions")) { wsFinish("protocol"); return; }
        wsBuffer.remove(0, end + 4); upgraded = true;
    }
    while (wsBuffer.size() >= 2 && wsState == "pending") {
        const quint8 first = quint8(wsBuffer[0]), second = quint8(wsBuffer[1]);
        const int opcode = first & 15; const bool fin = first & 128;
        quint64 length = second & 127; int offset = 2;
        if ((first & 0x70) || (second & 128) || length == 127) { wsFinish("protocol"); return; }
        if (length == 126) { if (wsBuffer.size() < 4) return; length = read16(wsBuffer, 2); offset = 4; }
        if (length > 16384 || (opcode >= 8 && (!fin || length > 125))) { wsFinish("protocol"); return; }
        if (wsBuffer.size() < offset + int(length)) return;
        const auto payload = wsBuffer.mid(offset, int(length)); wsBuffer.remove(0, offset + int(length));
        if (opcode == 9) { sendFrame(10, payload); continue; }
        if (opcode == 10) continue;
        if ((opcode == 1 && fragmented) || (opcode == 0 && !fragmented) || (opcode != 0 && opcode != 1)) { wsFinish("protocol"); return; }
        wsText += payload; fragmented = !fin;
        if (wsText.size() > 16384) { wsFinish("protocol"); return; }
        if (fin) {
            const auto json = QJsonDocument::fromJson(wsText).object();
            const auto heartbeat = json.value("d").toObject().value("heartbeat_interval").toDouble();
            wsFinish(json.value("op").toInt(-1) == 10 && heartbeat > 0 && heartbeat <= 300000 ? "hello" : "protocol");
            return;
        }
    }
}
void DiscordTransport::socksRead() {
    if (!running || udpState != "pending") return;
    socksBuffer += socks.read(513 - socksBuffer.size());
    if (socksBuffer.size() > 512) { udpFinish("protocol"); return; }
    if (socksStage == 1 || socksStage == 2) {
        if (socksBuffer.size() < 2) return;
        const auto response = socksBuffer.left(2); socksBuffer.remove(0, 2);
        if (socksStage == 1 && response == QByteArray::fromHex("0502") && !options.proxy.user().isEmpty()) {
            const auto user = options.proxy.user().toUtf8(), password = options.proxy.password().toUtf8();
            QByteArray auth(1, char(1)); auth.append(char(user.size())); auth += user; auth.append(char(password.size())); auth += password;
            socksStage = 2; socks.write(auth); return;
        }
        if ((socksStage == 1 && response != QByteArray::fromHex("0500"))
            || (socksStage == 2 && response != QByteArray::fromHex("0100"))) { udpFinish("proxy"); return; }
        socksStage = 3; socks.write(QByteArray::fromHex("05030001000000000000"));
    }
    if (socksStage != 3 || socksBuffer.size() < 4) return;
    if (quint8(socksBuffer[0]) != 5 || socksBuffer[1] != 0 || socksBuffer[2] != 0) { udpFinish("proxy"); return; }
    const int type = quint8(socksBuffer[3]); const int size = type == 1 ? 10 : type == 4 ? 22 : -1;
    if (size < 0) { udpFinish("protocol"); return; }
    if (socksBuffer.size() < size) return;
    if (type == 1) relay = QHostAddress(qFromBigEndian<quint32>(socksBuffer.constData() + 4));
    else { Q_IPV6ADDR address; std::copy_n(reinterpret_cast<const quint8 *>(socksBuffer.constData() + 4), 16, address.c); relay = QHostAddress(address); }
    relayPort = read16(socksBuffer, size - 2);
    if (relay.isNull() || relay == QHostAddress::AnyIPv4 || relay == QHostAddress::AnyIPv6) relay = socks.peerAddress();
    if (!relay.isLoopback() || !relayPort) { udpFinish("protocol"); return; }
    socksStage = 4; udpDeadline.stop();
    const int interval = std::clamp(options.udpIntervalMs, 20, 2000);
    udpDeadline.start(3 * interval + 100); udpRound.start(interval); sendUdpRound();
}
void DiscordTransport::sendUdpRound() {
    if (!running || udpState != "pending" || round >= 3) { udpRound.stop(); return; }
    ++round;
    for (const auto &resolver : options.resolvers) {
        quint16 id; do { id = quint16(QRandomGenerator::system()->generate()); } while (questions.contains(id));
        const auto label = randomBytes(8).toHex();
        QByteArray question; question.append(char(label.size())); question += label;
        question += QByteArray::fromHex("076578616d706c6503636f6d0000010001");
        questions.insert(id, {question, resolver, false});
        QByteArray dns; append16(dns, id); dns += QByteArray::fromHex("01000001000000000000"); dns += question;
        if (resolver.protocol() != QAbstractSocket::IPv4Protocol) { udpFinish("protocol"); return; }
        QByteArray packet = QByteArray::fromHex("00000001");
        const auto address = resolver.toIPv4Address();
        for (int shift = 24; shift >= 0; shift -= 8) packet.append(char(address >> shift));
        append16(packet, options.resolverPort); packet += dns; ++sent;
        udp.writeDatagram(packet, relay, relayPort);
    }
}
void DiscordTransport::udpRead() {
    int budget = 64;
    while (udp.hasPendingDatagrams() && budget-- > 0) {
        QByteArray packet(4096, Qt::Uninitialized); QHostAddress sender; quint16 port;
        const auto length = udp.readDatagram(packet.data(), packet.size(), &sender, &port);
        if (length < 0) { udpFinish("network"); return; }
        packet.resize(int(length));
        if (!running || udpState != "pending" || sender != relay || port != relayPort || packet.size() < 22
            || packet.left(4) != QByteArray::fromHex("00000001") || read16(packet, 8) != options.resolverPort) continue;
        const auto resolver = QHostAddress(qFromBigEndian<quint32>(packet.constData() + 4));
        const auto dns = packet.mid(10); const auto id = read16(dns, 0);
        auto question = questions.find(id);
        if (question == questions.end() || question->answered || question->resolver != resolver || !(quint8(dns[2]) & 128)
            || (quint8(dns[2]) & 0x7a) || (quint8(dns[3]) & 15) > 3 || ((quint8(dns[3]) & 15) != 0 && (quint8(dns[3]) & 15) != 3)
            || read16(dns, 4) != 1 || dns.mid(12, question->bytes.size()) != question->bytes) continue;
        question->answered = true; ++received;
        if (sent == 6 && received == 6) { udpFinish("replies"); return; }
    }
    if (running && udpState == "pending" && udp.hasPendingDatagrams()) QTimer::singleShot(0, this, &DiscordTransport::udpRead);
}
void DiscordTransport::wsFinish(const QString &state) {
    if (!running || wsState != "pending") return;
    wsState = state; wsDeadline.stop(); ws.abort(); wsBuffer.clear(); wsText.clear(); maybeFinish();
}
void DiscordTransport::udpFinish(const QString &state) {
    if (!running || udpState != "pending") return;
    udpState = state; udpDeadline.stop(); udpRound.stop(); socks.abort(); udp.close(); questions.clear(); maybeFinish();
}
void DiscordTransport::maybeFinish() {
    if (running && wsState != "pending" && udpState != "pending") { running = false; emit completed(); }
}
void DiscordTransport::stop() { wsDeadline.stop(); udpDeadline.stop(); udpRound.stop(); ws.abort(); socks.abort(); udp.close(); }
void DiscordTransport::cancel() { if (!running) return; wsFinish("cancelled"); udpFinish("cancelled"); }
QJsonObject DiscordTransport::report() const {
    return {{"gateway_wss", wsState}, {"udp_control", udpState}, {"udp_sent", sent}, {"udp_received", received},
        {"voice_verified", false}, {"udp_resolvers", 2}};
}
QString DiscordTransport::details() const {
    return tr("Discord Gateway WebSocket: %1.\nКонтроль UDP: %2; ответы %3/%4. Это не проверка голосовой сессии Discord.")
        .arg(stateText(wsState), stateText(udpState)).arg(received).arg(sent);
}
} // namespace GreenRhythm
