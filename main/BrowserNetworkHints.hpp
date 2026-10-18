#pragma once

#include <QList>
#include <QNetworkProxy>
#include <QObject>
#include <QSslError>
#include <QUrl>
#include <QVariant>
#include <memory>

namespace GreenRhythm {

enum class BrowserDnsHintSource { LocalState, MachinePolicy, UserPolicy };
struct BrowserDnsHint {
    QString browser;
    QString mode;
    bool customResolver = false;
    BrowserDnsHintSource source = BrowserDnsHintSource::LocalState;
};

struct BrowserDnsPolicyValue {
    bool present = false;
    QVariant value;
};

enum class SystemProxyHint { Disabled, GreenRhythm, Other, Automatic, Unavailable };
SystemProxyHint parseSystemProxyHint(quint32 flags, const QString &server, quint16 greenRhythmPort,
                                    bool querySucceeded = true);
struct BrowserNetworkHints {
    QList<BrowserDnsHint> browsers;
    SystemProxyHint systemProxy = SystemProxyHint::Unavailable;
};

// Reads only DNS-over-HTTPS settings from Local State and mandatory Windows
// policy keys. Never opens profile, cookie, session or login databases;
// resolver templates are not returned. Browser policy reload is not verified.
BrowserDnsHint parseBrowserDnsHint(const QString &browser, const QByteArray &localState);
// Highest present policy wins. An invalid policy reports an unknown mode;
// it cannot prove that a lower policy or saved preference is active at runtime.
BrowserDnsHint applyBrowserDnsPolicies(BrowserDnsHint saved, const BrowserDnsPolicyValue &machine,
                                     const BrowserDnsPolicyValue &user);
BrowserNetworkHints readBrowserNetworkHints(quint16 greenRhythmPort);
QString browserNetworkHintsText(const BrowserNetworkHints &hints);
bool parseWebCheckUrl(const QString &text, QUrl *url, QString *reason);
QString maskedWebCheckUrl(const QUrl &url);

enum class WebCheckPath { System, GreenRhythm };
enum class WebCheckKind { Http, Dns, Tcp, Tls, Timeout, Cancelled, Network, Unavailable };
enum class WebTlsIssue { Handshake, Trust, Name, Validity };
WebTlsIssue webTlsIssue(const QList<QSslError> &errors);
struct WebCheckResult {
    WebCheckPath path = WebCheckPath::System;
    WebCheckKind kind = WebCheckKind::Network;
    int httpStatus = 0;
    qint64 elapsedMs = 0;
    WebTlsIssue tlsIssue = WebTlsIssue::Handshake;
};
QString webCheckResultText(const WebCheckResult &result);
QString compareWebCheckResults(const WebCheckResult &system, const WebCheckResult &greenRhythm);

struct WebCheckOptions {
    QNetworkProxy greenRhythmProxy = QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 2080);
    QNetworkProxy systemProxy = QNetworkProxy(QNetworkProxy::DefaultProxy);
    bool profileRunning = false;
    bool checkSystemPath = true;
    int timeoutMs = 4000;
};

// Two independent requests, no browser cookies/credentials, no response bodies
// or headers retained. Results describe connectivity, never account readiness.
class BrowserNetworkProbe final : public QObject {
    Q_OBJECT
public:
    explicit BrowserNetworkProbe(QObject *parent = nullptr);
    ~BrowserNetworkProbe() override;
    bool start(const QUrl &url, const WebCheckOptions &options);
    void cancel();
    bool isRunning() const { return pending > 0; }
signals:
    void pathFinished(const GreenRhythm::WebCheckResult &result);
    void completed();
private:
    struct Job;
    void abortPending(bool reportCancellation);
    void finish(const std::shared_ptr<Job> &job, WebCheckKind kind, int status = 0);
    QList<std::shared_ptr<Job>> jobs;
    quint64 generation = 0;
    int pending = 0;
};

} // namespace GreenRhythm

Q_DECLARE_METATYPE(GreenRhythm::WebCheckResult)
