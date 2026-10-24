#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "db/ConfigBuilder.hpp"
#include "fmt/includes.h"
#include "main/HTTPRequestHelper.hpp"
#include "main/RouteState.hpp"
#include "main/RuRoutingPolicy.hpp"
#include "main/GameVoiceRouting.hpp"
#include "main/HealthProfiles.hpp"
#include "sub/GroupUpdater.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <iostream>

namespace {
int failures = 0, checks = 0;
void check(bool ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
}
QString fixture(const QString &name = "fixture") {
    return QStringLiteral("vless://11111111-1111-4111-8111-111111111111@203.0.113.1:443?security=reality&type=tcp&flow=xtls-rprx-vision&sni=example.com&fp=chrome&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA&sid=0123456789abcdef#") + name;
}
QByteArray read(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "private runtime config written");
}
struct FixtureStore : JsonStore {
    QString known;
    int number = 0;
    QString nestedKnown;
    JsonStore nested;
    explicit FixtureStore(const QString &path) : JsonStore(path) {
        _add(new configItem("known", &known, itemType::string));
        _add(new configItem("number", &number, itemType::integer));
        _add(new configItem("nested", &nested, itemType::jsonStore));
        nested._add(new configItem("known", &nestedKnown, itemType::string));
    }
};
void testJsonStoreRoundTrip() {
    const auto path = QStringLiteral("groups/jsonstore-fixture.json");
    const QJsonObject future{{"object", QJsonObject{{"opaque", 42}}},
        {"array", QJsonArray{1, QStringLiteral("keep"), QJsonValue::Null}}};
    const QJsonObject nestedFuture{{"deep", QJsonArray{true, 3.5}}};
    const QJsonObject input{{"known", "before"}, {"number", 7}, {"future", future},
        {"nested", QJsonObject{{"known", "nested-before"}, {"future", nestedFuture}}}};
    write(path, QJsonDocument(input).toJson());
    FixtureStore store(path);
    check(store.Load() && store.known == "before", "actual JsonStore loads known and future settings from isolated file");
    store.known = "after"; store.nestedKnown = "nested-after";
    check(store.Save(), "actual JsonStore commits changed known values atomically");
    const auto saved = QJsonDocument::fromJson(read(path)).object();
    check(saved.value("known") == "after" && saved.value("future") == future,
          "load-edit-save keeps unknown object and array fields");
    check(saved.value("nested").toObject().value("known") == "nested-after"
        && saved.value("nested").toObject().value("future") == nestedFuture,
          "nested JsonStore preserves its own unknown fields");
    store.known.clear(); store.nestedKnown.clear();
    const auto emptyKnown = store.ToJson();
    check(!emptyKnown.contains("known") && !emptyKnown.value("nested").toObject().contains("known"),
          "known empty strings are removed instead of restored from old JSON");
    check(!store.ToJson({"future", "nested"}).contains("future") && !store.ToJson({"future", "nested"}).contains("nested"),
          "without suppresses unknown fields and registered nested values");
    store.FromJson(QJsonObject{{"known", "replacement"}, {"nested", QJsonObject{{"known", "fresh"}}}, {"new-future", QJsonArray{4}}});
    const auto replacement = store.ToJson();
    check(!replacement.contains("future") && replacement.value("new-future").isArray()
        && !replacement.value("nested").toObject().contains("future"),
          "repeated FromJson replaces unknown fields without retaining stale root or nested data");
    store.FromJson(QJsonObject{{"late-known", "old-value"}});
    QString lateKnown;
    store._add(new configItem("late-known", &lateKnown, itemType::string));
    check(!store.ToJson().contains("late-known"), "newly registered empty value supersedes a former unknown field");
    store.known = "durable";
    check(store.Save() && !store.Save(), "Save preserves legacy changed versus unchanged result after successful commit");
    const auto durableBytes = read(path);
    const auto durableLastSave = store.last_save_content;
    store.known = "retry-value";
#ifdef Q_OS_WIN
    const auto nativePath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "Windows fixture holds target against replacement without blocking reads");
    if (handle != INVALID_HANDLE_VALUE) {
        check(!store.Save() && read(path) == durableBytes && store.last_save_content == durableLastSave,
              "failed atomic replacement preserves existing bytes and committed-content marker");
        CloseHandle(handle);
        check(store.Save() && QJsonDocument::fromJson(read(path)).object().value("known") == "retry-value"
            && store.last_save_content != durableLastSave, "Save retries the pending change after releasing Windows lock");
    }
