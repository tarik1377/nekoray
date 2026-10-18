#pragma once
#include "BrowserNetworkHints.hpp"
#include "DiscordTransport.hpp"
#include <QProcess>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QDateTime>

namespace GreenRhythm {
struct HealthCandidate {
    QString protocol, nodeId, unavailable;
    QJsonObject outbound; // Private credentials; never serialized in a report.
};
struct HealthSubscription {
    int count = 0, unsupported = 0;
    QString currentPresence = "unknown";
    QList<HealthCandidate> alternatives;
};
struct ProfileProbeTarget { QString id; QUrl url; int expected = 200; };
// Owns exactly one child. Never calls the GUI core controller, Test RPC,
// taskkill/pkill, import, system proxy, route, DNS or TUN APIs.
class ProfileProbe final : public QObject {
    Q_OBJECT
public:
    explicit ProfileProbe(QObject *parent = nullptr);
    ~ProfileProbe() override;
    void start(const HealthCandidate &candidate, const QString &coreExecutable,
               const QList<ProfileProbeTarget> &targets, bool discord, int timeoutMs = 4000);
    void cancel();
    QJsonObject report() const;
    QString details() const;
signals:
    void completed();
private:
    void nextRound();
    void maybeFinish();
    void finish(const QString &state);
    void reap();
    HealthCandidate candidate;
    QList<ProfileProbeTarget> targets;
    QList<BrowserNetworkProbe *> requests;
    QJsonArray samples;
    QHash<QString, WebCheckResult> lastResults;
    QProcess process;
    std::unique_ptr<QTemporaryDir> directory;
    DiscordTransport transport;
    QTimer deadline, roundTimer;
    QByteArray output, readyMarker;
    QNetworkProxy proxy;
    QString state = "not_checked";
    bool running = false, ready = false, discord = false, transportDone = true;
    int round = 0, pending = 0, timeout = 4000;
    QDateTime started, finished;
};
} // namespace GreenRhythm
