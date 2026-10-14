#include "main/BrowserNetworkHints.hpp"
#include "ui/MainShell.hpp"
#include "ui/dialog_web_diagnostics.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <iostream>

using namespace GreenRhythm;

class HttpFixture : public QTcpServer {
public:
    int status = 200;
    bool silent = false;
    QStringList requestLines;
    HttpFixture() {
        listen(QHostAddress::LocalHost);
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const auto data = socket->readAll();
                    if (silent) return;
                    // TLS-to-plain-HTTP is a deliberate handshake failure.
                    if (!data.startsWith("GET ") && !data.startsWith("HEAD ")) { socket->write("HTTP/1.1 200 OK\r\n\r\n"); socket->disconnectFromHost(); return; }
                    requestLines.append(QString::fromLatin1(data.left(data.indexOf('\r'))));
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: 999999\r\nConnection: close\r\n\r\n");
                    // Body never arrives: a status result must finish at headers.
                    socket->flush();
                });
            }
        });
    }
    QUrl url(const QString &scheme = QStringLiteral("http")) const { return QUrl(QStringLiteral("%1://127.0.0.1:%2/test?private=value").arg(scheme).arg(serverPort())); }
};

struct ProbeRun { QList<WebCheckResult> results; int completions = 0; bool completed = false; qint64 elapsed = 0; };
static ProbeRun run(BrowserNetworkProbe &probe, const QUrl &url, WebCheckOptions options, int cancelAfter = -1) {
    ProbeRun result; QEventLoop loop; QElapsedTimer elapsed; elapsed.start();
    auto got = QObject::connect(&probe, &BrowserNetworkProbe::pathFinished, &loop, [&](const WebCheckResult &value) { result.results.append(value); });
    auto done = QObject::connect(&probe, &BrowserNetworkProbe::completed, &loop, [&] { ++result.completions; result.completed = true; loop.quit(); });
    QTimer safety; safety.setSingleShot(true); QObject::connect(&safety, &QTimer::timeout, &loop, &QEventLoop::quit); safety.start(2500);
    probe.start(url, options);
    if (cancelAfter >= 0) QTimer::singleShot(cancelAfter, &probe, &BrowserNetworkProbe::cancel);
    if (!result.completed) loop.exec();
    result.elapsed = elapsed.elapsed(); QObject::disconnect(got); QObject::disconnect(done);
    return result;
}
static WebCheckResult pathResult(const ProbeRun &run, WebCheckPath path) {
    for (const auto &result : run.results) if (result.path == path) return result;
    return {};
}