#endif
    const auto lastSuccessful = store.last_save_content;
    store.known = "cannot-write";
    store.fn = "nonexistent-parent/jsonstore.json";
    check(!store.Save() && store.last_save_content == lastSuccessful && !QFileInfo::exists(store.fn),
          "failed open never claims content was saved or creates a partial target");
    store.fn = path;
    check(store.Save(), "retry after failed open still commits a real pending change");
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir temp;
    check(temp.isValid() && QDir::setCurrent(temp.path()), "isolated store created");
    for (const auto &dir : {"groups", "profiles", "temp", "routes_box"}) QDir().mkdir(dir);
    MW_show_log = [](QString) {};
    MW_dialog_message = [](QString, QString) {};
    {
        JsonStore legacy;
        QString oldCIDRs;
        legacy._add(new configItem("vpn_route_exclude_extra", &oldCIDRs, itemType::string));
        const QJsonObject newerSettings{{"vpn_route_exclude_extra", "10.20.0.0/16\n"}, {"vpn_gateway_exclusions", "gateway.example.org"}};
        legacy.FromJson(newerSettings);
        check(oldCIDRs == "10.20.0.0/16\n", "a CIDR-only settings reader never receives new gateway DNS syntax");
        const auto retained = legacy.ToJson();
        check(retained.value("vpn_gateway_exclusions") == newerSettings.value("vpn_gateway_exclusions")
              && retained.value("vpn_route_exclude_extra") == newerSettings.value("vpn_route_exclude_extra"),
              "older settings serialization preserves the separate unknown gateway preference");
    }
    NekoGui::dataStore->current_group = 0;
    NekoGui::dataStore->routing = std::make_unique<NekoGui::Routing>();
    NekoGui::profileManager->LoadManager();

    // Exercise the same complete snapshot and replacement helpers used by the
    // route dialog, with the real Routing constructor/JsonStore implementation.
    const QJsonObject savedRoute{{"def_outbound", "proxy"}, {"future", QJsonObject{{"keep", 7}}},
        {"proxy_domain", "full:old-proxy.example"}, {"custom", "{\"rules\":[{\"process_name\":\"old.exe\",\"outbound\":\"proxy\"}]}"},
        {"local_dns_domains", "domain:office.example"}, {"local_dns_server", "10.60.0.53"},
        {"local_dns_hosts", "git.office.example=10.60.1.10"}};
    NekoGui::Routing cleared(0);
    cleared.custom.clear();
    const auto mergedClears = GreenRhythm::RouteState::mergeEditor(savedRoute, cleared);
    for (const auto &field : {"proxy_domain", "custom", "local_dns_domains", "local_dns_server", "local_dns_hosts"})
        check(mergedClears.contains(field) && mergedClears.value(field).isString() && mergedClears.value(field).toString().isEmpty(),
              "route-editor clearing remains an explicit empty saved value");
    check(mergedClears.value("future") == savedRoute.value("future"), "route-editor merging retains unknown saved fields");
    // Use the same preview entry point as the real dialog, with the actual
    // factory preset. Its JsonStore serialization omits empty string defaults.
    NekoGui::Routing standard(1);
    auto customized = GreenRhythm::RouteState::completeSnapshot(standard);
    for (const auto &field : {"proxy_domain", "proxy_ip", "block_ip", "dns_object"})
        customized.insert(field, QStringLiteral("user-choice"));
    customized.insert("future", savedRoute.value("future"));
    customized.insert("local_dns_server", QStringLiteral("10.60.0.53"));
    const auto fullReset = GreenRhythm::RouteState::defaultsPreview(customized, standard,
        GreenRhythm::RouteDefaultsPolicy::Mode::ResetRoutes);
    for (const auto &field : {"proxy_domain", "proxy_ip", "block_ip", "dns_object"})
        check(fullReset.changedFields.contains(field) && fullReset.after.value(field).isString()
              && fullReset.after.value(field).toString().isEmpty(),
              "real preset reset clears custom values whose defaults serialize as absent");
    check(fullReset.after.value("local_dns_server") == customized.value("local_dns_server")
          && fullReset.after.value("future") == customized.value("future"),
          "full preset reset preserves local work DNS and unknown settings");
    const auto alreadyStandard = GreenRhythm::RouteState::completeSnapshot(standard);
    check(GreenRhythm::RouteState::defaultsPreview(alreadyStandard, standard,
          GreenRhythm::RouteDefaultsPolicy::Mode::ResetRoutes).changedFields.isEmpty(),
          "complete factory preset produces no spurious reset changes");
    const auto safeDefaults = GreenRhythm::RouteState::defaultsPreview(customized, standard,
        GreenRhythm::RouteDefaultsPolicy::Mode::SafeUpgrade);
    check(safeDefaults.after.value("proxy_domain") == customized.value("proxy_domain")
          && safeDefaults.after.value("proxy_ip") == customized.value("proxy_ip")
          && safeDefaults.after.value("block_ip") == customized.value("block_ip"),
          "safe defaults preview still preserves explicit user lists");
    auto restoredMemory = std::make_unique<NekoGui::Routing>(1);
    restoredMemory->FromJson(savedRoute);
    const QJsonObject absentFields{{"def_outbound", "proxy"}, {"future", QJsonObject{{"keep", 7}}}};
    GreenRhythm::RouteState::installSaved(restoredMemory, absentFields, "routes_box/Restored");
    check(restoredMemory->proxy_domain.isEmpty() && restoredMemory->custom == "{\"rules\": []}"
          && restoredMemory->direct_domain.isEmpty() && restoredMemory->local_dns_server.isEmpty()
          && restoredMemory->local_dns_domains.isEmpty() && restoredMemory->local_dns_hosts.isEmpty(),
          "restore starts from empty Routing schema and never retains absent old rules or scoped DNS");
    check(restoredMemory->fn == "routes_box/Restored", "replacement retains the committed scheme path");
    GreenRhythm::RouteState::installSaved(restoredMemory, {}, "routes_box/EmptyRestored");
    check(restoredMemory->direct_domain.isEmpty() && restoredMemory->proxy_domain.isEmpty()
          && restoredMemory->custom == "{\"rules\": []}", "valid empty snapshot also resets all prior route state");
    GreenRhythm::RouteState::installSaved(restoredMemory, mergedClears, "routes_box/Cleared");
    check(restoredMemory->custom.isEmpty() && restoredMemory->proxy_domain.isEmpty()
          && restoredMemory->local_dns_server.isEmpty(), "explicit empty fields are also empty in active memory");
    const auto existingPath = QStringLiteral("routes_box/ExistingMissingFields");
    write(existingPath, QJsonDocument(absentFields).toJson());
    auto afterRestart = GreenRhythm::RouteState::initialForPath(existingPath);
    check(afterRestart->Load() && afterRestart->custom == "{\"rules\": []}"
          && afterRestart->direct_domain.isEmpty() && afterRestart->proxy_domain.isEmpty(),
          "startup of existing scheme does not reinsert omitted bundled rules");
    auto freshScheme = GreenRhythm::RouteState::initialForPath("routes_box/NeverCreated");
    check(freshScheme->direct_domain.contains("domain:ru") && freshScheme->custom.contains("Squad")
          && freshScheme->dns_final_out == "proxy", "new scheme still starts with the current bundled preset");

    testJsonStoreRoundTrip();

    auto plain = NekoGui::ProfileManager::NewProxyEntity("vless");
    check(plain->TrojanVLESSBean()->TryParseLink(fixture()), "plain REALITY link parses");
    check(plain->TrojanVLESSBean()->flow == "xtls-rprx-vision", "Vision flow survives import");
    auto encoded = NekoGui::ProfileManager::NewProxyEntity("vless");
    check(encoded->TrojanVLESSBean()->TryParseLink(fixture() .replace("#fixture", "&spx=%2Fwatch%3Fa%3D1%26b%3D2#fixture")), "encoded query link parses");
    check(encoded->TrojanVLESSBean()->stream->reality_spx == "/watch?a=1&b=2", "encoded ampersand is not a query separator");
    auto quantum = NekoGui::ProfileManager::NewProxyEntity("vless");
    const QString pqv = QString(2603, 'A');
    check(quantum->TrojanVLESSBean()->TryParseLink(fixture().replace("#fixture", "&pqv=" + pqv + "#fixture")), "post-quantum REALITY link parses");
    check(quantum->TrojanVLESSBean()->stream->ToJson().value("pqv").toString() == pqv, "post-quantum verifier is stored");
    check(QUrlQuery(QUrl(quantum->bean->ToShareLink())).queryItemValue("pqv") == pqv, "post-quantum verifier survives export");
    check(quantum->bean->NeedExternal(true) == 0, "post-quantum verifier uses the native core");
    auto quantumOutbound = quantum->bean->BuildCoreObjSingBox();
    check(quantumOutbound.outbound.value("tls").toObject().value("reality").toObject()
              .value("mldsa65_verify").toString() == pqv,
          "native core config retains the post-quantum verifier");

    const auto profilesBeforeAudit = NekoGui::profileManager->profiles.size();
    const auto filesBeforeAudit = QDir("profiles").entryList(QDir::Files);
    NekoGui_sub::RawUpdater audit; audit.collect_only = true;
    audit.update(QString::fromUtf8((fixture("audit-a") + "\n" + fixture("audit-b")).toUtf8().toBase64()));
    check(audit.updated_order.size() == 2 && NekoGui::profileManager->profiles.size() == profilesBeforeAudit
          && QDir("profiles").entryList(QDir::Files) == filesBeforeAudit,
          "diagnostic subscription validation parses real profiles without changing the store or profile files");

    const auto beanBefore = plain->bean->ToJson();
    const auto trafficBefore = plain->traffic_data->ToJson();
    auto currentCandidate = GreenRhythm::healthCandidate(plain);
    check(!currentCandidate.outbound.isEmpty() && plain->bean->ToJson() == beanBefore
          && plain->traffic_data->ToJson() == trafficBefore, "health candidate clones the native builder without mutating active bean or traffic tags");
    const auto freshA = fixture("renamed-current");
    const auto freshB = fixture("new-b").replace("203.0.113.1", "203.0.113.2");
    const auto freshC = fixture("new-c").replace("203.0.113.1", "203.0.113.3");
    const auto freshD = fixture("new-d").replace("203.0.113.1", "203.0.113.4");
    auto comparison = GreenRhythm::healthSubscription((freshA + '\n' + freshB + '\n' + freshB + '\n' + freshC + '\n' + freshD).toUtf8().toBase64(), currentCandidate);
    check(comparison.count == 5 && comparison.currentPresence == "present" && comparison.alternatives.size() == 2,
          "fresh snapshot deduplicates current and repeated profiles and caps alternatives at two");
    check(comparison.alternatives[0].outbound.value("server") == "203.0.113.2"
          && comparison.alternatives[1].outbound.value("server") == "203.0.113.3", "only fresh subscription entries become alternatives in subscription order");
    comparison = GreenRhythm::healthSubscription(freshB.toUtf8(), currentCandidate);
    check(comparison.currentPresence == "absent" && comparison.alternatives.size() == 1, "deleted current profile is explicitly absent and not inherited as a fresh alternative");
    const auto rotated = freshA; auto rotatedEntity = NekoGui::ProfileManager::NewProxyEntity("vless");
    rotatedEntity->TrojanVLESSBean()->TryParseLink(rotated);
    rotatedEntity->TrojanVLESSBean()->password = "22222222-2222-4222-8222-222222222222";
    comparison = GreenRhythm::healthSubscription(rotatedEntity->bean->ToShareLink().toUtf8(), currentCandidate);
    check(comparison.currentPresence == "absent" && comparison.alternatives.size() == 1, "rotated credentials at the same endpoint remain a distinct fresh candidate");
    check(GreenRhythm::healthCandidate(plain, true).unavailable == "front_proxy", "group front-proxy topology is reported unsupported rather than silently bypassed as the same profile");
    plain->bean->custom_config = "{\"inbounds\":[]}";
    check(GreenRhythm::healthCandidate(plain).outbound.isEmpty(), "arbitrary custom config cannot inject TUN or hooks into diagnostics");
    plain->bean->custom_config.clear();
    comparison = GreenRhythm::healthSubscription("<html>not a subscription</html>", currentCandidate);
    check(comparison.count == 0 && comparison.alternatives.isEmpty(), "malformed fresh response never falls back to cached profiles");
    check(NekoGui::profileManager->profiles.size() == profilesBeforeAudit && QDir("profiles").entryList(QDir::Files) == filesBeforeAudit,
          "comparison of real subscription profiles preserves the profile store and files");

    NekoGui_sub::RawUpdater raw;
    raw.update(QString::fromUtf8((fixture("base64-a") + "\n" + fixture("base64-b")).toUtf8().toBase64()));
    check(raw.updated_order.size() == 2, "base64 subscription imports both profiles");
    NekoGui_sub::RawUpdater bom;
    bom.update(QString(QChar(0xfeff)) + fixture("bom"));
    check(bom.updated_order.size() == 1, "UTF-8 BOM does not make import silently empty");

    QTcpServer http;
    check(http.listen(QHostAddress::LocalHost), "local subscription fixture listens");
    QByteArray body = fixture("subscription").toUtf8().toBase64();
    int status = 200;
    QObject::connect(&http, &QTcpServer::newConnection, &http, [&] {
        while (auto *s = http.nextPendingConnection()) {
            QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            auto respond = [&, s] {
                if (!s->bytesAvailable()) return;
                s->readAll();
                s->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: text/plain\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            };
            QObject::connect(s, &QTcpSocket::readyRead, s, respond);
            if (s->bytesAvailable()) respond();
        }
    });
    const auto url = QStringLiteral("http://127.0.0.1:%1/sub").arg(http.serverPort());
    NekoGui_sub::groupUpdater->Update(QString(QChar(0xfeff)) + url, -1, true);
    auto group = NekoGui::profileManager->CurrentGroup();
    check(group->url == url, "import stores a subscription URL without a clipboard BOM");
    auto profiles = group->Profiles();
    check(profiles.size() >= 1, "downloaded subscription imports profiles");
    int firstId = -1;
    for (const auto &p : profiles) if (p->bean->name == "subscription") firstId = p->id;
    check(firstId >= 0, "downloaded profile is present");
    NekoGui_sub::groupUpdater->Update(url, group->id, true);
    check(NekoGui::profileManager->GetProfile(firstId) != nullptr, "first unchanged profile keeps identity on refresh");
    const auto count = group->Profiles().size();
    group->sub_last_update = 123;
    const auto stamp = group->sub_last_update;
    body = "<html>upstream maintenance</html>";
    NekoGui::dataStore->sub_clear = true;
    NekoGui_sub::groupUpdater->Update(url, group->id, true);
    check(group->Profiles().size() == count, "invalid HTTP 200 preserves profiles even with clear mode");
    check(group->sub_last_update == stamp, "invalid response is not a successful refresh");
    status = 503;
    NekoGui_sub::groupUpdater->Update(url, group->id, true);
    check(group->Profiles().size() == count, "HTTP failure preserves profiles");

    auto originalRouting = std::move(NekoGui::dataStore->routing);
    NekoGui::dataStore->routing = std::make_unique<NekoGui::Routing>(1);
    check(NekoGui::dataStore->routing->dns_final_out == "proxy", "new RU preset resolves unmatched VPN names through the VPN DNS");
    NekoGui::Routing::MigrateOne(NekoGui::dataStore->routing.get());
    check(NekoGui::dataStore->routing->dns_final_out == "proxy", "legacy QUIC migration retains the DNS final choice");
    const auto defaultConfig = NekoGui::BuildConfig(NekoGui::profileManager->GetProfile(firstId), false, false);
    const auto defaultServers = defaultConfig->coreConfig.value("dns").toObject().value("servers").toArray();
    check(!defaultServers.isEmpty() && defaultServers.first().toObject().value("tag") == "dns-remote", "actual config chooses remote DNS as its unmatched default");
    bool directDoH = false, remoteDetour = false;
    for (const auto &entry : defaultServers) {
        const auto server = entry.toObject();
        if (server.value("tag") == "dns-direct") directDoH = server.value("address") == "https://1.1.1.1/dns-query" && server.value("detour") == "direct";
        if (server.value("tag") == "dns-remote") remoteDetour = server.value("detour") == "proxy";
    }
    check(directDoH && remoteDetour, "direct DNS uses HTTPS while VPN DNS keeps its VPN detour");

    const auto runtimeRules = [](const std::shared_ptr<NekoGui::BuildConfigResult> &built) {
        return built->coreConfig.value("route").toObject().value("rules").toArray();
    };
    const auto ruleIndex = [](const QJsonArray &rules, const QJsonObject &expected) {
        for (int i = 0; i < rules.size(); ++i) if (rules[i].toObject() == expected) return i;
        return -1;
    };
    const auto ruIndex = [](const QJsonArray &rules) {
        for (int i = 0; i < rules.size(); ++i) {
            const auto rule = rules[i].toObject();
            if (rule.value("outbound") == "bypass" && rule.value("geoip").toArray().contains("ru")) return i;
        }
        return -1;
    };
    const auto buildCurrent = [&]() { return NekoGui::BuildConfig(NekoGui::profileManager->GetProfile(firstId), false, false); };
    const auto standardProcessProxy = GreenRhythm::RuRoutingPolicy::standardProcessProxyRule();
    const auto originalChoice = NekoGui::dataStore->routing_defaults_choice;
    NekoGui::dataStore->routing_defaults_choice.clear();
    const auto defaultRules = runtimeRules(buildCurrent());
    check(ruIndex(defaultRules) >= 0 && ruleIndex(defaultRules, standardProcessProxy) > ruIndex(defaultRules),
          "actual RU preset sends Russian destinations before the shipped Codex and Telegram proxy fallback");
    const auto factoryCustom = NekoGui::dataStore->routing->custom;
    for (const auto &choice : {QStringLiteral("keep"), QStringLiteral("restored")}) {
        NekoGui::dataStore->routing_defaults_choice = choice;
        const auto keptRules = runtimeRules(buildCurrent());
        check(ruleIndex(keptRules, standardProcessProxy) >= 0
              && ruleIndex(keptRules, standardProcessProxy) < ruIndex(keptRules),
              "actual builder honors keep and restored route choices without reordering factory-looking rules");
    }
    NekoGui::dataStore->routing_defaults_choice.clear();
    auto customDocument = QJsonDocument::fromJson(factoryCustom.toUtf8()).object();
    auto customRules = customDocument.value("rules").toArray();
    auto qualified = standardProcessProxy;
    qualified.insert("network", "tcp");
    for (int i = 0; i < customRules.size(); ++i)
        if (customRules[i].toObject() == standardProcessProxy) customRules.replace(i, qualified);
    const QJsonObject explicitBlock{{"ip_cidr", QJsonArray{"192.0.2.0/24"}}, {"outbound", "block"}};
    customRules.append(explicitBlock);
    customDocument.insert("rules", customRules);
    NekoGui::dataStore->routing->custom = QString::fromUtf8(QJsonDocument(customDocument).toJson(QJsonDocument::Compact));
    const auto qualifiedRules = runtimeRules(buildCurrent());
    check(ruleIndex(qualifiedRules, qualified) >= 0 && ruleIndex(qualifiedRules, qualified) < ruIndex(qualifiedRules),
          "actual builder preserves edited custom process VPN exceptions ahead of Russian addresses");
    check(ruleIndex(qualifiedRules, explicitBlock) >= 0 && ruleIndex(qualifiedRules, explicitBlock) < ruIndex(qualifiedRules),
          "actual builder preserves custom block exceptions ahead of Russian addresses");
    NekoGui::dataStore->routing->custom = factoryCustom;
    const auto previousTun = NekoGui::dataStore->vpn_internal_tun;
    const auto previousVpn = NekoGui::dataStore->spmode_vpn;
    const auto previousProxyProcesses = NekoGui::dataStore->vpn_rule_process_proxy;
    NekoGui::dataStore->vpn_internal_tun = true;
    NekoGui::dataStore->spmode_vpn = true;
    NekoGui::dataStore->vpn_rule_process_proxy = "Telegram.exe\nCodex.exe";
    const QJsonObject explicitProcesses{{"outbound", "proxy"}, {"process_name", QJsonArray{"Telegram.exe", "Codex.exe"}}};
    const auto selectedRules = runtimeRules(buildCurrent());
