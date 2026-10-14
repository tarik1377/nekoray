#include "main/DefaultRoutes.hpp"
#include <QStringList>
#include "fmt/Preset.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace GreenRhythm::DefaultRoutes;
static int checks = 0, failures = 0;
static void check(bool ok, const char *name) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}
static QByteArray bytes(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
static void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "write fixture");
}
static QJsonObject legacyDnsScheme() {
    return QJsonObject{
        {"def_outbound", "proxy"}, {"dns_routing", true}, {"use_dns_object", false},
        {"direct_dns", "77.88.8.8"}, {"direct_dns_strategy", "ipv4_only"},
        {"remote_dns", "https://1.1.1.1/dns-query"}, {"remote_dns_strategy", "ipv4_only"},
        {"dns_final_out", "bypass"},
        {"direct_domain", "# retained\r\n domain:ru \r\ndomain:su\r\nfull:chosen.example"},
        {"direct_ip", "geoip:ru\n192.168.0.0/16"},
        {"proxy_domain", "full:must-proxy.example"}, {"block_domain", "full:blocked.example"},
        {"custom", "{\"rules\":[{\"process_name\":\"mygame.exe\",\"outbound\":\"bypass\"}]}"},
        {"local_dns_domains", "domain:office.example"}, {"local_dns_server", "10.60.0.53"},
        {"local_dns_hosts", "git.office.example=10.60.1.10"},
        {"dns_object", "{\"preserve_unused\":true}"}, {"future", QJsonObject{{"keep", 123}}}
    };
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    check(Preset::SingBox::VpnImplementation == QStringList{"gvisor", "system", "mixed"},
          "stored index 1 means genuine System");
    const QJsonObject before{
        {"block_domain", "  geosite:category-ads-all\r\n# keep\r\ndomain:appcenter.ms\r\n\ngeosite:category-ads-all\nfull:keep.example"},
        {"direct_domain", "domain:ru\ndomain:vk.com"},
        {"proxy_domain", "geosite:category-ads-all"},
        {"block_ip", "geoip:test"},
        {"custom", "{\"rules\":[{\"port\":443,\"network\":\"udp\",\"outbound\":\"block\"}]}"},
        {"future", QJsonObject{{"keep", 123}}}
    };
    const auto after = migrate(before);
    check(after.value("block_domain").toString() == "# keep\r\ndomain:appcenter.ms\r\n\nfull:keep.example",
          "remove exact inherited token, retaining order comments blanks and CRLF");
    auto expected = before;
    expected["block_domain"] = after["block_domain"];
    check(after == expected, "every other field preserved");
    check(migrate(after) == after, "idempotent");
    check(removeAdsSelector("geosite:category-ads-all-extra\n# geosite:category-ads-all")
          == "geosite:category-ads-all-extra\n# geosite:category-ads-all", "no partial or comment match");
    check(removeAdsSelector("geosite:category-ads-all").isEmpty(), "sole selector removed");
    check(migrate(QJsonObject{{"other", 1}}) == QJsonObject{{"other", 1}}, "missing field untouched");
    check(migrate(QJsonObject{{"block_domain", 123}}) == QJsonObject{{"block_domain", 123}}, "unknown field type untouched");
    QTemporaryDir dir;
    const auto path = dir.filePath("scheme.json");
    write(path, QJsonDocument(before).toJson());
    QString loaded = before["block_domain"].toString(), error;
    check(migrateFile(path, &loaded, &error) == FileResult::Updated, "saved scheme migrated");
    check(loaded == after["block_domain"].toString(), "loaded scheme synchronized after commit");
    check(QJsonDocument::fromJson(bytes(path)).object() == after, "unknown saved fields retained");
    const auto committed = bytes(path);
    check(migrateFile(path, &loaded) == FileResult::Unchanged && bytes(path) == committed, "retry does not rewrite file");
    write(path, "{broken");
    loaded = "unchanged";
    check(migrateFile(path, &loaded) == FileResult::Failed && loaded == "unchanged"
          && bytes(path) == "{broken", "malformed JSON preserved, loaded state unchanged");
    check(migrateFile(dir.filePath("missing")) == FileResult::Failed, "missing file remains retryable");
#ifdef Q_OS_WIN
    write(path, QJsonDocument(before).toJson());
    const auto original = bytes(path);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "lock fixture against replace");
    loaded = "unchanged";
    check(migrateFile(path, &loaded) == FileResult::Failed && loaded == "unchanged"
          && bytes(path) == original, "failed atomic replacement leaves disk and memory intact");
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#endif
    const auto settings = dir.filePath("settings.json");
    write(settings, "{\"vpn_impl\":2,\"future\":{\"x\":7}}");
    check(markComplete(settings), "completion marker committed");
    check(QJsonDocument::fromJson(bytes(settings)).object()
          == QJsonObject{{"vpn_impl", 2}, {"routing_ads_removed", true}, {"future", QJsonObject{{"x", 7}}}},
          "existing explicit stack and unknown settings preserved");
    check(!markComplete(dir.filePath("missing")), "marker failure stays retryable");

    const auto oldDns = legacyDnsScheme();
    auto expectedDns = oldDns;
    expectedDns["direct_dns"] = "https://1.1.1.1/dns-query";
    expectedDns["dns_final_out"] = "proxy";
    check(repairDns(oldDns) == expectedDns,
          "inherited RU DNS repaired without changing scoped hosts routes comments or unknown fields");
    check(repairDns(expectedDns) == expectedDns, "repaired DNS tuple is idempotent");
    auto oldProxyDns = oldDns;
    oldProxyDns["dns_final_out"] = "proxy";
    check(repairDns(oldProxyDns) == expectedDns, "old RU proxy DNS final also gets direct DoH");
    auto implicitSimple = oldDns;
    implicitSimple.remove("use_dns_object");
    auto repairedImplicit = expectedDns;
    repairedImplicit.remove("use_dns_object");
    check(repairDns(implicitSimple) == repairedImplicit, "absent optional DNS-object flag retains simple DNS mode");
    const QList<QPair<QString, QJsonValue>> customized{
        {"direct_dns", "9.9.9.9"}, {"direct_dns", "https://chosen.example/dns-query"},
        {"remote_dns", "https://chosen.example/dns-query"},
        {"direct_dns_strategy", "prefer_ipv6"}, {"remote_dns_strategy", "prefer_ipv6"},
        {"dns_final_out", "direct"}, {"def_outbound", "bypass"},
        {"dns_routing", false}, {"dns_routing", "true"},
        {"use_dns_object", true}, {"use_dns_object", 1},
        {"direct_domain", "domain:ru\n# domain:su"},
        {"direct_domain", "keyword:ru\ndomain:su"},
        {"direct_ip", "192.168.0.0/16\n# geoip:ru"}
    };
    for (const auto &choice : customized) {
        auto chosen = oldDns;
        chosen[choice.first] = choice.second;
        check(repairDns(chosen) == chosen, qPrintable("custom field retained: " + choice.first));
    }
    for (const auto &field : {"direct_dns", "remote_dns", "direct_dns_strategy", "remote_dns_strategy",
                             "dns_final_out", "def_outbound", "dns_routing", "direct_domain", "direct_ip"}) {
        auto incomplete = oldDns;
        incomplete.remove(field);
        check(repairDns(incomplete) == incomplete, "incomplete inherited signature not guessed");
    }

    const auto dnsPath = dir.filePath("dns-scheme.json");
    write(dnsPath, QJsonDocument(oldDns).toJson());
    QString loadedDirectDns = "77.88.8.8", loadedFinal = "bypass";
    check(repairDnsFile(dnsPath, &loadedDirectDns, &loadedFinal, &error) == FileResult::Updated
          && loadedDirectDns == "https://1.1.1.1/dns-query" && loadedFinal == "proxy",
          "successful atomic DNS commit synchronizes both active values");
    check(QJsonDocument::fromJson(bytes(dnsPath)).object() == expectedDns, "committed DNS JSON preserves all other fields");
    const auto dnsCommitted = bytes(dnsPath);
    check(repairDnsFile(dnsPath, &loadedDirectDns, &loadedFinal) == FileResult::Unchanged
          && bytes(dnsPath) == dnsCommitted, "DNS retry does not rewrite committed data");

    write(dnsPath, QJsonDocument(oldDns).toJson());
    const auto dnsOriginal = bytes(dnsPath);
    loadedDirectDns = "unsaved custom DNS";
    loadedFinal = "bypass";
    check(repairDnsFile(dnsPath, &loadedDirectDns, &loadedFinal) == FileResult::Failed
          && loadedDirectDns == "unsaved custom DNS" && loadedFinal == "bypass"
          && bytes(dnsPath) == dnsOriginal, "active DNS mismatch never overwrites disk or unsaved memory");
    loadedDirectDns = "77.88.8.8";
    loadedFinal = "unsaved final";
    check(repairDnsFile(dnsPath, &loadedDirectDns, &loadedFinal) == FileResult::Failed
          && bytes(dnsPath) == dnsOriginal && loadedFinal == "unsaved final",
          "active final mismatch stays retryable");
    write(dnsPath, "{broken");
    check(repairDnsFile(dnsPath, &loadedDirectDns, &loadedFinal) == FileResult::Failed
          && bytes(dnsPath) == "{broken", "DNS repair never replaces malformed JSON");
    check(repairDnsFile(dir.filePath("missing-dns")) == FileResult::Failed, "missing DNS scheme remains retryable");

    const auto dnsSettings = dir.filePath("dns-settings.json");
    const QByteArray settingsBefore = "{\"routing_dns_repaired\":false,\"vpn_impl\":1,\"future\":{\"x\":7}}";
    write(dnsSettings, settingsBefore);
    write(dnsPath, QJsonDocument(oldDns).toJson());
    loadedDirectDns = "77.88.8.8";
    loadedFinal = "bypass";
