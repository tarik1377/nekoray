#include "main/VideoRoutes.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace GreenRhythm::VideoRoutes;
static int checks = 0, failures = 0;

static void check(bool value, const char *name) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}

static QJsonObject legacyRu() {
    return {{"direct_domain", "domain:ru\ndomain:example.org"},
            {"direct_ip", "geoip:ru\ngeoip:private"}, {"def_outbound", "proxy"},
            {"block_domain", "geosite:category-ads-all\ndomain:appcenter.ms"},
            {"remote_dns", "https://1.1.1.1/dns-query"}, {"direct_dns", "77.88.8.8"},
            {"custom", R"({"rules":[{"outbound":"bypass","process_name":["steam.exe"]},{"network":"udp","port":443,"outbound":"block"},{"process_name":["Telegram.exe"],"outbound":"proxy"}]})"},
            {"future_setting", QJsonObject{{"preserve", 123}}}};
}

static bool has(const QJsonObject &object, const char *rule) {
    return object.value("direct_domain").toString().split('\n').contains(QString::fromLatin1(rule));
}

static QByteArray bytes(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

static void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "fixture written");
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto before = legacyRu();
    const auto after = migrate(before);
    check(has(after, "domain:twitch.tv") && has(after, "domain:ttvnw.net")
          && has(after, "full:sfv.an-media.org"), "legacy RU schemes receive page and media routes");
    auto withoutDomains = after;
    withoutDomains["direct_domain"] = before["direct_domain"];
    check(withoutDomains == before, "all other fields and unknown settings survive");
    check(migrate(after) == after, "repeat migration is idempotent");
    const auto defaults = appendDefaults("domain:ru\n");
    check(defaults == after.value("direct_domain").toString().replace("domain:example.org\n", ""),
          "fresh defaults and legacy migration use the same media rules");
    check(appendDefaults(defaults) == defaults, "default rules do not duplicate");

    auto scheme = before;
    scheme["direct_domain"] = "";
    scheme["direct_ip"] = "geoip:private";
    check(migrate(scheme) == scheme, "full-tunnel schemes stay full-tunnel");
    scheme["direct_domain"] = "domain:twitch.tv";
    const auto alreadyDirect = migrate(scheme);
    check(has(alreadyDirect, "domain:ttvnw.net") && !has(alreadyDirect, "full:sfv.an-media.org"),
          "existing Twitch direct choice extends only to Twitch media");
    scheme = before;
    scheme["direct_domain"] = " # keep comment\r\n DOMAIN:RU \r\ndomain:an-media.org\ndomain:ttvnw.net";
    const auto covered = migrate(scheme).value("direct_domain").toString();
    check(!covered.contains("full:sfv.an-media.org") && covered.count("domain:ttvnw.net") == 1,
          "existing broader direct rules need no duplicate");
    check(covered.startsWith(scheme.value("direct_domain").toString()), "original comments and formatting retained");

    for (const auto &rule : {"domain:ttvnw.net", "full:usher.ttvnw.net", "domain:twitch.tv", "domain:net"}) {
        scheme = before;
        scheme["proxy_domain"] = rule;
        const auto result = migrate(scheme);
        check(!has(result, "domain:twitch.tv") && !has(result, "domain:ttvnw.net")
              && has(result, "full:sfv.an-media.org"), "explicit Twitch proxy selection protects the whole service");
    }
    scheme = before;
    scheme["block_domain"] = "full:sfv.an-media.org";
    check(!has(migrate(scheme), "full:sfv.an-media.org") && has(migrate(scheme), "domain:ttvnw.net"),
          "Anistar block preserved without suppressing Twitch repair");
    scheme["block_domain"] = "domain:an-media.org";
    check(!has(migrate(scheme), "full:sfv.an-media.org"), "parent-domain block preserved");
    scheme["block_domain"] = "domain:sfv.an-media.org.evil.example";
    check(has(migrate(scheme), "full:sfv.an-media.org"), "similar-looking domains do not conflict");
    scheme["proxy_domain"] = "keyword:usher";
    check(!has(migrate(scheme), "domain:ttvnw.net"), "keyword exception not widened over arbitrary subdomains");

    scheme = before;
    scheme["custom"] = R"({"rules":[{"outbound":"proxy","domain_suffix":".ttvnw.net"}]})";
    check(!has(migrate(scheme), "domain:ttvnw.net") && has(migrate(scheme), "full:sfv.an-media.org"),
          "custom domain selection retained");
    const auto global = QStringLiteral(R"({"rules":[{"outbound":"proxy","domain":["sfv.an-media.org"]}]})");
    check(!has(migrate(before, global), "full:sfv.an-media.org") && has(migrate(before, global), "domain:ttvnw.net"),
          "global custom rules retained too");
    scheme["custom"] = R"({"rules":[{"type":"logical","mode":"or","rules":[{"domain_suffix":["twitch.tv"]}],"outbound":"block"}]})";
    check(!has(migrate(scheme), "domain:ttvnw.net"), "logical child selection protected");
    for (const auto &invalid : {"{broken", "[]", "{}", "{\"rules\":[42]}"}) {
        scheme = before;
        scheme["custom"] = invalid;
        check(migrate(scheme) == scheme, "unreadable custom rules left unchanged");
    }
    check(migrate(before, "{broken") == before, "unreadable global rules left unchanged");
    for (const auto &field : {"proxy_ip", "block_ip"}) {
        scheme = before;
        scheme[field] = "0.0.0.0/0";
        check(migrate(scheme) == scheme, "explicit IP choice is not overridden by a new domain rule");
    }

    QTemporaryDir directory;
    check(directory.isValid(), "isolated file directory created");
    const auto routePath = directory.filePath("Default");
    const auto originalBytes = QJsonDocument(before).toJson();
    write(routePath, originalBytes);
    QString loaded = before.value("direct_domain").toString(), error;
    check(migrateFile(routePath, {}, &loaded, &error) == FileResult::Updated, "route file migration committed");
    check(QJsonDocument::fromJson(bytes(routePath)).object() == after
          && loaded == after.value("direct_domain").toString(), "disk and loaded route agree after commit");
    const auto committedBytes = bytes(routePath);
    check(migrateFile(routePath, {}, &loaded, &error) == FileResult::Unchanged
          && bytes(routePath) == committedBytes, "repeat file migration does not rewrite");
    loaded = "domain:curated.example";
    check(migrateFile(routePath, {}, &loaded, &error) == FileResult::Failed
          && bytes(routePath) == committedBytes && loaded == "domain:curated.example", "stale loaded scheme rejected");
    write(routePath, "{broken");
    check(migrateFile(routePath, {}, &loaded, &error) == FileResult::Failed
          && bytes(routePath) == "{broken", "bad JSON file is not replaced");
    check(migrateFile(directory.filePath("missing"), {}, &loaded, &error) == FileResult::Failed,
          "missing file reports failure");