int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *label) { ++checks; if (!ok) ++failures; std::cout << (ok ? "PASS " : "FAIL ") << label << '\n'; };
    const auto secure = parseBrowserDnsHint(QStringLiteral("Fixture browser"), R"({"dns_over_https":{"mode":"secure","templates":"https://resolver.invalid/private-token"},"profile":{"last_used":"Secret name"},"unrelated":"secret"})");
    check(secure.mode == QStringLiteral("secure") && secure.customResolver, "detect secure browser DNS from Local State independently of last profile");
    BrowserNetworkHints hints; hints.browsers.append(secure); hints.systemProxy = SystemProxyHint::Disabled;
    const auto hintText = browserNetworkHintsText(hints);
    check(!hintText.contains(QStringLiteral("private-token")) && !hintText.contains(QStringLiteral("Secret name")) && !hintText.contains(QStringLiteral("resolver.invalid")), "hints do not reveal DNS template paths or profile identity");
    check(parseBrowserDnsHint({}, R"({"dns_over_https":{"mode":"automatic"}})").mode == QStringLiteral("automatic"), "automatic DNS is described separately from mandatory secure mode");
    check(parseBrowserDnsHint({}, R"({"dns_over_https":{"mode":"off"}})").mode == QStringLiteral("off"), "disabled browser secure DNS is retained");
    check(parseBrowserDnsHint({}, "broken").mode.isEmpty() && parseBrowserDnsHint({}, R"({"dns_over_https":{"mode":"<script>"}})").mode.isEmpty(), "malformed or unknown mode cannot become a DNS diagnosis");
    check(parseBrowserDnsHint({}, QByteArray(8 * 1024 * 1024 + 1, 'x')).mode.isEmpty(), "Local State parsing has a fixed input size limit");
    QUrl url; QString reason;
    check(parseWebCheckUrl(QStringLiteral("example.invalid/path?secret=value#fragment"), &url, &reason) && url.scheme() == QStringLiteral("https") && url.fragment().isEmpty(), "bare hostname gets HTTPS and fragments are excluded from requests");
    check(!parseWebCheckUrl(QStringLiteral("file:///private"), &url, &reason) && !parseWebCheckUrl(QStringLiteral("https://user:password@example.invalid/"), &url, &reason), "only HTTP sites without URL credentials are accepted");
    check(!parseWebCheckUrl(QStringLiteral("https://"), &url, &reason), "empty host is rejected before any request");
    check(maskedWebCheckUrl(QUrl(QStringLiteral("https://user:pass@example.invalid:444/private/token?q=secret#hidden"))) == QStringLiteral("https://example.invalid:444/…"), "masked URL removes credentials, full path, query and fragment");
    const WebCheckResult unauthorized{WebCheckPath::System, WebCheckKind::Http, 401};
    const WebCheckResult forbidden{WebCheckPath::System, WebCheckKind::Http, 403};
    check(webCheckResultText(unauthorized).contains(QStringLiteral("401")) && webCheckResultText(unauthorized).contains(QStringLiteral("сессию")), "HTTP 401 is connectivity with missing authorization, never account success");
    check(webCheckResultText(forbidden).contains(QStringLiteral("403")) && webCheckResultText(forbidden).contains(QStringLiteral("не установлена")), "HTTP 403 does not diagnose the user's browser account");
    check(webTlsIssue({QSslError(QSslError::SelfSignedCertificate)}) == WebTlsIssue::Trust, "private CA trust failure is distinct from generic TLS negotiation");
    check(webTlsIssue({QSslError(QSslError::HostNameMismatch)}) == WebTlsIssue::Name && webTlsIssue({QSslError(QSslError::CertificateExpired)}) == WebTlsIssue::Validity, "certificate name and validity errors remain distinguishable");
    check(webCheckResultText({WebCheckPath::System, WebCheckKind::Tls, 0, 0, WebTlsIssue::Trust}).contains(QStringLiteral("не исправляет")), "proxy connection is not offered as a cure for certificate trust");

    HttpFixture direct, proxy;
    check(direct.isListening() && proxy.isListening(), "HTTP fixtures are bound only on loopback");
    WebCheckOptions options; options.systemProxy = QNetworkProxy(QNetworkProxy::NoProxy); options.profileRunning = true; options.timeoutMs = 500;
    options.greenRhythmProxy = QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), proxy.serverPort());
    BrowserNetworkProbe probe;
    auto ordinary = run(probe, direct.url(), options);
    check(ordinary.completed && ordinary.results.size() == 2 && ordinary.completions == 1, "two independent HTTP paths complete exactly once");
    check(pathResult(ordinary, WebCheckPath::System).httpStatus == 200 && pathResult(ordinary, WebCheckPath::GreenRhythm).httpStatus == 200 && ordinary.elapsed < 500, "status result does not wait for missing response body");
    check(!proxy.requestLines.isEmpty() && proxy.requestLines.last().startsWith(QStringLiteral("HEAD http://")), "explicit proxy sends full destination URL to proxy rather than resolving it locally");
    proxy.status = 403; direct.status = 401;
    auto denied = run(probe, direct.url(), options);
    check(pathResult(denied, WebCheckPath::System).httpStatus == 401 && pathResult(denied, WebCheckPath::GreenRhythm).httpStatus == 403, "server refusal statuses are retained on both paths");
    check(compareWebCheckResults(pathResult(denied, WebCheckPath::System), pathResult(denied, WebCheckPath::GreenRhythm)).contains(QStringLiteral("не определена")), "two reachable paths never claim the browser account is repaired");

    HttpFixture silent; silent.silent = true;
    options.greenRhythmProxy.setPort(silent.serverPort()); options.timeoutMs = 120;
    auto timeout = run(probe, silent.url(), options);
    check(timeout.completed && timeout.results.size() == 2 && timeout.elapsed < 1000
        && pathResult(timeout, WebCheckPath::System).kind == WebCheckKind::Timeout
        && pathResult(timeout, WebCheckPath::GreenRhythm).kind == WebCheckKind::Timeout, "silent endpoints stop at the total deadline on both paths");
    options.timeoutMs = 1200;
    auto cancelled = run(probe, silent.url(), options, 20);
    check(cancelled.completed && cancelled.results.size() == 2 && cancelled.completions == 1 && cancelled.elapsed < 500
        && pathResult(cancelled, WebCheckPath::System).kind == WebCheckKind::Cancelled, "cancel completes both pending requests exactly once");
    options.profileRunning = false;
    auto unavailable = run(probe, direct.url(), options);
    check(pathResult(unavailable, WebCheckPath::GreenRhythm).kind == WebCheckKind::Unavailable && unavailable.completed, "stopped GreenRhythm is reported without connecting to another listener");
    options.profileRunning = true; options.greenRhythmProxy.setPort(proxy.serverPort()); options.timeoutMs = 1000;
    auto tls = run(probe, direct.url(QStringLiteral("https")), options);
    check(tls.completed && pathResult(tls, WebCheckPath::System).kind == WebCheckKind::Tls
        && pathResult(tls, WebCheckPath::System).httpStatus == 0, "HTTPS handshake failure never becomes a successful plain HTTP result");
    auto again = run(probe, direct.url(), options);
    check(again.completed && again.results.size() == 2 && again.completions == 1, "new check after cancellation or TLS error has no stale callbacks");

    MainShell shell; shell.adopt(new QWidget, new QWidget);
    auto *entry = shell.findChild<QPushButton *>(QStringLiteral("grBrowserHelp")); int entryCalls = 0;
    QObject::connect(&shell, &MainShell::browserHelpRequested, [&] { ++entryCalls; });
    check(entry && !entry->accessibleName().isEmpty(), "browser help entry is accessible from connection page");
    if (entry) entry->click();
    check(entryCalls == 1 && !shell.isConnecting(), "browser help opens diagnostics without toggling VPN");
    DialogWebDiagnostics dialog; dialog.setProxyEndpoint(2080, QStringLiteral("private-user"), QStringLiteral("private-password"), true); dialog.setHints(hints);
    auto *connectButton = dialog.findChild<QPushButton *>(QStringLiteral("grConnectBrowsers"));
    auto *leaveButton = dialog.findChild<QPushButton *>(QStringLiteral("grLeaveBrowserSettings"));
    auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("grWebAddress")); int applyCalls = 0;
    QObject::connect(&dialog, &DialogWebDiagnostics::connectBrowsersRequested, [&] { ++applyCalls; });
#ifdef Q_OS_WIN
    check(connectButton && connectButton->isEnabled(), "connect browsers is enabled only for a running Windows profile");
    if (connectButton) connectButton->click();
    check(applyCalls == 1, "connect browsers emits controller signal without modifying settings");
#else
    check(connectButton && !connectButton->isEnabled(), "unsupported platform does not promise system proxy apply");
#endif
    if (leaveButton) leaveButton->click();
    check(leaveButton && applyCalls <= 1 && dialog.result() == QDialog::Rejected, "leave unchanged closes without requesting apply");
    check(urlEdit && urlEdit->text().isEmpty(), "browser diagnostics never prefill private corporate URL");
    bool exposed = false;
    for (const auto *label : dialog.findChildren<QLabel *>()) exposed |= label->text().contains(QStringLiteral("private-user")) || label->text().contains(QStringLiteral("private-password"));
    check(!exposed, "proxy authentication values never appear in diagnostic labels");
    dialog.setProxyEndpoint(2080, {}, {}, false);
    check(connectButton && !connectButton->isEnabled(), "stopping profile removes browser apply availability");
    std::cout << checks << " checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}
