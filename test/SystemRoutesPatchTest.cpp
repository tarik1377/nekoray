#define SYSTEM_ROUTES_PATCH_TEST
#include "support/SystemRoutesPatch.cpp"
#include <QTemporaryDir>

static int checks = 0, failures = 0;
static void check(bool ok, const char *name) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}
static bool rejected(const std::function<void()> &action) {
    try { action(); return false; } catch (const QString &) { return true; }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const auto root = dir.path();
    QDir().mkpath(root + "/config/groups");
    QDir().mkpath(root + "/config/routes_box");
    const auto settings = root + "/config/groups/greenrhythm.json";
    const auto route = root + "/config/routes_box/default.json";
    write(settings, {{"vpn_impl", 2}, {"active_routing", "default.json"}, {"future", 7}});
    write(route, {{"block_domain", "geosite:category-ads-all\ndomain:appcenter.ms"},
                  {"custom", "{\"rules\":[{\"outbound\":\"bypass\",\"process_name\":[\"steam.exe\"]}]}"},
                  {"direct_domain", "domain:ru"}, {"future", 7}});
    const auto settingsBytes = read(settings), routeBytes = read(route);
    const auto plan = makePlan(root);
    check(read(settings) == settingsBytes && read(route) == routeBytes, "plan is read-only");
    const auto noGui = [] {};
    check(rejected([&] { apply(plan, [] { throw QString("GUI running"); }); }),
          "running GUI rejected before config writes");
    check(read(settings) == settingsBytes && read(route) == routeBytes, "GUI rejection preserves bytes");
    {
        RunGuard guard("greenrhythm" + QDir(root).absolutePath());
        quint64 data = 42;
        check(guard.tryToRun(&data), "fixture holds actual GUI RunGuard");
        check(rejected([&] { apply(plan, noGui); }), "GUI lock rejects offline patch");
    }
    auto edited = plan;
    edited["files"] = QJsonArray{};
    check(rejected([&] { apply(edited, noGui); }), "incomplete manifest refused");
    write(settings, {{"vpn_impl", 2}, {"future", 99}});
    const auto changedSettings = read(settings);
    check(rejected([&] { apply(plan, noGui); }), "stale settings hash refused");
    check(read(settings) == changedSettings && read(route) == routeBytes, "stale rejection leaves all files intact");
    write(settings, object(settingsBytes));
    apply(makePlan(root), noGui); // Only an isolated QTemporaryDir; never user config.
    auto expectedRoute = object(routeBytes);
    expectedRoute["block_domain"] = "domain:appcenter.ms";
    check(object(read(route)) == expectedRoute, "only exact ads selector removed");
    auto expectedSettings = object(settingsBytes);
    expectedSettings["vpn_impl"] = 1;
    expectedSettings["routing_ads_removed"] = true;
    check(object(read(settings)) == expectedSettings, "System selected, completion set, other keys retained");
    const QDir backups(root);
    const auto names = backups.entryList({"system-routes-backup-*"}, QDir::Dirs);
    check(names.size() == 1, "one complete backup");
    if (names.size() == 1) {
        check(read(root + '/' + names[0] + "/groups/greenrhythm.json") == settingsBytes,
              "settings backup contains exact original bytes");
        check(read(root + '/' + names[0] + "/routes_box/default.json") == routeBytes,
              "scheme backup contains exact original bytes");
    }
    check(rejected([&] { apply(plan, noGui); }), "old plan cannot overwrite already migrated files");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
