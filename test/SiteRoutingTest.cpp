#include "main/SiteRouting.hpp"
#include "main/RobloxSitePreset.hpp"
#include "ui/SiteRoutingDialog.hpp"
#include "main/AppFont.hpp"
#include "test/WidgetTestFonts.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QSet>
#include <cstdio>
using namespace GreenRhythm;
static int checks=0,failures=0;
static void check(bool ok,const char *message) { ++checks;failures+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",message); }
static QByteArray read(const QString &path) {QFile f(path);f.open(QIODevice::ReadOnly);return f.readAll();}
static void write(const QString &path,const QByteArray &bytes) {QFile f(path);f.open(QIODevice::WriteOnly);f.write(bytes);}
int main(int argc,char **argv) {
    qputenv("QT_QPA_PLATFORM","offscreen");QApplication app(argc,argv);registerWidgetTestFonts();applyAppFont(app);
    SiteRouting::State state;QString error,normalized;
    check(SiteRouting::parse({},&state,&error) && !state.enabled && state.entries.isEmpty(),"missing setting is disabled; no user is silently enrolled");
    check(SiteRouting::fromLists(true,robloxSitePreset()+robloxSitePreset(),{},{},&state,&error) && state.entries.size()==72,"one-click preset accepts and deduplicates the full supplied suffix/CDN catalog");
    QSet<QString> preset;for(const auto &entry:state.entries)preset.insert(entry.pattern);
    const auto fixture=QJsonDocument::fromJson(read(QStringLiteral(GR_SITE_FIXTURE))).array();
    for(const auto &name:fixture)check(preset.contains(name.toString()),"UI preset matches the core's tested domain fixture");
    check(SiteRouting::pattern("DOMAIN:RoBlOx.Com.",&normalized) && normalized=="domain:roblox.com","canonicalize case and trailing dot");
    check(SiteRouting::pattern(QString::fromUtf8("пример.рф"),&normalized) && normalized=="domain:xn--e1afmkfd.xn--p1ai","IDN is stored in ASCII for exact core parity");
    check(SiteRouting::pattern("domain:one.cloudfront.net",&normalized) && normalized=="full:one.cloudfront.net","shared CDN tenants are exact-only");
    for(const auto &bad:{"ru","co.uk","domain:cloudfront.net","full:amazonaws.com","https://example.com/","example.com:443","127.0.0.1","10.1.2.3","foo.local","foo.lan","a..example.com","-a.example.com","*.example.com","regexp:.*","foo.com/path","example.com@evil.com"})
        check(!SiteRouting::pattern(bad,&normalized),"reject broad providers, private addresses, URL syntax and unrecognized selectors");
    const auto good=SiteRouting::serialize(state);
    check(!SiteRouting::fromLists(true,"domain:example.com","domain:example.com",{},&state,&error) && SiteRouting::serialize(state)==good,"conflicting modes reject the entire draft without partial state");
    check(SiteRouting::fromLists(true,"domain:example.com","full:a.example.com","full:b.example.com",&state,&error),"manual per-host exceptions coexist with a parent auto suffix");
    const auto rules=SiteRouting::rules(state,"selected-profile",false);
    check(rules.size()==3 && rules.first().toObject().value("domain").toArray().first().toString().endsWith("example.com"),"specific host exceptions precede broader auto suffixes");
    const auto automatic=rules.last().toObject();
    check(automatic.value("outbound")=="site-auto" && automatic.value("network")=="tcp" && automatic.value("port").toArray()==QJsonArray{443},"availability selection captures only HTTPS, preserving other protocols");
    check(!rules.first().toObject().contains("network") && !rules.first().toObject().contains("port"),"manual host mode covers every protocol without a global UDP rule");
    check(SiteRouting::rules(state,"selected-profile",true).last().toObject().value("outbound")=="selected-profile","generic sing-box export has an explicit compatible VPN fallback");
    state.enabled=false;check(SiteRouting::rules(state,"proxy",false).isEmpty(),"disabled selection emits no route");
    auto invalid=QJsonDocument::fromJson(good.toUtf8()).object();invalid["schema"]=2;
    check(!SiteRouting::parse(QString::fromUtf8(QJsonDocument(invalid).toJson()),&state,&error),"unknown schema cannot be overwritten by an older editor");
    QString large;for(int i=0;i<513;++i)large+=QString("domain:h%1.example.com\n").arg(i);
    check(!SiteRouting::fromLists(true,large,{},{},&state,&error),"catalog size is bounded before generating rules");

    QTemporaryDir folder;const auto path=folder.filePath("settings.json");
    const QJsonObject original{{"vpn_bypass_process","cs2.exe"},{"vpn_proxy_process","Discord.exe"},{"roblox_auto_processes","RobloxStudioBeta.exe"},
        {"routing","Default"},{"local_dns","corporate"},{"unknown_future",QJsonArray{1,2,3}}};
    const auto bytes=QJsonDocument(original).toJson();write(path,bytes);
    check(SiteRouting::save(path,{},good,&error),"atomic save updates only the new setting");
    auto saved=QJsonDocument::fromJson(read(path)).object();saved.remove("site_routing");
    check(saved==original,"profiles, app routes, corporate DNS and future fields survive");
    const auto backups=QDir(folder.filePath(".site-routing-backups")).entryList(QDir::Files);
    check(backups.size()==1 && read(folder.filePath(".site-routing-backups/"+backups.first()))==bytes,"backup contains exact original bytes before save");
    const auto firstSave=read(path);check(!SiteRouting::save(path,{},good,&error) && read(path)==firstSave,"stale editor cannot clobber another save");
    write(path,"invalid-json");check(!SiteRouting::save(path,{},good,&error) && read(path)=="invalid-json","corrupt settings are never overwritten");

    SiteRoutingDialog dialog({});int commits=0;QString result;
    dialog.setCommitter([&](const QString &value,QString *){++commits;result=value;return true;});
    dialog.show();app.processEvents();auto *save=dialog.findChild<QPushButton *>("siteRoutingSave");
    check(dialog.rect().contains(QRect(save->mapTo(&dialog,QPoint{}),save->size())),"save footer stays visible outside scrolling content");
    check(!dialog.findChild<QCheckBox *>("siteRoutingEnabled")->isChecked(),"viewing the editor does not enable auto");
    dialog.findChild<QPushButton *>("siteRobloxPreset")->click();dialog.findChild<QPushButton *>("siteRobloxPreset")->click();
    check(commits==0 && dialog.findChild<QCheckBox *>("siteRoutingEnabled")->isChecked(),"preset changes only a draft until Save");
    save->click();check(commits==1 && SiteRouting::parse(result,&state,&error) && state.enabled && state.entries.size()==72,"actual Save button persists a deduplicated enabled preset");
    SiteRoutingDialog bad({});bad.setCommitter([&](const QString &,QString *){++commits;return true;});
    bad.findChild<QPlainTextEdit *>("siteAutoList")->setPlainText("not-a-domain");bad.accept();
    check(commits==1 && !bad.findChild<QLabel *>("siteRoutingStatus")->text().isEmpty(),"invalid pasted input stays open with an actionable error and no write");
    SiteRoutingDialog cancelled(good);cancelled.setCommitter([&](const QString &,QString *){++commits;return true;});cancelled.reject();
    check(commits==1,"Cancel leaves even an existing enabled set unchanged");
    SiteRoutingDialog bulk(good);bulk.setCommitter([&](const QString &text,QString *){++commits;result=text;return true;});
    bulk.findChild<QComboBox *>("siteBulkMode")->setCurrentIndex(2);bulk.findChild<QPushButton *>("siteBulkApply")->click();
    check(commits==1 && bulk.findChild<QPlainTextEdit *>("siteAutoList")->toPlainText().isEmpty(),"one bulk action moves the entire set to VPN only in the draft");
    bulk.accept();bool allProxy=SiteRouting::parse(result,&state,&error) && state.entries.size()==72;
    for(const auto &entry:state.entries)allProxy&=entry.mode=="proxy";
    check(commits==2 && allProxy,"whole-set manual VPN is saved without editing individual domains");
    SiteRoutingDialog future(QString::fromUtf8(QJsonDocument(invalid).toJson()));
    check(!future.findChild<QPushButton *>("siteRoutingSave")->isEnabled(),"unknown schema disables destructive save");
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
