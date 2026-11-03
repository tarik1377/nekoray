#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "main/ProgramRouting.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
using namespace GreenRhythm;
static QByteArray read(const QString &path) { QFile file(path); file.open(QIODevice::ReadOnly); return file.readAll(); }
static void write(const QString &path, const QByteArray &data) { QFile file(path); file.open(QIODevice::WriteOnly); file.write(data); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *text) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "PASS" : "FAIL", text); };
    const auto apps = GameAppRules::serialize({{"BROWSER.EXE", "proxy"}, {"dota2.exe", "bypass"}, {"Discord.exe", "proxy"}});
    const ProgramRouting::State before{"# keep\r\nRobloxStudioBeta.exe\r\n browser.exe \r\nBROWSER.EXE\r\nnot-browser.exe\r\n", "chrome.exe\nbrowser.exe\n", apps, false};
    ProgramRouting::State after; QString error;
    check(ProgramRouting::restore(before, "Browser.Exe", &after, &error), "restore one exact browser");
    check(after.bypass == "# keep\r\nRobloxStudioBeta.exe\r\nnot-browser.exe\r\n", "preserve unrelated names, comments and CRLF");
    check(after.proxy == "chrome.exe\n", "clear selected browser from both legacy directions");
    QJsonObject remaining; GameAppRules::parse(after.applications, &remaining);
    check(GameAppRules::mode(remaining, "browser.exe") == "auto" && remaining.value("dota2.exe") == "bypass" && remaining.value("Discord.exe") == "proxy", "clear new override while preserving game and voice choices");
    check(ProgramRouting::browserOverrides(before) == QStringList({"browser.exe", "chrome.exe"}) && ProgramRouting::browserOverrides(after) == QStringList({"chrome.exe"}), "deduplicate browser conflicts across every program list");
    check(ProgramRouting::overrideMode(before, "browser.exe") == "proxy" && ProgramRouting::overrideMode(after, "browser.exe") == "auto", "displayed override honors new-choice precedence over both legacy lists");
    const auto restored = after; auto white = before; white.whitelist = true;
    check(!ProgramRouting::restore(white, "browser.exe", &after, &error) && after == restored, "whitelist cannot silently become site routing");
    for (const auto &name : {"*.exe", "", "C:/browser.exe", "browser.exe\n"})
        check(!ProgramRouting::restore(before, name, &after, &error), "reject invalid program selector");
    auto future = before; future.applications = "{\"schema\":2,\"programs\":{}}";
    check(!ProgramRouting::restore(future, "browser.exe", &after, &error), "unknown schema blocks partial repair");
    check(ProgramRouting::restore(before, "unlisted.exe", &after, &error) && after == before, "unlisted process is a byte-preserving no-op");
    const ProgramRouting::State nativeProcesses{"Google Chrome\nfirefox", {}, {}, false};
    check(ProgramRouting::restore(nativeProcesses, "Google Chrome", &after, &error) && after.bypass == "firefox", "legacy native process names remain editable on macOS");
    ProgramRouting::State robloxSmart,browserSite;
    check(ProgramRouting::choose(before,"RobloxStudioBeta.exe","smart",&robloxSmart,&error)
        && ProgramRouting::restore(robloxSmart,"browser.exe",&browserSite,&error)
        && browserSite.robloxSplit==robloxSmart.robloxSplit
        && ProgramRouting::overrideMode(browserSite,"RobloxStudioBeta.exe")=="smart","browser site recovery preserves Roblox automatic split and its fallback");
    QTemporaryDir files; const auto path = files.filePath("settings.json"), route = files.filePath("Default");
    QJsonObject settings{{"vpn_bypass_process", before.bypass}, {"vpn_proxy_process", before.proxy}, {"game_app_rules", before.applications},
        {"vpn_rule_white", false}, {"vpn_gateway_exclusions", "work.invalid"}, {"custom_route", "keep exactly"}, {"unknown_future", QJsonArray{1, 2}}, {"remote_dns", "keep DNS"}};
    const auto original = QJsonDocument(settings).toJson(); write(path, original); write(route, "original route bytes\n");
    check(ProgramRouting::saveSetting(path, before, "browser.exe", &after, &error), "atomic selected-browser save");
    auto saved = QJsonDocument::fromJson(read(path)).object();
    for (const auto *key : {"vpn_bypass_process", "vpn_proxy_process", "game_app_rules"}) { saved.remove(key); settings.remove(key); }
    check(saved == settings && read(route) == "original route bytes\n", "retain DNS, gateways, unknown settings and route bytes");
    const auto backups = QDir(files.filePath(".program-routing-backups")).entryList({"before-*.json"}, QDir::Files);
    check(backups.size() == 1 && read(files.filePath(".program-routing-backups/" + backups.first())) == original, "exact backup exists before replacement");
    const auto savedBytes = read(path);
    const auto beforeNoOp = after;
    check(!ProgramRouting::saveSetting(path, before, "browser.exe", &after, &error) && read(path) == savedBytes, "stale editor leaves new preferences intact");
    check(ProgramRouting::saveSetting(path, beforeNoOp, "unlisted.exe", &after, &error) && read(path) == savedBytes, "no-op save preserves file bytes");
    write(path, original);
#ifdef _WIN32
    const auto native = path.toStdWString();
    const auto held = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(held != INVALID_HANDLE_VALUE, "hold file against replacement on Windows");
    if (held != INVALID_HANDLE_VALUE) {
        check(!ProgramRouting::saveSetting(path, before, "browser.exe", &after, &error) && read(path) == original, "locked atomic save preserves original settings");
        CloseHandle(held);
        check(ProgramRouting::saveSetting(path, before, "browser.exe", &after, &error), "retry after releasing file lock");
    }
#endif
    write(path, "broken JSON");
    check(!ProgramRouting::saveSetting(path, before, "browser.exe", &after, &error) && read(path) == "broken JSON", "damaged JSON is not replaced");
    write(path, "{\"vpn_bypass_process\":123}");
    check(!ProgramRouting::saveSetting(path, {}, "browser.exe", &after, &error), "wrong-typed setting is not treated as absent");
    std::printf("%d checks; %d failures\n", checks, failures); return failures ? 1 : 0;
}