#ifdef Q_OS_MACOS
    // macOS always uses the privileged external TUN helper. A persisted true
    // preference cannot opt its ordinary core into internal-TUN-only rules.
    check(!NekoGui::PlatformSupportsInternalTun() && !NekoGui::UseInternalTun()
          && ruleIndex(selectedRules, explicitProcesses) < 0 && ruIndex(selectedRules) >= 0
          && ruleIndex(selectedRules, standardProcessProxy) > ruIndex(selectedRules),
          "macOS excludes the internal TUN process list while preserving the RU default and application fallback");
#else
    check(ruIndex(selectedRules) >= 0 && ruleIndex(selectedRules, explicitProcesses) >= 0
          && ruleIndex(selectedRules, explicitProcesses) < ruIndex(selectedRules),
          "actual user process VPN list retains priority over the RU default");
#endif
    check(NekoGui::dataStore->vpn_internal_tun
          && NekoGui::dataStore->vpn_rule_process_proxy == QStringLiteral("Telegram.exe\nCodex.exe"),
          "actual builder preserves saved TUN preference and process selection on every platform");
    NekoGui::dataStore->vpn_internal_tun = false;
    const auto externalRules = runtimeRules(buildCurrent());
    check(ruleIndex(externalRules, explicitProcesses) < 0 && ruIndex(externalRules) >= 0
          && ruleIndex(externalRules, standardProcessProxy) > ruIndex(externalRules),
          "external TUN mode excludes internal-only process settings without losing RU and application fallback rules");
