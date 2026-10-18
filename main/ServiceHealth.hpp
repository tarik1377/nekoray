#pragma once
#include "BrowserNetworkHints.hpp"
#include "ProfileProbe.hpp"
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <functional>

namespace GreenRhythm {
struct HealthTarget {
    QString id, title;
    QUrl url;
    int expectedStatus = 200;
    bool control = false;
};
struct HealthSample {
    QString target;
    int round = 0;
    WebCheckResult result;
    QDateTime observedUtc;
};
struct HealthContext {
    QString version, protocol, nodeId, routeFingerprint, tunStack;
    int mtu = 0;
    bool profileRunning = false, tunEnabled = false;
    QNetworkProxy profileProxy;
    QUrl subscription;
    QString subscriptionUserAgent;
    bool subscriptionViaProfile = false;
    int cachedProfiles = 0, lastUpdateOutcome = 0;
    qint64 lastUpdateUtc = 0;
    HealthCandidate currentCandidate;
    QString coreExecutable;
};
QList<HealthTarget> healthTargets(const QString &service);
QString healthPathName(WebCheckPath path);

// A local, user-started snapshot. No uploads, import, route changes or reconnects.
// The system comparison explicitly avoids a proxy, but is NOT an independent
// ISP baseline: a TUN or another VPN can still capture it.
class ServiceHealth final : public QObject {
    Q_OBJECT
public:
    explicit ServiceHealth(QObject *parent = nullptr);
    ~ServiceHealth() override;
    bool start(const HealthContext &context, const QString &service, int rounds = 3,
               const QList<HealthTarget> &targets = {}, int intervalMs = 3000, int timeoutMs = 4000);
    void cancel();
    bool isRunning() const { return running; }
    QString summary() const;
    QString details() const;
    QJsonObject report() const;
    std::function<int(const QByteArray &)> validateSubscription;
    std::function<HealthSubscription(const QByteArray &)> readSubscription;
    // Allows a cancelled/stale run to remain visible without claiming success.
    void invalidate();
    void recordDiscordObservation(int viaTunnel, int direct, int udp, bool limited,
                                  const QDateTime &from, const QDateTime &to);
signals:
    void updated(int completed, int total);
    void completed();
private:
    void nextRound();
    void maybeFinish();
    void fetchDocument(bool subscription);
    void stopJobs();
    void nextCandidate();
    QString comparisonConclusion() const;
    HealthContext context;
    QString service, reportId;
    QList<HealthTarget> targets;
    QList<HealthSample> samples;
    QList<BrowserNetworkProbe *> probes;
    QList<QNetworkAccessManager *> documents;
    QTimer next;
    QDateTime started, finished;
    int rounds = 0, round = 0, pending = 0, documentPending = 0;
    int interval = 3000, timeout = 4000, done = 0, total = 0;
    quint64 generation = 0;
    bool running = false, cancelled = false, stale = false;
    QString subscriptionState = "not_checked", discordState = "not_checked";
    int subscriptionHttp = 0, subscriptionProfiles = 0;
    QJsonObject discordObservation;
    HealthSubscription fresh;
    DiscordTransport activeTransport;
    ProfileProbe *isolated = nullptr;
    QJsonArray comparison;
    QStringList comparisonDetails;
    QList<HealthCandidate> candidates;
    bool comparisonStarted = false, comparisonDone = true, activeTransportDone = true;
    QTimer overall;
    bool deadlineExceeded = false;
};
} // namespace GreenRhythm
