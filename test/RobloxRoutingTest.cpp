#include "ui/RobloxRoutingDialog.hpp"
#include "main/AppFont.hpp"
#include "main/GameVoiceRouting.hpp"
#include "test/WidgetTestFonts.hpp"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstdio>

using namespace GreenRhythm;
static int failures=0,checks=0;
static void check(bool ok,const char *text) {++checks;failures+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",text);}
static QByteArray read(const QString &path) {QFile f(path);f.open(QIODevice::ReadOnly);return f.readAll();}
static void write(const QString &path,const QByteArray &bytes) {QFile f(path);f.open(QIODevice::WriteOnly);f.write(bytes);}
int main(int argc,char **argv) {
    qputenv("QT_QPA_PLATFORM","offscreen"); QApplication app(argc,argv);
    registerWidgetTestFonts();applyAppFont(app);
    const auto choices=GameAppRules::serialize({{"Discord.exe","proxy"},{"dota2.exe","bypass"}});
    const ProgramRouting::State original{"#keep\nRobloxStudioBeta.exe\nbrowser.exe","RobloxPlayerBeta.exe",choices,false};
    RobloxRoutingDialog dialog(original,true);
    if(app.arguments().contains("--preview")) {
        QFile style("res/theme/feiyangqingyun/qss/modern.css");
        if(style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
        auto *autoPlayer=dialog.findChild<QComboBox *>("robloxPlayerRoute");
        autoPlayer->setCurrentIndex(autoPlayer->findData("smart"));
        auto *vpnStudio=dialog.findChild<QComboBox *>("robloxStudioRoute");
        vpnStudio->setCurrentIndex(vpnStudio->findData("proxy"));
        dialog.show();app.processEvents();dialog.grab().save(app.arguments().last());return 0;
    }
    auto *player=dialog.findChild<QComboBox *>("robloxPlayerRoute"),*studio=dialog.findChild<QComboBox *>("robloxStudioRoute");
    dialog.show();app.processEvents();auto *footer=dialog.findChild<QPushButton *>("robloxRouteSave");
    check(dialog.rect().contains(QRect(footer->mapTo(&dialog,QPoint{}),footer->size())),"footer remains visible outside the scrollable content");
    check(player && studio && player->currentData()=="proxy" && studio->currentData()=="bypass","initial controls show actual legacy directions, not a misleading automatic choice");
    ProgramRouting::State saved; int commits=0,inspects=0;
    dialog.setCommitter([&](const ProgramRouting::State &state,QString *) {saved=state;++commits;return true;});
    dialog.setInspector([&](const QString &name) {if(name=="RobloxStudioBeta.exe") ++inspects;});
    studio->setCurrentIndex(studio->findData("auto"));
    dialog.findChild<QPushButton *>("robloxStudioRouteInspect")->click();
    check(inspects==1 && commits==0,"route inspection never saves the draft");
    dialog.findChild<QPushButton *>("robloxRouteSave")->click();
    check(commits==1 && !ProgramRouting::listed(saved.bypass,"RobloxStudioBeta.exe") && ProgramRouting::overrideMode(saved,"RobloxStudioBeta.exe")=="auto","site routing clears the legacy whole-Studio bypass");
    check(saved.proxy==original.proxy && saved.applications==original.applications && ProgramRouting::listed(saved.bypass,"browser.exe"),"Studio automatic choice leaves Player, browser, games and voice untouched");
    RobloxRoutingDialog manual(original,true); manual.setCommitter([&](const ProgramRouting::State &state,QString *) {saved=state;++commits;return true;});
    auto *p=manual.findChild<QComboBox *>("robloxPlayerRoute"),*s=manual.findChild<QComboBox *>("robloxStudioRoute");
    p->setCurrentIndex(p->findData("bypass"));s->setCurrentIndex(s->findData("proxy"));manual.accept();
    check(ProgramRouting::overrideMode(saved,"RobloxPlayerBeta.exe")=="bypass" && ProgramRouting::overrideMode(saved,"RobloxStudioBeta.exe")=="proxy","Player and Studio choose independent forced directions over legacy conflicts");
    QJsonObject apps; GameAppRules::parse(saved.applications,&apps);
    const auto routeRules=GameAppRules::rules(apps);
    bool direct=false,proxy=false;
    for(const auto &v:routeRules) {
        const auto r=v.toObject();
        if(r.value("process_name").toArray().contains("RobloxPlayerBeta.exe")) direct=r.value("outbound")=="bypass" && !r.contains("network");
        if(r.value("process_name").toArray().contains("RobloxStudioBeta.exe")) proxy=r.value("outbound")=="proxy" && !r.contains("network");
    }
    check(direct && proxy,"forced process routes cover both TCP and UDP without a global UDP rule");
    check(GameAppRules::mode(apps,"Discord.exe")=="proxy" && GameAppRules::mode(apps,"dota2.exe")=="bypass","forced Roblox choices preserve Discord VPN and Dota DIRECT");
    ProgramRouting::State smartPlayer,smart;QString smartError;
    check(ProgramRouting::choose(original,"RobloxPlayerBeta.exe","smart",&smartPlayer,&smartError)
        && ProgramRouting::choose(smartPlayer,"RobloxStudioBeta.exe","smart",&smart,&smartError),"both applications can enable automatic TCP/UDP split independently");
    check(ProgramRouting::overrideMode(smart,"RobloxPlayerBeta.exe")=="smart" && ProgramRouting::overrideMode(smart,"RobloxStudioBeta.exe")=="smart","display distinguishes automatic split from forced full VPN");
    QJsonObject fallback;QStringList split;
    check(GameAppRules::parse(smart.applications,&fallback) && GameAppRules::mode(fallback,"RobloxStudioBeta.exe")=="proxy"
        && RobloxRouting::parse(smart.robloxSplit,&split) && split.size()==2,"automatic mode retains a schema-1 full-VPN fallback understood by older clients");
    const auto udp=RobloxRouting::udpRule(RobloxRouting::active(split,fallback));
    QJsonArray splitRules{udp};
    for(const auto &v:GameAppRules::rules({{"RobloxPlayerBeta.exe","proxy"},{"RobloxStudioBeta.exe","proxy"}})) splitRules.append(v);
    QFile fixture(QString::fromUtf8(GR_ROBLOX_FIXTURE));
    check(fixture.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(fixture.readAll()).array()==splitRules,"generated split rules exactly match the real core TCP/UDP loopback fixture");
    const auto sniff=GameVoiceRouting::tunSniffRules(splitRules,true,RobloxRouting::processes(),false);
    check(sniff.size()==2 && sniff.first().toObject().value("sniffer").toArray()==QJsonArray{"dns"},"automatic UDP retains DNS detection without delaying gameplay for protocol discovery");
    check(GameVoiceRouting::tunSniffRules(splitRules,false,RobloxRouting::processes(),false).size()==1,"corporate DNS restrictions still disable sniff shortcuts");
    ProgramRouting::State allProxy;
    check(ProgramRouting::choose(smart,"RobloxStudioBeta.exe","proxy",&allProxy,&smartError)
        && ProgramRouting::overrideMode(allProxy,"RobloxStudioBeta.exe")=="proxy"
        && ProgramRouting::overrideMode(allProxy,"RobloxPlayerBeta.exe")=="smart","manual full VPN removes only Studio's UDP bypass marker");
    ProgramRouting::State site;
    check(ProgramRouting::restore(smart,"RobloxStudioBeta.exe",&site,&smartError)
        && ProgramRouting::overrideMode(site,"RobloxStudioBeta.exe")=="auto"
        && ProgramRouting::overrideMode(site,"RobloxPlayerBeta.exe")=="smart","site routing removes the selected automatic marker as well as all older overrides");
    for(const auto &invalid:{"*.exe","other.exe","RobloxPlayerBeta.exe\nROBLOXPLAYERBETA.EXE"})
        check(!RobloxRouting::parse(invalid,&split,&smartError),"automatic split never accepts wildcard, unrelated or duplicate programs");
    RobloxRoutingDialog smartEditor(original,true);
    smartEditor.setCommitter([&](const ProgramRouting::State &state,QString *) {saved=state;return true;});
    auto *smartChoice=smartEditor.findChild<QComboBox *>("robloxStudioRoute");
    smartChoice->setCurrentIndex(smartChoice->findData("smart"));smartEditor.accept();
    check(ProgramRouting::overrideMode(saved,"RobloxStudioBeta.exe")=="smart","the actual automatic combo selection persists a split choice");
    RobloxRoutingDialog noop(original,true); noop.setCommitter([&](const ProgramRouting::State &,QString *) {++commits;return true;});
    const int before=commits;noop.accept();check(commits==before,"Done without edits does not rewrite settings");
    RobloxRoutingDialog cancel(original,true);cancel.setCommitter([&](const ProgramRouting::State &,QString *){++commits;return true;});
    cancel.findChild<QComboBox *>("robloxStudioRoute")->setCurrentIndex(2);cancel.reject();check(commits==before,"Cancel never saves a route");
    RobloxRoutingDialog denied(original,true);denied.setCommitter([](const ProgramRouting::State &,QString *error){*error="fixture denied";return false;});
    denied.findChild<QComboBox *>("robloxStudioRoute")->setCurrentIndex(2);denied.show();denied.accept();
    check(denied.isVisible() && denied.findChild<QLabel *>("robloxRouteStatus")->text()=="fixture denied","failed atomic save stays open with the actual error");
    auto white=original;white.whitelist=true;RobloxRoutingDialog whitelist(white,true);
    ProgramRouting::State selected; QString error;
    check(ProgramRouting::choose(white,"RobloxStudioBeta.exe","bypass",&selected,&error) && !ProgramRouting::choose(white,"RobloxStudioBeta.exe","auto",&selected,&error),"whitelist supports explicit direction and refuses fake site routing");
    check(ProgramRouting::choose(white,"RobloxStudioBeta.exe","smart",&selected,&error),"automatic split also works as an explicit choice in whitelist TUN");
    auto damaged=original;damaged.applications="{broken";RobloxRoutingDialog broken(damaged,true);
    check(!broken.findChild<QPushButton *>("robloxRouteSave")->isEnabled(),"unrecognized rules cannot be overwritten by the Roblox editor");
    QTemporaryDir files;const auto path=files.filePath("settings.json"),route=files.filePath("Default");
    QJsonObject settings{{"vpn_bypass_process",original.bypass},{"vpn_proxy_process",original.proxy},{"game_app_rules",original.applications},{"vpn_rule_white",false},{"unknown",42},{"remote_dns","preserve"}};
    const auto bytes=QJsonDocument(settings).toJson();write(path,bytes);write(route,"user routes");
    check(ProgramRouting::saveState(path,original,smart,&error),"combined independent choices and automatic marker are committed atomically");
    const auto actual=QJsonDocument::fromJson(read(path)).object();
    check(actual.value("unknown")==42 && actual.value("remote_dns")=="preserve" && read(route)=="user routes","saving never changes DNS, unknown settings or route files");
    const auto backups=QDir(files.filePath(".program-routing-backups")).entryList({"before-*.json"},QDir::Files);
    check(backups.size()==1 && read(files.filePath(".program-routing-backups/"+backups.first()))==bytes,"exact pre-change backup is retained");
    const auto updated=read(path);
    check(!ProgramRouting::saveState(path,original,smart,&error) && read(path)==updated,"stale Roblox editor cannot overwrite later preferences");
    auto stale=smart;stale.robloxSplit.clear();
    check(!ProgramRouting::saveState(path,stale,smart,&error) && read(path)==updated,"stale detection includes split markers even when the full-VPN fallback is identical");
    check(!GreenRhythm::GameAppRules::setMode(&apps,"RobloxPlayerBeta.exe","unrecognized"),"unsupported automatic/fallback modes are never persisted as guessed routes");
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