#ifdef Q_OS_WIN
    HANDLE dnsLock = CreateFileW(reinterpret_cast<LPCWSTR>(dnsPath.utf16()), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(dnsLock != INVALID_HANDLE_VALUE, "lock DNS scheme against atomic replace");
    const auto dnsLocked = bytes(dnsPath);
    check(repairDnsFile(dnsPath, &loadedDirectDns, &loadedFinal) == FileResult::Failed
          && loadedDirectDns == "77.88.8.8" && loadedFinal == "bypass"
          && bytes(dnsPath) == dnsLocked && bytes(dnsSettings) == settingsBefore,
          "failed DNS commit preserves memory disk and uncommitted settings marker");
    if (dnsLock != INVALID_HANDLE_VALUE) CloseHandle(dnsLock);
#endif
    check(repairDnsFile(dnsPath, &loadedDirectDns, &loadedFinal) == FileResult::Updated,
          "DNS commit succeeds after unlock");
    check(bytes(dnsSettings) == settingsBefore, "scheme repair does not commit settings marker early");
#ifdef Q_OS_WIN
    HANDLE markerLock = CreateFileW(reinterpret_cast<LPCWSTR>(dnsSettings.utf16()), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(markerLock != INVALID_HANDLE_VALUE, "lock DNS marker against atomic replace");
    check(!markDnsComplete(dnsSettings) && bytes(dnsSettings) == settingsBefore,
          "failed settings commit keeps DNS migration retryable");
    if (markerLock != INVALID_HANDLE_VALUE) CloseHandle(markerLock);
    check(repairDnsFile(dnsPath, &loadedDirectDns, &loadedFinal) == FileResult::Unchanged,
          "retry after failed marker accepts already repaired scheme");
#endif
    check(markDnsComplete(dnsSettings), "DNS marker committed after schemes");
    check(QJsonDocument::fromJson(bytes(dnsSettings)).object()
          == QJsonObject{{"routing_dns_repaired", true}, {"vpn_impl", 1}, {"future", QJsonObject{{"x", 7}}}},
          "DNS marker preserves explicit stack and unknown settings");
    check(!markDnsComplete(dir.filePath("missing-dns-settings")), "missing DNS marker settings remain retryable");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