#ifdef Q_OS_WIN
    {
        const auto previousExclusions = NekoGui::dataStore->vpn_route_exclude_extra;
        const auto previousGateways = NekoGui::dataStore->vpn_gateway_exclusions;
        const auto previousInternal = NekoGui::dataStore->vpn_internal_tun;
        const auto beforeScheme = GreenRhythm::RouteState::completeSnapshot(*NekoGui::dataStore->routing);
        NekoGui::dataStore->vpn_internal_tun = true;
        NekoGui::dataStore->vpn_route_exclude_extra = "10.20.0.0/16";
        NekoGui::dataStore->vpn_gateway_exclusions = "mks-racceess.rtk-it.ru\n212.164.137.29";
        check(NekoGui::dataStore->ToJson().value("vpn_gateway_exclusions").toString() == NekoGui::dataStore->vpn_gateway_exclusions,
              "normal settings save registers and retains the gateway preference independently of old CIDRs");
        const auto gatewayBuild = buildCurrent();
        QJsonArray excluded;
        for (const auto &value : gatewayBuild->coreConfig.value("inbounds").toArray())
            if (value.toObject().value("type") == "tun") excluded = value.toObject().value("route_exclude_address").toArray();
        // This serialization fixture deliberately has no packaged geo databases.
        // Compare against the existing baseline and check the generated routes
        // independently, rather than mistaking that fixture limitation for a
        // gateway build error.
        check(gatewayBuild->error == defaultConfig->error, "gateway policy introduces no error beyond the known geo fixture baseline");
        check(excluded.contains("10.20.0.0/16") && excluded.contains("mks-racceess.rtk-it.ru")
              && excluded.contains("212.164.137.29/32") && excluded.contains("192.168.0.0/16"),
              "real builder puts saved gateway names and IPs into OS exclusions while retaining existing LAN ranges");
        check(GreenRhythm::RouteState::completeSnapshot(*NekoGui::dataStore->routing) == beforeScheme
              && NekoGui::dataStore->vpn_gateway_exclusions.endsWith("212.164.137.29")
              && NekoGui::dataStore->vpn_route_exclude_extra == "10.20.0.0/16",
              "building a gateway exclusion never rewrites the saved scheme or preference");
        NekoGui::dataStore->vpn_gateway_exclusions = "*.invalid";
        check(!buildCurrent()->error.isEmpty(), "invalid gateway entry is rejected before starting any core");
        NekoGui::dataStore->vpn_route_exclude_extra = previousExclusions;
        NekoGui::dataStore->vpn_gateway_exclusions = previousGateways;
        NekoGui::dataStore->vpn_internal_tun = previousInternal;
    }
