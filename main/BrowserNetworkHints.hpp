#pragma once

#include <QList>
#include <QNetworkProxy>
#include <QObject>
#include <QSslError>
#include <QUrl>
#include <memory>

namespace GreenRhythm {

struct BrowserDnsHint {
    QString browser;
    QString mode;
    bool customResolver = false;
};

enum class SystemProxyHint { Disabled, GreenRhythm, Other, Automatic, Unavailable };
struct BrowserNetworkHints {
    QList<BrowserDnsHint> browsers;
    SystemProxyHint systemProxy = SystemProxyHint::Unavailable;
};

// Reads only DNS-over-HTTPS settings from Local State. Never opens profile,
// cookie, session or login databases; resolver templates are not returned.
BrowserDnsHint parseBrowserDnsHint(const QString &browser, const QByteArray &localState);
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
