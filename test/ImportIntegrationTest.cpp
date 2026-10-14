#include "db/ConfigBuilder.hpp"
#include "fmt/includes.h"
#include "main/HTTPRequestHelper.hpp"
#include "sub/GroupUpdater.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
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
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir temp;
    check(temp.isValid() && QDir::setCurrent(temp.path()), "isolated store created");
    for (const auto &dir : {"groups", "profiles", "temp", "routes_box"}) QDir().mkdir(dir);
    MW_show_log = [](QString) {};
    MW_dialog_message = [](QString, QString) {};
    NekoGui::dataStore->current_group = 0;
    NekoGui::dataStore->routing = std::make_unique<NekoGui::Routing>();
    NekoGui::profileManager->LoadManager();

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