#endif
#ifdef Q_OS_WIN
    {
        const auto beforeRoute = GreenRhythm::RouteState::completeSnapshot(*NekoGui::dataStore->routing);
        const auto previousBypass = NekoGui::dataStore->vpn_rule_process;
        const auto previousGames = NekoGui::dataStore->games_via_tunnel;
        const auto previousWhite = NekoGui::dataStore->vpn_rule_white;
        NekoGui::dataStore->vpn_internal_tun = true;
        NekoGui::dataStore->vpn_rule_process.clear();
        NekoGui::dataStore->vpn_rule_process_proxy.clear();
        NekoGui::dataStore->vpn_rule_white = false;
        const auto originalGlobals = NekoGui::dataStore->custom_route_global;
        NekoGui::dataStore->custom_route_global.clear();
        const auto routeFor = [](const QJsonArray &rules, const QString &process) {
            for (const auto &value : rules) {
                const auto rule = value.toObject();
                if (rule.contains("outbound") && rule.value("process_name").toArray().contains(process)) return rule.value("outbound").toString();
            }
            return QString();
        };
        for (bool games : {false, true}) {
            NekoGui::dataStore->games_via_tunnel = games;
            const auto built = buildCurrent();
            const auto rules = runtimeRules(built);
            bool legacyTunSniff = false;
            for (const auto &value : built->coreConfig.value("inbounds").toArray()) {
                const auto inbound = value.toObject();
                if (inbound.value("tag") == "tun-in") legacyTunSniff |= inbound.value("sniff").toBool();
            }
            check(!legacyTunSniff && std::any_of(rules.begin(), rules.end(), [](const QJsonValue &value) {
                const auto rule = value.toObject();
                return rule.value("action") == "sniff" && rule.value("inbound").toArray().contains("tun-in");
            }), "actual TUN config scopes sniffing without a legacy global action");
            // The serialization fixture has no packaged geo databases. A new
            // process policy must not introduce an error beyond that baseline.
            check(built->error == defaultConfig->error, "game/voice policy introduces no additional builder errors");
            const auto guards = GreenRhythm::GameVoiceRouting::defaults(
                QJsonDocument::fromJson(factoryCustom.toUtf8()).object().value("rules").toArray(),
                QJsonDocument::fromJson(factoryCustom.toUtf8()).object().value("rules").toArray(), {});
            for (const auto &guard : guards)
                check(ruleIndex(rules, guard.toObject()) >= 0 && ruleIndex(rules, guard.toObject()) < ruIndex(rules),
                      "actual game and voice guards precede broad country rules");
            int udpBlock = -1;
            for (int i = 0; i < rules.size(); ++i)
                if (rules[i].toObject().value("outbound") == "block" && rules[i].toObject().value("port").toInt() == 443) { udpBlock = i; break; }
            check(udpBlock >= 0 && ruleIndex(rules, guards.last().toObject()) < udpBlock,
                  "Discord UDP/443 reaches the VPN before the general QUIC restriction");
            for (const auto &name : GreenRhythm::GameVoiceRouting::directProcesses())
                check(routeFor(rules, name) == "bypass", "actual internal TUN keeps CS2 and Steam direct with games on/off");
            for (const auto &name : GreenRhythm::GameVoiceRouting::voiceProcesses())
                check(routeFor(rules, name) == "proxy", "actual internal TUN forces Discord voice before country and QUIC rules");
        }
        // Exercise a profile saved before CS2 was added to the preset.
        auto legacy = QJsonDocument::fromJson(factoryCustom.toUtf8()).object();
        auto legacyRules = legacy.value("rules").toArray();
        auto oldGames = legacyRules.first().toObject();
        auto oldNames = oldGames.value("process_name").toArray();
        QJsonArray filtered;
        for (const auto &name : oldNames) if (name != "cs2.exe" && name != "csgo.exe") filtered.append(name);
        oldGames["process_name"] = filtered; legacyRules[0] = oldGames;
        legacy["rules"] = legacyRules;
        NekoGui::dataStore->routing->custom = QString::fromUtf8(QJsonDocument(legacy).toJson(QJsonDocument::Compact));
        const auto savedLegacy = NekoGui::dataStore->routing->custom;
        check(routeFor(runtimeRules(buildCurrent()), "cs2.exe") == "bypass"
              && NekoGui::dataStore->routing->custom == savedLegacy, "existing schemes acquire CS2 policy without a migration or route rewrite");
        NekoGui::dataStore->vpn_rule_process = "Discord.exe";
        NekoGui::dataStore->vpn_rule_process_proxy = "cs2.exe";
        const auto explicitRules = runtimeRules(buildCurrent());
        check(routeFor(explicitRules, "Discord.exe") == "bypass" && routeFor(explicitRules, "cs2.exe") == "proxy",
              "explicit per-app user choices beat game and voice defaults");
        NekoGui::dataStore->routing->custom = factoryCustom;
        NekoGui::dataStore->vpn_rule_process = previousBypass;
        NekoGui::dataStore->games_via_tunnel = previousGames;
        NekoGui::dataStore->vpn_rule_white = previousWhite;
        NekoGui::dataStore->custom_route_global = originalGlobals;
        check(GreenRhythm::RouteState::completeSnapshot(*NekoGui::dataStore->routing) == beforeRoute,
              "building all game/voice configurations leaves every route field untouched");
    }
