#include "main/RouteDefaultsPolicy.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace Policy = GreenRhythm::RouteDefaultsPolicy;
static int checks = 0, failures = 0;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
static QByteArray read(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
static void write(const QString &path, const QByteArray &data) {
    QFile f(path); check(f.open(QIODevice::WriteOnly) && f.write(data) == data.size(), "write fixture");
}
static QJsonObject current() {
    return {{"def_outbound", "proxy"}, {"dns_routing", true}, {"use_dns_object", false},
        {"direct_dns", "77.88.8.8"}, {"direct_dns_strategy", "ipv4_only"},
        {"remote_dns", "https://1.1.1.1/dns-query"}, {"remote_dns_strategy", "ipv4_only"},
        {"dns_final_out", "bypass"}, {"direct_domain", "# keep\r\ndomain:ru\r\ndomain:su\r\nfull:my.example"},
        {"direct_ip", "geoip:ru\n10.50.0.0/16"}, {"proxy_domain", "full:my-vpn.example"},
        {"block_domain", "full:my-block.example"},
        {"custom", "{\"future\":7,\"rules\":[{\"process_name\":\"mygame.exe\",\"outbound\":\"proxy\"}]}"},
        {"local_dns_domains", "domain:office.example"}, {"local_dns_server", "10.60.0.53"},
        {"local_dns_hosts", "git.office.example=10.60.1.10"}, {"future", QJsonObject{{"keep", 123}}}};
}
static QJsonObject preset() {
    auto result = current();
    result["direct_domain"] = "domain:ru\ndomain:su";
    result["direct_ip"] = "geoip:ru";
    result["proxy_domain"] = "";
    result["block_domain"] = "domain:appcenter.ms";
    result["direct_dns"] = "https://1.1.1.1/dns-query";
    result["dns_final_out"] = "proxy";
    result["custom"] = "{\"rules\":[{\"process_name\":\"standardgame.exe\",\"outbound\":\"bypass\"},{\"network\":\"udp\",\"port\":443,\"outbound\":\"block\"},{\"protocol\":\"quic\",\"outbound\":\"block\"}]}";
    return result;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    check(Policy::needsReview(0) && !Policy::needsReview(Policy::Revision)
          && !Policy::needsReview(Policy::Revision + 1), "review is versioned and newer choices survive downgrade");
    const auto old = current();
    const auto safe = Policy::preview(old, preset(), Policy::Mode::SafeUpgrade);
    check(safe.mode == Policy::Mode::SafeUpgrade && safe.after["direct_dns"] == "https://1.1.1.1/dns-query"
          && safe.after["dns_final_out"] == "proxy", "safe review includes inherited DNS repair");
    const auto custom = QJsonDocument::fromJson(safe.after["custom"].toString().toUtf8()).object();
    const auto oldCustom = QJsonDocument::fromJson(old["custom"].toString().toUtf8()).object();
    check(custom["future"] == oldCustom["future"]
          && custom["rules"].toArray().first() == oldCustom["rules"].toArray().first(),
          "safe upgrade preserves explicit game proxy rule and original precedence");
    check(!safe.after["custom"].toString().contains("standardgame.exe"), "safe upgrade never replaces process choices with preset bypass");
    check(safe.after["direct_domain"].toString().startsWith(old["direct_domain"].toString())
          && safe.after["proxy_domain"] == old["proxy_domain"] && safe.after["block_domain"] == old["block_domain"],
          "safe upgrade retains custom selectors comments and CRLF prefix");
    check(safe.addedRules > 0 && safe.removedRules == 0 && safe.changedFields.contains("custom"), "safe preview reports additive rules and DNS changes");
    for (const auto &field : {"future", "local_dns_domains", "local_dns_server", "local_dns_hosts"})
        check(safe.after[field] == old[field], "safe preserves scoped and unknown fields");
    auto chosenDns = old; chosenDns["direct_dns"] = "https://custom.example/dns-query";
    check(Policy::preview(chosenDns, preset(), Policy::Mode::SafeUpgrade).after["direct_dns"] == chosenDns["direct_dns"], "custom public DNS not overridden");
    auto customScheme = old; customScheme["direct_domain"] = "full:my.example";
    check(Policy::preview(customScheme, preset(), Policy::Mode::SafeUpgrade).after == customScheme, "unrecognised custom scheme not inferred as RU preset");
    auto unreadable = old; unreadable["custom"] = "{broken";
    check(Policy::preview(unreadable, preset(), Policy::Mode::SafeUpgrade).after["custom"] == unreadable["custom"], "malformed custom rules are preserved");
    const auto reset = Policy::preview(old, preset(), Policy::Mode::ResetRoutes);
    check(reset.after["custom"] == preset()["custom"] && reset.after["direct_domain"] == preset()["direct_domain"]
          && reset.removedRules > 0, "full reset differs explicitly from safe upgrade");
    for (const auto &field : {"future", "local_dns_domains", "local_dns_server", "local_dns_hosts"})
        check(reset.after[field] == old[field], "reset preserves scoped and unknown fields");

    QTemporaryDir temp;
    QDir().mkpath(temp.filePath("routes_box")); QDir().mkpath(temp.filePath("groups"));
    const auto route = temp.filePath("routes_box/Default"), settings = temp.filePath("groups/greenrhythm.json");
    const QByteArray original = QJsonDocument(old).toJson(QJsonDocument::Compact) + "\r\n";
    const QByteArray originalSettings = "{\"vpn_impl\":1,\"future\":{\"keep\":7}}";
    write(route, original); write(settings, originalSettings);
    auto result = Policy::keep(settings);
    check(result.success && !Policy::needsReview(result.settings["routing_defaults_review_revision"].toInt())
          && result.settings["routing_defaults_choice"] == "keep" && read(route) == original,
          "keep commits reviewed revision without touching route bytes");
    check(result.settings["future"].toObject()["keep"] == 7 && result.settings["vpn_impl"] == 1, "keep preserves other settings and explicit System");
    write(settings, originalSettings);
    result = Policy::apply(route, settings, safe, original);
    check(result.success && result.hasScheme && result.routeChanged && result.scheme == safe.after
          && Policy::canRestore(route, result.settings), "apply atomically commits preview and restorable backup");
    const auto applied = read(route);
    result = Policy::restore(route, settings);
    check(result.success && read(route) == original && result.scheme == old
          && !Policy::canRestore(route, result.settings), "restore is byte exact including unknown fields process rules and line endings");
    result = Policy::apply(route, settings, reset, original);
    check(result.success && result.settings["routing_defaults_choice"] == "reset", "explicit full reset commits distinct choice");
    write(route, read(route) + " ");
    const auto edited = read(route), savedSettings = read(settings);
    result = Policy::restore(route, settings);
    check(!result.success && read(route) == edited && read(settings) == savedSettings, "restore refuses subsequent custom edits");
    write(route, original); write(settings, originalSettings);
    result = Policy::apply(route, settings, safe, original + "stale");
    check(!result.success && read(route) == original && read(settings) == originalSettings, "stale preview changes nothing");
#ifdef Q_OS_WIN
    auto lock = [](const QString &path) { return CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); };
    HANDLE routeLock = lock(route);
    check(routeLock != INVALID_HANDLE_VALUE, "lock route fixture");
    result = Policy::apply(route, settings, safe, original);
    check(!result.success && !result.rollbackFailed && read(route) == original && read(settings) == originalSettings, "locked route leaves settings and memory uncommitted");
    CloseHandle(routeLock);
    HANDLE settingsLock = lock(settings);
    check(settingsLock != INVALID_HANDLE_VALUE, "lock settings fixture");
    result = Policy::apply(route, settings, safe, original);
    check(!result.success && !result.rollbackFailed && read(route) == original && read(settings) == originalSettings,
          "failed marker commit rolls route back byte exactly");
    check(!Policy::keep(settings).success, "failed keep choice remains pending");
    CloseHandle(settingsLock);
    result = Policy::apply(route, settings, safe, original);
    check(result.success, "apply succeeds after unlock");
    const auto beforeRestore = read(route), beforeRestoreSettings = read(settings);
    settingsLock = lock(settings);
    result = Policy::restore(route, settings);
    check(!result.success && !result.rollbackFailed && read(route) == beforeRestore
          && read(settings) == beforeRestoreSettings, "failed restore marker rolls back the restoration");
    CloseHandle(settingsLock);
    check(Policy::restore(route, settings).success && read(route) == original, "restore retry succeeds after unlock");
#endif
    auto badSettings = QJsonDocument::fromJson(read(settings)).object();
    badSettings["routing_defaults_backup"] = "../groups/greenrhythm.json";
    check(!Policy::canRestore(route, badSettings), "backup cannot redirect reads to settings or profiles");
    write(route, "{broken");
    check(!Policy::apply(route, settings, safe, "{broken").success && read(route) == "{broken", "malformed route JSON never overwritten");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
