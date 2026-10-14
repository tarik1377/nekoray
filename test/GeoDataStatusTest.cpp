#include "ui/GeoDataStatus.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char *label) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using namespace GreenRhythm::GeoData;
    QTemporaryDir temp;
    check(temp.isValid(), "isolated settings directory");
    check(readStatus(temp.path()).isEmpty(), "missing status is not a fabricated successful update");
    const auto bootstrap = describe({}, true, true);
    check(bootstrap.contains(QStringLiteral("встроенная")) && !bootstrap.contains(QStringLiteral("успешная проверка:")),
          "offline bootstrap is distinguishable from successful network validation");
    QJsonObject status{{"state", "error"}, {"applied_at", "2026-10-14T12:00:00Z"},
                       {"checked_at", "2026-10-14T13:00:00Z"}, {"next_retry", "2026-10-14T13:05:00Z"},
                       {"prefix_count", 25171}, {"error", "<script>private-secret</script>"}};
    const auto failed = describe(status, true, true);
    check(failed.contains(QStringLiteral("Сохранена последняя рабочая база"))
          && failed.contains(QStringLiteral("25171")) && failed.contains(QStringLiteral("Повторная")),
          "failed download reports preservation and scheduled retry");
    check(!failed.contains(QStringLiteral("private-secret")), "untrusted download errors are not rendered in UI");
    check(describe(status, false, true).contains(QStringLiteral("выключено"))
          && !describe(status, false, true).contains(QStringLiteral("Повторная")),
          "disabled automatic updates do not promise scheduled retries");
    check(describe(status, true, false).contains(QStringLiteral("при подключении")), "offline manual check explains its requirement");
    check(describe(QJsonObject{{"state", "unused"}}, true, true).contains(QStringLiteral("не использует")),
          "a scheme without built-in RU data cannot promise an update");
    const auto unconfirmed = describe(QJsonObject{{"state", "unconfirmed"}, {"applied_at", "2026-10-14T12:00:00Z"}}, true, true);
    check(unconfirmed.contains(QStringLiteral("не подтвердило")) && !unconfirmed.contains(QStringLiteral("Рабочая база применена")),
          "unconfirmed rollback cannot claim that the old rules are active");
    status["applied_at"] = "invalid";
    status["checked_at"] = "invalid";
    status["prefix_count"] = -1;
    const auto invalid = describe(status, true, true);
    check(!invalid.contains(QStringLiteral("Рабочая база применена"))
          && !invalid.contains(QStringLiteral("Диапазонов РФ")), "invalid metadata cannot claim application or range count");
    status["applied_at"] = "0001-01-01T00:00:00Z";
    check(!describe(status, true, true).contains(QStringLiteral("Рабочая база применена")),
          "Go zero time is not a successful application timestamp");
    check(saveAutomatic(temp.path(), false), "atomic auto setting writes");
    QFile setting(QDir(temp.path()).filePath(".geodata/settings.json"));
    check(setting.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(setting.readAll()).object().value("enabled").isBool(),
          "manual disabled setting is a boolean, not an absent default");
    check(requestRefresh(temp.path()), "atomic request writes");
    QFile request(QDir(temp.path()).filePath(".geodata/refresh.request"));
    check(request.open(QIODevice::ReadOnly), "request readable");
    const auto first = request.readAll();
    request.close();
    check(first.size() < 128 && !QUuid(QString::fromUtf8(first.trimmed())).isNull(), "request is bounded UUID only");
    check(requestRefresh(temp.path()) && request.open(QIODevice::ReadOnly) && first != request.readAll(),
          "a repeated manual check has a distinct request token");
    QFile broken(QDir(temp.path()).filePath(".geodata/status.json"));
    check(broken.open(QIODevice::WriteOnly), "status fixture opens");
    broken.write("{broken"); broken.close();
    check(readStatus(temp.path()).isEmpty(), "partial status cannot be interpreted as fresh data");
    std::printf("GeoIP status: %d failures\n", failures);
    return failures ? 1 : 0;
}
