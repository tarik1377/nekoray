#include "main/UserDataPaths.hpp"
#include "sys/AutoRunTask.hpp"
#include "3rdparty/RunGuard.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

using namespace GreenRhythm;
static void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path); file.open(QIODevice::WriteOnly); file.write(bytes);
}
static QByteArray read(const QString &path) { QFile file(path); file.open(QIODevice::ReadOnly); return file.readAll(); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); int count = 0, failures = 0;
    auto check = [&](bool ok, const char *text) { ++count; if (!ok) ++failures; std::printf("%s %s\n", ok ? "PASS" : "FAIL", text); };
    using UserData::Migration;
    check(UserData::windowsStorage({}, false).appData, "Windows defaults to shared per-user AppData");
    check(!UserData::windowsStorage({"-portable"}, false).appData && !UserData::windowsStorage({}, true).appData, "portable flag and marker retain directory-local profiles");
    const auto explicitRoot = UserData::windowsStorage({"-appdata", "custom data", "-tray"}, true);
    check(explicitRoot.appData && explicitRoot.path == "custom data", "explicit AppData path wins over portable marker");
    check(UserData::windowsStorage({"-appdata", "-tray"}, false).path.isEmpty(), "AppData flag without path uses standard location");
    check(!UserData::windowsStorage({"-appdata", "-portable"}, false).error.isEmpty(), "ambiguous storage flags are rejected");
    check(!UserData::windowsStorage({"-appdata", "one", "-appdata", "two"}, false).error.isEmpty(), "duplicate data roots cannot select an arbitrary profile store");
    QTemporaryDir files; const auto source = files.path() + "/old/config", data = files.path() + "/App Data/greenrhythm";
    const auto oldExe = files.path()+"/old/greenrhythm.exe";
    check(UserData::legacyConfigDirectory(oldExe, {}, data) == source, "legacy portable autorun selects its original config");
    check(UserData::legacyConfigDirectory(oldExe, {"-tray", "-appdata", data}, {}) == data+"/config", "custom AppData from previous autorun is discovered too");
    check(UserData::legacyConfigDirectory(oldExe, {"-appdata"}, data) == data+"/config", "legacy standard AppData is not mistaken for an exe-local profile store");
    {
        const auto key = "fixture-migration-"+files.path();
        RunGuard migration(key), oldCopy(key); quint64 token = 42, observed = 0;
        check(migration.tryToRun(&token) && !oldCopy.tryToRun(&token) && oldCopy.isAnotherRunning(&observed) && observed == 42,
            "retained legacy guard blocks an old executable and forwards the current IPC token");
        migration.release();
        check(oldCopy.tryToRun(&token), "legacy guard is released for a planned restart");
    }
    const QMap<QString, QByteArray> originals{{"profiles/0.json", "profile secret bytes\n"}, {"groups/greenrhythm.json", "{\"unknown\":123}"},
        {"groups/0.json", "subscription secret bytes"}, {"routes_box/Default", "custom rules\r\n"}, {"routes_box/work", "work routes"},
        {"device.dat", QByteArray("sealed\0identity", 15)}, {".geodata/metadata.json", "cached state"}};
    for (auto it = originals.begin(); it != originals.end(); ++it) write(source + '/' + it.key(), it.value());
    write(source + "/temp/core.json", "volatile token"); write(source + "/neko.log", "old logs");
    QString error;
    check(UserData::hasProfiles(source), "legacy profiles and device state are recognized");
    check(UserData::migrate(source, data, &error) == Migration::Copied, "complete legacy config copied into AppData");
    for (auto it = originals.begin(); it != originals.end(); ++it)
        check(read(data + "/config/" + it.key()) == it.value() && read(source + '/' + it.key()) == it.value(), "profiles, routes, identity and unknown data retained byte-for-byte in both stores");
    check(!QFileInfo::exists(data + "/config/temp") && !QFileInfo::exists(data + "/config/neko.log"), "live temporary configs and logs do not migrate");
    write(source + "/profiles/0.json", "newer old copy");
    check(UserData::migrate(source, data, &error) == Migration::Existing && read(data + "/config/profiles/0.json") == originals.value("profiles/0.json"), "existing AppData never merges with or rolls back to another directory");
    check(UserData::migrate(files.path() + "/missing", files.path() + "/fresh", &error) == Migration::NoSource, "fresh installation has no fake migration");
    const auto race = files.path() + "/race";
    check(UserData::migrate(source, race, &error, [&] { write(source + "/profiles/added.json", "new profile"); }) == Migration::Failed
        && !QFileInfo::exists(race + "/config"), "new files created during migration cancel publication instead of dropping profiles");
    check(UserData::migrate(source, race, &error, [&] { write(source + "/groups/greenrhythm.json", "changed settings"); }) == Migration::Failed
        && !QFileInfo::exists(race + "/config"), "source writes during migration cancel publication");
    check(UserData::migrate(source, race, &error) == Migration::Copied && read(race + "/config/profiles/added.json") == "new profile", "failed migration can retry with a complete fresh snapshot");
    check(UserData::migrate(source, source + "/nested", &error) == Migration::Failed, "recursive source/destination overlap is rejected");
    const auto escapeSource = files.path() + "/escape/config", outside = files.path() + "/outside";
    write(escapeSource + "/profiles/0.json", "safe"); write(outside, "do not copy");
    if (QFile::link(outside, escapeSource + "/foreign.lnk")) {
        check(UserData::migrate(escapeSource, files.path() + "/escaped-data", &error) == Migration::Failed, "linked data cannot escape the legacy root");
    }

    const auto guiArgs = UserData::guiArguments("C:/User Data/greenrhythm", true, true);
    check(guiArgs.size() == 3 && guiArgs[0] == "-tray" && guiArgs[1] == "-appdata", "autorun and elevation preserve selected data root");
    check(UserData::guiArguments({}, false).contains("-portable"), "portable mode survives restart and rollback");
    check(UserData::quoteWindowsArgument(QStringLiteral("C:\\data path\\")) == QStringLiteral("\"C:\\data path\\\\\""), "trailing backslash in a quoted Windows path is preserved");

    const QByteArray task = R"(<?xml version="1.0" encoding="UTF-16"?><Task><Principals><Principal><UserId>S-1-5-21-fixture</UserId></Principal></Principals><Actions><Exec><Command>C:\Old Copy\greenrhythm.exe</Command><Arguments>-tray</Arguments><WorkingDirectory>C:\Old Copy</WorkingDirectory></Exec></Actions></Task>)";
    const auto launch = AutoRunTask::parse(task); const QStringList identities{"S-1-5-21-fixture"};
    check(AutoRunTask::owned(launch, identities), "real schtasks-style UTF-16 declaration over text transport remains readable");
    check(AutoRunTask::owned(AutoRunTask::parse(QByteArray::fromHex("efbbbf")+task), identities), "UTF-8 BOM does not invalidate exported task text");
    check(!AutoRunTask::matches(launch, identities, "C:/New Copy/greenrhythm.exe", "C:/New Copy", "-tray"), "existing task pointing to an older exe is stale rather than correct autorun");
    check(AutoRunTask::matches(launch, identities, "c:/old copy/GREENRHYTHM.EXE", "C:/Old Copy/", "-tray"), "Windows path case and separators do not produce false relocation");
    check(!AutoRunTask::owned(launch, {"S-1-5-21-other"}), "another account's task is never owned");
    check(!AutoRunTask::matches(launch, identities, launch.executable, launch.directory, "-tray -appdata data"), "stale data arguments need repair even when exe path matches");
    auto duplicate = task; duplicate.replace("</Actions>", "<Exec><Command>C:/other/greenrhythm.exe</Command></Exec></Actions>");
    check(!AutoRunTask::parse(duplicate).valid && !AutoRunTask::parse("<broken>").valid, "ambiguous or malformed task actions cannot be repaired blindly");
    const auto noForce = AutoRunTask::registerScript("C:/New/greenrhythm.exe", "C:/New", "PC\\user", "-tray -appdata \"C:\\Data Path\"", false);
    check(!noForce.contains(" -Force") && noForce.contains("-appdata"), "unrecognized task cannot be force-replaced and data path is forwarded");
    std::printf("%d checks; %d failures\n", count, failures); return failures ? 1 : 0;
}
