#include "main/ServiceHealth.hpp"
#include "ui/dialog_service_health.h"
#include "ServiceHealthFixture.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <cstdio>
using namespace GreenRhythm;
namespace {
int checks = 0, failures = 0;
void check(bool value, const char *message) { ++checks; if (!value) { ++failures; std::fprintf(stderr,"FAIL %s\n",message); } }
struct Server : QTcpServer {
    QList<int> statuses{200}; int requests = 0; bool silent = false;
    Server() {
        listen(QHostAddress::LocalHost, 0);
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    socket->readAll(); if (socket->property("answered").toBool()) return;
                    socket->setProperty("answered", true);
                    const int status = statuses[requests++ % statuses.size()];
                    if (silent) return;
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl url(const QString &scheme = "http") { return QUrl(scheme+"://127.0.0.1:"+QString::number(serverPort())+"/private-path?token=secret-query"); }
};
struct TlsServer : QTcpServer {
    QByteArray body = "fixture-private-subscription-body";
    QByteArray lastRequest;
    int status = 200;
    TlsServer() { listen(QHostAddress::LocalHost, 0); }
    QUrl url() const { return QUrl("https://127.0.0.1:"+QString::number(serverPort())+"/fixture-secret-subscription"); }
    void incomingConnection(qintptr descriptor) override {
        auto *socket = new QSslSocket(this);
        socket->setSocketDescriptor(descriptor);
        socket->setLocalCertificate(QSslCertificate(healthFixtureCertificate));
        socket->setPrivateKey(QSslKey(healthFixtureKey, QSsl::Rsa));
        QObject::connect(socket,&QSslSocket::readyRead,socket,[this,socket] {
            lastRequest = socket->readAll();
            if (socket->property("answered").toBool()) return;
            socket->setProperty("answered",true);
            socket->write("HTTP/1.1 "+QByteArray::number(status)+" Fixture\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
            socket->disconnectFromHost();
        });
        QObject::connect(socket,&QSslSocket::disconnected,socket,&QObject::deleteLater);
        socket->startServerEncryption();
    }
};
void run(ServiceHealth &health, const HealthContext &context, const QList<HealthTarget> &targets, int rounds=3, int cancel=0) {
    QEventLoop loop; int completed = 0;
    auto connection = QObject::connect(&health, &ServiceHealth::completed, &loop, [&] { ++completed; loop.quit(); });
    check(health.start(context,"discord",rounds,targets,2,1500),"start accepts bounded local fixture run");
    if (cancel) QTimer::singleShot(cancel,&health,&ServiceHealth::cancel);
    QTimer::singleShot(6000,&loop,&QEventLoop::quit); loop.exec();
    QObject::disconnect(connection);
    check(!health.isRunning() && completed==1,"run completes once and does not remain busy");
}
}
int main(int argc,char **argv) {
    qputenv("QT_QPA_PLATFORM","offscreen"); QApplication app(argc,argv);
    std::fprintf(stderr,"TLS backend: %s; available: %s\n",qPrintable(QSslSocket::activeBackend()),qPrintable(QSslSocket::availableBackends().join(',')));
    const auto catalog=healthTargets("discord");
    check(std::any_of(catalog.begin(),catalog.end(),[](const HealthTarget &target){return target.id=="ip_control" && !QHostAddress(target.url.host()).isNull() && target.url.scheme()=="https" && target.expectedStatus==302;}),
          "default diagnostics include a TLS-verified IP control independent of destination DNS");
    Server system, profile;
    check(system.isListening() && profile.isListening(),"owned HTTP endpoints listen on loopback");
    HealthContext context; context.version="fixture"; context.protocol="vless"; context.profileRunning=true; context.tunEnabled=true;
    context.profileProxy=QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",profile.serverPort(),"secret-user","secret-password");
    const QList<HealthTarget> targets{{"control","Control",system.url(),200,true}};
    ServiceHealth health;
    run(health,context,targets);
    auto report=health.report();
    check(report.value("samples").toArray().size()==6,"three rounds retain exactly two path samples each");
    check(report.value("independent_isp_baseline")==false && report.value("voice_verified")==false,
          "TUN comparisons never claim an independent ISP baseline or verified voice");
    check(health.summary().contains(QString::fromUtf8("Голосовое соединение не проверено")),"HTTPS success cannot claim Discord voice works");
    context.tunEnabled=false; context.systemProxyEnabled=true;
    run(health,context,targets,1);
    check(health.summary().contains(QString::fromUtf8("HTTP-прокси не перенаправляет голосовой UDP"))
        && health.report().value("system_proxy_enabled").toBool() && !health.report().value("tun_enabled").toBool(),
        "proxy-only success explains that Discord voice does not inherit an explicit control probe's UDP path");
    context.systemProxyEnabled=false;
    run(health,context,targets,1);
    check(health.details().contains(QString::fromUtf8("Другая программа или VPN")) && !health.report().value("voice_verified").toBool(),
        "manual proxy mode preserves uncertainty about other VPNs and real voice");
    context.tunEnabled=true;
    run(health,context,targets);
    report=health.report();
    health.recordDiscordObservation(3,0,2,false,QDateTime::currentDateTimeUtc(),QDateTime::currentDateTimeUtc());
    check(health.report().value("discord_observation").toObject().value("udp_via_profile")==2
        && health.report().value("voice_verified")==false,"observed Discord UDP is included in the report without claiming packet delivery");
    const auto safe=QJsonDocument(report).toJson();
    check(!safe.contains("secret-") && !safe.contains("127.0.0.1") && !safe.contains("private-path"),"report excludes URLs, private paths and proxy credentials");
    const auto firstId=report.value("report_id");
    profile.statuses={200,503,200}; profile.requests=0;
    run(health,context,targets);
    check(health.summary().contains(QString::fromUtf8("не каждый раз")),"intermittent endpoint failures are visible instead of a last-success verdict");
    check(health.report().value("report_id")!=firstId && health.report().value("samples").toArray().size()==6,"repeat run replaces samples and assigns a fresh report id");
    profile.statuses={403}; profile.requests=0;
    run(health,context,targets);
    check(health.summary().contains(QString::fromUtf8("не прошли")),"HTTP 403 never passes a known control expectation");
    profile.statuses={302}; run(health,context,targets,1);
    check(health.summary().contains(QString::fromUtf8("не прошли")),"redirect is not a successful internet test");
    profile.statuses={200}; profile.silent=true;
    run(health,context,targets,10,20);
    check(health.report().value("cancelled").toBool() && health.summary().contains(QString::fromUtf8("Неполные")),"cancellation preserves an explicitly incomplete result");
    profile.silent=false; context.profileRunning=false;
    run(health,context,targets,1);
    check(health.summary().contains(QString::fromUtf8("Профиль не подключён")),"stopped profile is not inferred healthy from direct requests");
    context.profileRunning=true; context.subscription=system.url("https");
    int validations=0; health.validateSubscription=[&](const QByteArray &){++validations;return 1;};
    run(health,context,targets,1);
    report=health.report();
    check(report.value("subscription_state")=="tls" && validations==0,"subscription TLS errors are not ignored or fed to the parser");
    check(!QJsonDocument(report).toJson().contains("secret-query"),"failed subscription report still never exposes its key");
    const auto savedTls = QSslConfiguration::defaultConfiguration();
    auto trustedTls = savedTls; trustedTls.addCaCertificate(QSslCertificate(healthFixtureCaCertificate));
    QSslConfiguration::setDefaultConfiguration(trustedTls);
    TlsServer subscription;
    context.subscription=subscription.url();
    context.subscriptionUserAgent="fixture-importer-user-agent";
    QByteArray parsed;
    health.validateSubscription=[&](const QByteArray &body) { parsed=body; return body==subscription.body ? 2 : 0; };
    run(health,context,targets,1);
    report=health.report();
    check(report.value("subscription_state")=="valid" && report.value("subscription_profiles")==2 && parsed==subscription.body,
          "verified TLS GET passes the complete bounded subscription to read-only validation");
    check(subscription.lastRequest.contains("User-Agent: fixture-importer-user-agent"),"subscription diagnosis requests the same format as the normal importer");
    check(!QJsonDocument(report).toJson().contains("fixture-private") && !health.details().contains("fixture-secret"),
          "successful subscription body and token never enter report or UI");
    health.validateSubscription=[](const QByteArray &) { return 0; };
    run(health,context,targets,1);
    check(health.report().value("subscription_state")=="invalid_content","HTTP 200 with unrecognized contents is not a valid subscription");
    subscription.status=403;
    run(health,context,targets,1);
    check(health.report().value("subscription_state")=="denied","subscription refusal is separate from profile HTTPS health");
    subscription.status=200; subscription.body=QByteArray(1024*1024+1,'x'); parsed.clear();
    health.validateSubscription=[&](const QByteArray &body) { parsed=body; return 1; };
    run(health,context,targets,1);
    check(health.report().value("subscription_state")=="too_large" && parsed.isEmpty(),"oversized subscription is aborted before parsing");
    QSslConfiguration::setDefaultConfiguration(savedTls);
    context.subscription=QUrl();
    DialogServiceHealth dialog(nullptr,targets);
    check(!dialog.health->isRunning(),"opening diagnostics starts no unsolicited checks");
    dialog.captureContext=[&]{return context;}; dialog.contextStillCurrent=[] { return false; };
    auto *start=dialog.findChild<QPushButton*>("grHealthStart"); auto *cancel=dialog.findChild<QPushButton*>("grHealthCancel");
    auto *copy=dialog.findChild<QPushButton*>("grHealthCopy"); auto *service=dialog.findChild<QComboBox*>("grHealthService");
    int observation=0; QObject::connect(&dialog,&DialogServiceHealth::observeDiscordRequested,[&]{++observation;});
    dialog.show(); service->setCurrentIndex(1); app.processEvents();
    auto *observe=dialog.findChild<QPushButton*>("grHealthObserveDiscord");
    check(observe->isVisible() && !observe->isEnabled() && !copy->isEnabled(),"Discord explains that observation needs a completed context snapshot");
    observe->click(); check(observation==0,"unrun context cannot produce an unattributed voice observation");
    profile.silent=true; start->click();
    check(!start->isEnabled() && cancel->isEnabled(),"real start button reflects an active run");
    QEventLoop staleLoop; QObject::connect(dialog.health,&ServiceHealth::completed,&staleLoop,&QEventLoop::quit);
    QTimer::singleShot(1500,&staleLoop,&QEventLoop::quit); staleLoop.exec();
    check(dialog.health->report().value("stale").toBool() && start->isEnabled() && copy->isEnabled(),
          "changing profile during a run cancels it and restores usable UI");
    copy->click(); check(QApplication::clipboard()->text().contains("\"stale\": true"),"copy exports the explicit stale report rather than a healthy verdict");
    dialog.contextStillCurrent=[] { return true; };
    profile.silent=false;
    run(*dialog.health,context,targets,1);
    observe->click(); check(observation==1,"completed current snapshot permits exactly one voice observation request");
    service->setCurrentIndex(0);
    check(!copy->isEnabled(),"changing the selected check cannot copy a report for another service as its result");
    service->setCurrentIndex(1);
    check(copy->isEnabled(),"returning to the completed check restores access to its report");
    std::printf("%d checks, %d failures\n",checks,failures); return failures ? 1 : 0;
}
