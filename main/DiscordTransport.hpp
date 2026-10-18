#pragma once
#include <QObject>
#include <QNetworkProxy>
#include <QJsonObject>
#include <QSslSocket>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QUrl>
#include <QHash>

namespace GreenRhythm {
struct DiscordProbeOptions {
    QNetworkProxy proxy;
    QUrl gateway = QUrl("wss://gateway.discord.gg/?v=10&encoding=json");
    QList<QHostAddress> resolvers{QHostAddress("1.1.1.1"), QHostAddress("8.8.8.8")};
    quint16 resolverPort = 53;
    int timeoutMs = 5000, udpIntervalMs = 1200;
};
// No login, voice/session tokens, cookies or application data. Gateway Hello
// checks WSS; nonce-matched DNS responses check a separate, generic UDP path.
class DiscordTransport final : public QObject {
    Q_OBJECT
public:
    explicit DiscordTransport(QObject *parent = nullptr);
    ~DiscordTransport() override;
    void start(const DiscordProbeOptions &options);
    void cancel();
    QJsonObject report() const;
    QString details() const;
signals:
    void completed();
private:
    void wsRead();
    void wsFinish(const QString &state);
    void sendFrame(quint8 opcode, const QByteArray &data);
    void socksRead();
    void sendUdpRound();
    void udpRead();
    void udpFinish(const QString &state);
    void maybeFinish();
    void stop();
    DiscordProbeOptions options;
    QSslSocket ws;
    QTcpSocket socks;
    QUdpSocket udp;
    QTimer wsDeadline, udpDeadline, udpRound;
    QByteArray wsBuffer, wsText, key, socksBuffer;
    bool upgraded = false, fragmented = false, running = false;
    QString wsState = "not_checked", udpState = "not_checked";
    int socksStage = 0, round = 0, sent = 0, received = 0;
    quint16 relayPort = 0;
    QHostAddress relay;
    struct Question { QByteArray bytes; QHostAddress resolver; bool answered = false; };
    QHash<quint16, Question> questions;
};
} // namespace GreenRhythm
