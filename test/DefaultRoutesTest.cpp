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
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