#endif
    NekoGui::dataStore->vpn_rule_process_proxy = previousProxyProcesses;
    NekoGui::dataStore->vpn_internal_tun = previousTun;
    NekoGui::dataStore->spmode_vpn = previousVpn;
    NekoGui::dataStore->routing_defaults_choice = originalChoice;

    check(NekoGui::dataStore->geodata_auto_update
          && defaultConfig->coreConfig.value("geodata_update").toObject().value("enabled").toBool(),
          "new installations enable managed GeoIP updates in the actual runtime config");
    NekoGui::dataStore->geodata_auto_update = false;
    check(NekoGui::dataStore->ToJson().value("geodata_auto_update") == false
          && buildCurrent()->coreConfig.value("geodata_update").toObject().value("enabled") == false,
          "explicit disabled GeoIP updates survive settings serialization and config generation");
    NekoGui::dataStore->geodata_auto_update = true;
    check(!NekoGui::BuildConfig(NekoGui::profileManager->GetProfile(firstId), false, true)->coreConfig.contains("geodata_update"),
          "exported config contains no app-only GeoIP updater option");
    check(!NekoGui::BuildConfig(NekoGui::profileManager->GetProfile(firstId), true, false)->coreConfig.contains("geodata_update"),
          "latency test config cannot start automatic GeoIP downloads");
    NekoGui::dataStore->routing = std::move(originalRouting);

    const auto args = app.arguments();
    const auto storedIndex = args.indexOf("--stored-profile-file");
    const auto liveIndex = storedIndex >= 0 ? storedIndex : args.indexOf("--live-url-file");
    if (liveIndex >= 0 && liveIndex + 2 < args.size()) {
        NekoGui::dataStore->sub_clear = false;
        const auto output = args[liveIndex + 2];
        auto live = NekoGui::ProfileManager::NewGroup();
        live->name = "runtime-test";
        NekoGui::profileManager->AddGroup(live);
        if (storedIndex >= 0) {
            const auto saved = QJsonDocument::fromJson(read(args[liveIndex + 1])).object();
            auto stored = NekoGui::ProfileManager::NewProxyEntity(saved.value("type").toString());
            stored->fn = args[liveIndex + 1];
            const bool loaded = stored->Load();
            check(loaded, "stored runtime profile loads through the actual profile reader");
            if (loaded) {
                stored->id = -1;
                check(NekoGui::profileManager->AddProfile(stored, live->id), "stored profile copied into the isolated test group");
            }
        } else {
            const auto liveUrl = QString::fromUtf8(read(args[liveIndex + 1])).trimmed();
            NekoGui_sub::groupUpdater->Update(liveUrl, live->id, true);
        }
        const auto imported = live->Profiles();
        check(imported.size() == 1, "runtime test has exactly one actual user profile");
        if (!imported.isEmpty()) {
            const auto runtime = QJsonDocument::fromJson(read(output + "/runtime-settings.json")).object();
            NekoGui::dataStore->FromJson(runtime.value("datastore").toObject());
            NekoGui::dataStore->routing->FromJson(runtime.value("routing").toObject());
            const auto cores = runtime.value("external_cores").toObject();
            for (auto it = cores.begin(); it != cores.end(); ++it) NekoGui::dataStore->extraCore->Set(it.key(), it.value().toString());
            NekoGui::dataStore->inbound_socks_port = 23884;
            const auto built = NekoGui::BuildConfig(imported.first(), false, false);
            write(output + "/build-error.txt", built->error.toUtf8());
            check(built->error.isEmpty(), "actual app config builds for user subscription");
#ifdef Q_OS_WIN
            check(built->coreConfig.value("windows_work_vpn").toBool()
                  && built->coreConfig.value("route").toObject().value("auto_detect_interface").toBool(),
                  "Windows proxy config enables automatic work VPN discovery without domain exceptions");
#endif
            check(built->extRs.empty(), "user REALITY subscription connects without an external core");
            write(output + "/built-core.json", QJsonDocument(built->coreConfig).toJson());
            write(output + "/imported-profile.json", imported.first()->ToJsonBytes());
            int i = 0;
            for (const auto &external : built->extRs) {
                write(output + QStringLiteral("/built-external-%1.json").arg(i++), external->config_export.toUtf8());
            }
            NekoGui::dataStore->spmode_vpn = true;
            NekoGui::dataStore->vpn_internal_tun = true;
            NekoGui::dataStore->vpn_implementation = 1;
            const auto system = NekoGui::BuildConfig(imported.first(), false, false);
            bool actualSystem = false;
            for (const auto &value : system->coreConfig.value("inbounds").toArray()) {
                const auto inbound = value.toObject();
                if (inbound.value("type") == "tun") actualSystem = inbound.value("stack") == "system";
            }
            check(system->error.isEmpty() && actualSystem, "app generates a real System TUN configuration");
            write(output + "/built-system.json", QJsonDocument(system->coreConfig).toJson());
            const auto externalTunPath = NekoGui::WriteVPNSingBoxConfig();
            const auto externalTun = QJsonDocument::fromJson(read(externalTunPath)).object();
#ifdef Q_OS_WIN
            check(externalTun.value("windows_work_vpn").toBool()
                  && externalTun.value("route").toObject().value("auto_detect_interface").toBool(),
                  "external Windows TUN enables the same work VPN discovery");
#endif
            check(!externalTunPath.isEmpty()
                  && externalTun.value("inbounds").toArray().first().toObject().value("stack") == "system",
                  "external TUN also preserves the System selection");
            write(output + "/built-external-tun.json", QJsonDocument(externalTun).toJson());
        }
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
