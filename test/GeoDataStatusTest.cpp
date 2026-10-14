#include "ui/GeoDataStatus.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

static int checks = 0, failures = 0;
static void check(bool ok, const char *label) {
    ++checks;
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

    const auto emptyBrief = brief({}, true, true);
    check(emptyBrief.contains(QStringLiteral("встроенная база"))
          && !emptyBrief.contains(QStringLiteral("Проверено:"))
          && !emptyBrief.contains(QStringLiteral("диапазонов РФ:")),
          "brief empty status reports available fallback without inventing a verified update");
    const auto offlineBrief = brief({}, true, false);
    check(offlineBrief.contains(QStringLiteral("подключитесь к VPN"))
          && !offlineBrief.contains(QStringLiteral("Проверено:")),
          "brief disconnected state explains the connection requirement without claiming success");
    const auto checkingBrief = brief(QJsonObject{{"state", "checking"}}, true, true);
    check(checkingBrief.contains(QStringLiteral("Проверяем обновление"))
          && !checkingBrief.contains(QStringLiteral("Доступна встроенная"))
          && !checkingBrief.contains(QStringLiteral("Проверено:")),
          "brief active check is distinct from completion and bootstrap fallback");
    const QJsonObject updated{{"state", "updated"}, {"checked_at", "2026-10-14T13:00:00Z"},
                              {"applied_at", "2026-10-14T12:00:00Z"}, {"prefix_count", 25171},
                              {"error", "<script>private-secret</script>"}};
    const auto expectedChecked = QDateTime(QDate(2026, 10, 14), QTime(13, 0), Qt::UTC)
                                     .toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"));
    const auto updatedBrief = brief(updated, true, true);
    check(updatedBrief.contains(QStringLiteral("Проверено: ") + expectedChecked)
          && updatedBrief.contains(QStringLiteral("диапазонов РФ: 25171")),
          "brief successful check presents its local date and verified range count together");
    check(updatedBrief.contains(QStringLiteral("раз в сутки"))
          && updatedBrief.contains(QStringLiteral("без перезапуска туннеля"))
          && !updatedBrief.contains(QStringLiteral("private-secret"))
          && !updatedBrief.contains(QStringLiteral("<script>")),
          "brief auto status describes cadence without exposing untrusted error text");
    const auto manualBrief = brief(updated, false, true);
    check(manualBrief.contains(QStringLiteral("Автопроверка выключена"))
          && manualBrief.contains(QStringLiteral("вручную"))
          && manualBrief.contains(expectedChecked)
          && !manualBrief.contains(QStringLiteral("раз в сутки")),
          "brief disabled auto preserves useful last-check metadata without promising scheduled checks");
    check(brief({}, false, false).contains(QStringLiteral("подключитесь к VPN"))
          && brief({}, false, false).contains(QStringLiteral("Автопроверка выключена")),
          "brief offline and disabled auto are represented independently");
    auto unusedStatus = updated;
    unusedStatus["state"] = "unused";
    const auto unusedBrief = brief(unusedStatus, true, true);
    check(unusedBrief.contains(QStringLiteral("нет встроенного правила GeoIP РФ"))
          && unusedBrief.contains(QStringLiteral("Ваши правила сохранены"))
          && !unusedBrief.contains(QStringLiteral("Проверено:"))
          && !unusedBrief.contains(QStringLiteral("25171"))
          && !unusedBrief.contains(QStringLiteral("раз в сутки")),
          "brief unused scheme does not advertise a stale successful update or active auto schedule");
    auto unconfirmedStatus = updated;
    unconfirmedStatus["state"] = "unconfirmed";
    const auto unconfirmedBrief = brief(unconfirmedStatus, true, true);
    check(unconfirmedBrief.contains(QStringLiteral("не подтвердило"))
          && unconfirmedBrief.contains(QStringLiteral("резервная копия сохранена"))
          && !unconfirmedBrief.contains(QStringLiteral("Проверено:"))
          && !unconfirmedBrief.contains(QStringLiteral("25171"))
          && !unconfirmedBrief.contains(QStringLiteral("действует предыдущая база")),
          "brief unconfirmed application takes precedence over stale success metadata and never asserts active rules");
    check(brief(unconfirmedStatus, false, false).contains(QStringLiteral("не подтвердило")),
          "brief unconfirmed warning remains visible when offline with auto disabled");
    auto failedStatus = updated;
    failedStatus["state"] = "error";
    const auto failedBrief = brief(failedStatus, false, true);
    check(failedBrief.contains(QStringLiteral("Обновить не удалось"))
          && failedBrief.contains(QStringLiteral("действует предыдущая база"))
          && failedBrief.contains(expectedChecked)
          && !failedBrief.contains(QStringLiteral("private-secret")),
          "brief failed refresh retains last successful check without hiding the failed attempt");
    auto invalidBriefStatus = updated;
    invalidBriefStatus["checked_at"] = "invalid";
    check(!brief(invalidBriefStatus, true, true).contains(QStringLiteral("Проверено:"))
          && !brief(invalidBriefStatus, true, true).contains(QStringLiteral("25171")),
          "brief invalid check timestamp cannot validate an otherwise plausible count");
    invalidBriefStatus["checked_at"] = "0001-01-01T00:00:00Z";
    check(!brief(invalidBriefStatus, true, true).contains(QStringLiteral("Проверено:")),
          "brief Go zero timestamp is not presented as a successful check");
    auto countStatus = updated;
    for (const int count : {-1, 0, 250001}) {
        countStatus["prefix_count"] = count;
        const auto countBrief = brief(countStatus, true, true);
        check(countBrief.contains(QStringLiteral("Последняя проверка: ") + expectedChecked)
              && !countBrief.contains(QStringLiteral("диапазонов РФ:")),
              "brief rejects out-of-range counts while retaining the valid check date");
    }
    countStatus["prefix_count"] = 250000;
    check(brief(countStatus, true, true).contains(QStringLiteral("диапазонов РФ: 250000")),
          "brief accepts the documented maximum range count");
    check(QDir(temp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()
          && updated.value("state").toString() == "updated"
          && updated.value("prefix_count").toInt() == 25171,
          "rendering brief states creates no diagnostic settings or refresh request and leaves inputs unchanged");
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
    std::printf("GeoIP status: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