#ifdef Q_OS_WIN
    write(routePath, originalBytes);
    loaded = before.value("direct_domain").toString();
    const HANDLE held = CreateFileW(reinterpret_cast<LPCWSTR>(routePath.utf16()), GENERIC_READ,
                                   FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(held != INVALID_HANDLE_VALUE, "replacement failure fixture locked");
    if (held != INVALID_HANDLE_VALUE) {
        check(migrateFile(routePath, {}, &loaded, &error) == FileResult::Failed
              && loaded == before.value("direct_domain").toString()
              && bytes(routePath) == originalBytes, "failed replacement preserves file and loaded route");
        CloseHandle(held);
    }
#endif

    const auto settingsPath = directory.filePath("greenrhythm.json");
    const QJsonObject settings{{"routing_quic_migrated", true}, {"routing_games_migrated", true},
                               {"routing_launcher_migrated", true}, {"remember_id", 12},
                               {"custom_setting", QJsonObject{{"keep", true}}}};
    write(settingsPath, QJsonDocument(settings).toJson());
    check(markComplete(settingsPath, &error), "new migration flag persisted for previous upgraders");
    auto marked = QJsonDocument::fromJson(bytes(settingsPath)).object();
    check(marked.take("routing_video_migrated").toBool() && marked == settings, "flag update preserves old settings");
    write(settingsPath, "{broken");
    check(!markComplete(settingsPath, &error) && bytes(settingsPath) == "{broken", "flag not recorded on persistence failure");
    std::printf("Video route migration: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
