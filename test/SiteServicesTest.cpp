#include "main/SiteServices.hpp"
#include "ui/SiteServicesController.hpp"
#include "ui/SitesPage.hpp"
#include "ui/MainShell.hpp"
#include "main/AppFont.hpp"
#include "test/WidgetTestFonts.hpp"
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <cstdio>
using namespace GreenRhythm;
static int failures = 0, checks = 0;
static void check(bool ok, const char *text) {
    ++checks;
    failures += !ok;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", text);
}
static QByteArray readFile(const QString &path) {
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return file.readAll();
}
static void writeFile(const QString &path, const QByteArray &value) {
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(value);
}
static QString hash(const QString &value) { return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex()); }
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    registerWidgetTestFonts();
    applyAppFont(app);
    QString navigation, origin, error;
    check(SiteServices::url("https://EXAMPLE.com:443/a?token=do-not-save#secret", &navigation, &origin, &error) && navigation == "https://example.com/a?token=do-not-save#secret" && origin == "https://example.com", "URL parameters remain transient for SPA navigation; only the origin is saved");
    for (const auto &bad: {"file:///C:/Windows/", "http://127.0.0.1/", "http://10.44.0.1/", "https://u:p@example.com/", "http://169.254.169.254/", "http://host.local/", "http://[::1]/"}) check(!SiteServices::url(bad, nullptr, nullptr, &error), "reject internal, file and credential URLs");
    QTemporaryDir folder;
    const auto path = folder.filePath("settings.json"), routePath = folder.filePath("Default");
    QJsonObject original{{"site_routing", "legacy lists kept byte-for-byte"}, {"vpn_bypass_process", "cs2.exe"}, {"vpn_proxy_process", "Discord.exe"}, {"local_dns", "work"}, {"unknown_future", QJsonArray{1, 2}}};
    writeFile(path, QJsonDocument(original).toJson());
    writeFile(routePath, "unchanged work/private/RU routes");
    QString raw, liveHash = hash(SiteServices::encode(SiteServices::policy(SiteServices::empty()))), failure;
    QStringList actions;
    QJsonObject scanPayload;
    bool hold = false;
    MainShell shell;
    shell.adopt(new QWidget, new QWidget);
    auto *page = shell.sitesWidget();
    auto sample = [](int status) { return QJsonObject{{"requests", 1}, {"ok", status == 200 ? 1 : 0}, {"failed", status == 200 ? 0 : 1}, {"status", status}}; };
    const QJsonObject report{{"url", "https://www.example.com"}, {"title", "Owned example"}, {"engine", "chromium"}, {"direct_ok", false}, {"proxy_ok", true}, {"partial", false}, {"recommendation", "proxy"}, {"endpoints", QJsonArray{QJsonObject{{"origin", "https://www.example.com"}, {"direct", sample(403)}, {"proxy", sample(200)}}, QJsonObject{{"origin", "https://one.cloudfront.net"}, {"direct", sample(503)}, {"proxy", sample(200)}}}}};
    SiteServicesController controller(page, [&](const QString &action, const QJsonObject &payload, SiteServicesController::Done done) {actions<<action;
  if(action=="status")done({{"scope",QString(64,'a')},{"configuration_hash",liveHash},{"entries",QJsonObject{}},{"history",QJsonArray{}}},{});
  else if(action=="scan"){scanPayload=payload;done({{"id","owned-job"},{"scope",QString(64,'a')}},{});}
  else if(action=="scan_status")done({{"state",hold?"direct":"completed"},{"scope",QString(64,'a')},{"report",report}},{});
  else if(action=="apply"){if(failure.isEmpty())liveHash=hash(payload.value("library").toString());done({{"applied",failure.isEmpty()}},failure);}
  else done({},{}); }, [&] { return raw; }, [&](const QString &expected, const QJsonObject &value, QString *error) {if(!SiteServices::save(path,expected,value,error))return false;raw=SiteServices::encode(value);return true; }, [] { return true; }, [&](const QJsonObject &value) { shell.setSavedSites(value); });
    QObject::connect(&shell, &MainShell::siteModeRequested, &controller, [&](const QString &id, const QString &mode) { controller.chooseMode(id, mode); });
    check(shell.findChild<QPushButton *>("grNav4") && shell.findChild<QStackedWidget *>("grPages")->count() == 5, "Sites is a real left navigation page; old pages retained");
    page->findChild<QLineEdit *>("siteUrlInput")->setText("https://www.example.com/a?token=do-not-save");
    page->findChild<QPushButton *>("siteAnalyze")->click();
    check(scanPayload.value("url").toString().contains("?token=do-not-save") && !raw.contains("do-not-save") && !raw.contains("/a?"), "real Analyze control never persists URL query or navigation path");
    QJsonObject library;
    check(SiteServices::parse(raw, &library, &error) && library.value("sites").toArray().size() == 1, "anonymous dependency result automatically becomes a saved card");
    auto site = library.value("sites").toArray().first().toObject();
    const auto id = site.value("id").toString();
    check(site.value("endpoints").toArray().size() == 2 && site.value("mode") == "auto" && site.value("enabled").toBool(), "new service contains observed exact CDN endpoints in Auto");
    check(page->findChild<QComboBox *>("siteMode")->currentData() == "auto", "saved Auto mode is visibly selected in the actual dropdown");
    check(actions.contains("apply") && page->findChild<QLabel *>("siteFeedback")->text().contains(QString::fromUtf8("применено")), "new saved card applies through an isolated live policy RPC");
    auto *shortcut = shell.findChild<QPushButton *>("grSavedSiteOpen");
    check(shortcut && shortcut->property("siteId") == id, "saved service is immediately reachable from left sidebar");
    shortcut->click();
    check(shell.findChild<QStackedWidget *>("grPages")->currentIndex() == 4 && page->selectedID() == id, "left shortcut opens that card directly");
    auto *quick = shell.findChild<QPushButton *>("grSavedSiteMode");
    check(quick && quick->menu()->actions().size() == 3, "left sidebar exposes Auto/DIRECT/VPN quick choices");
    quick->menu()->actions().last()->trigger();
    SiteServices::parse(raw, &library, &error);
    site = library.value("sites").toArray().first().toObject();
    check(site.value("mode") == "proxy" && site.value("history").toArray().size() == 2, "one quick VPN action saves its history without per-domain edits");
    auto saved = QJsonDocument::fromJson(readFile(path)).object();
    saved.remove("site_services");
    check(saved == original && readFile(routePath) == "unchanged work/private/RU routes", "URL discovery and quick modes preserve legacy lists, games, work DNS and route bytes");
    check(!QDir(folder.filePath(".site-services-backups")).entryList(QDir::Files).isEmpty(), "each persisted change has a private original settings backup");
    for (const auto &action: actions) check(action == "status" || action == "scan" || action == "scan_status" || action == "apply" || action == "cancel", "controller never requests Stop, Start or a global route reset");
    failure = "profile_changed";
    controller.chooseMode(id, "direct");
    check(page->findChild<QLabel *>("siteFeedback")->text().contains(QString::fromUtf8("не применено")) && page->findChild<QLabel *>("siteLiveRoute")->text().contains(QString::fromUtf8("не применено")), "failed live apply is saved but visibly pending, never shown as active");
    failure.clear();
    const auto beforeCancel = raw;
    hold = true;
    controller.analyze("https://another.example.org/");
    controller.cancel();
    check(raw == beforeCancel && actions.last() == "cancel", "cancellation stops only the analysis and never saves a partial draft");
    const auto beforeStale = readFile(path);
    check(!SiteServices::save(path, "stale", library, &error) && readFile(path) == beforeStale, "stale bookmark editor cannot clobber current settings");
    for (int i = 0; i < 40; ++i) {
        site["mode"] = i % 2 ? "proxy" : "direct";
        SiteServices::history(site, "mode", "fixture");
    }
    check(site.value("history").toArray().size() == 30, "history is bounded and retained for quick reversal");
    {
        auto *queuedPage = new SitesPage;
        QString memory = raw, backendHash = hash(SiteServices::encode(SiteServices::policy(library)));
        QList<QPair<QJsonObject, SiteServicesController::Done>> waiting;
        SiteServicesController queued(queuedPage, [&](const QString &action, const QJsonObject &payload, SiteServicesController::Done done) {if(action=="status")done({{"scope","owned"},{"configuration_hash",backendHash}},{});else if(action=="apply")waiting.append({payload,std::move(done)});else done({},{}); }, [&] { return memory; }, [&](const QString &, const QJsonObject &next, QString *) {memory=SiteServices::encode(next);return true; }, [] { return true; }, {});
        queued.chooseMode(id, "direct");
        queued.chooseMode(id, "proxy");
        queued.chooseMode(id, "auto");
        check(waiting.size() == 1, "rapid mode changes serialize core policy replacement");
        auto acknowledge = [&] {const auto pending=waiting.takeFirst();backendHash=hash(pending.first.value("library").toString());pending.second({},{}); };
        acknowledge();
        check(waiting.size() == 1 && waiting.first().first.value("library").toString().contains("\"mode\":\"auto\""), "queued replacement always uses the latest user choice");
        acknowledge();
        check(waiting.isEmpty() && backendHash == hash(SiteServices::encode(SiteServices::policy(QJsonDocument::fromJson(memory.toUtf8()).object()))), "final runtime policy matches latest saved choice after delayed replies");
        delete queuedPage;
    }
    {
        SiteServicesController::Done late;
        auto *closing = new SitesPage;
        bool savedAfterClose = false;
        SiteServicesController pending(closing, [&](const QString &, const QJsonObject &, SiteServicesController::Done done) { late = std::move(done); }, [] { return QString(); }, [&](const QString &, const QJsonObject &, QString *) {savedAfterClose=true;return true; }, [] { return true; }, {});
        pending.analyze("https://close.example.org/");
        delete closing;
        late({{"scope", "owned"}}, {});
        check(!savedAfterClose, "late analysis callback is ignored after its page closes");
    }
    shell.showPage(4);
    shell.resize(900, 720);
    shell.show();
    app.processEvents();
    check(shell.width() <= 900 && shell.height() <= 720 && page->rect().contains(page->findChild<QPushButton *>("siteAnalyze")->geometry().bottomRight()), "URL input and actions remain reachable in a compact window");
    shell.hide();
    if (app.arguments().contains("--preview")) {
        QFile style("res/theme/feiyangqingyun/qss/modern.css");
        if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
        shell.showPage(4);
        shell.resize(1160, 820);
        shell.show();
        app.processEvents();
        shell.grab().save(app.arguments().last());
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
