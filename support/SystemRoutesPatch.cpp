// Offline plan/apply tool. No GUI, proxy, adapter or core startup.
#include "main/DefaultRoutes.hpp"
#include "3rdparty/RunGuard.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cstdio>
#include <functional>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

static QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw QString("Cannot read: ") + path;
    return file.readAll();
}
static QJsonObject object(const QByteArray &bytes) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw QString("Expected JSON object");
    return doc.object();
}
static QString hash(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
static void write(const QString &path, const QJsonObject &value) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(value).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw QString("Cannot commit: ") + path;
}
static void refuseRunningGui() {
#ifdef Q_OS_WIN
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw QString("Cannot inspect GUI processes");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        throw QString("Cannot enumerate GUI processes");
    }
    do {
        const QString name = QString::fromWCharArray(entry.szExeFile).toLower();
        if (name == "greenrhythm.exe" || name == "nekoray.exe" || name == "nekobox.exe")
            found = true;
    } while (Process32NextW(snapshot, &entry));
    const DWORD enumerationError = GetLastError();
    CloseHandle(snapshot);
    if (enumerationError != ERROR_NO_MORE_FILES) throw QString("Cannot finish GUI process enumeration");
    if (found) throw QString("GUI is running; no files changed. Close it before applying the reviewed plan.");
#else
    throw QString("Offline apply is only implemented for Windows");
#endif
}
static QJsonObject filePlan(const QString &relative, const QByteArray &bytes, const QJsonObject &set) {
    return {{"file", relative}, {"sha256", hash(bytes)}, {"set", set}};
}
static QJsonObject makePlan(const QString &root) {
    const QDir config(QDir(root).filePath("config"));
    QJsonArray files;
    const auto groups = config.filePath("groups");
    const auto settings = QFile::exists(QDir(groups).filePath("greenrhythm.json"))
        ? QString("groups/greenrhythm.json") : QString("groups/nekobox.json");
    const QDir routes(config.filePath("routes_box"));
    if (!routes.exists()) throw QString("Missing routes directory");
    for (const auto &name : routes.entryList(QDir::Files)) {
        const auto relative = "routes_box/" + name;
        const auto bytes = read(config.filePath(relative));
        const auto before = object(bytes);
        const auto after = GreenRhythm::DefaultRoutes::migrate(before);
        QJsonObject set;
        if (after != before) set.insert("block_domain", after.value("block_domain"));
        files.append(filePlan(relative, bytes, set));
    }
    const auto settingsBytes = read(config.filePath(settings));
    object(settingsBytes);
    // The settings completion marker is committed LAST.
    files.append(filePlan(settings, settingsBytes, {{"vpn_impl", 1}, {"routing_ads_removed", true}}));
    return {{"format", 1}, {"root", QDir(root).absolutePath()}, {"files", files}};
}
static QString validatedPath(const QString &root, const QString &relative) {
    if (QDir::isAbsolutePath(relative) || relative.contains("..") || relative.contains('\\')
        || !(relative.startsWith("routes_box/") || relative == "groups/greenrhythm.json" || relative == "groups/nekobox.json"))
        throw QString("Unsafe relative path");
    const QDir config(QDir(root).filePath("config"));
    const QFileInfo info(config.filePath(relative));
    if (info.isSymLink() || !info.canonicalFilePath().startsWith(config.canonicalPath() + '/'))
        throw QString("Path leaves config directory");
    return info.absoluteFilePath();
}
static void apply(const QJsonObject &plan, const std::function<void()> &checkGui = refuseRunningGui) {
    if (plan.value("format").toInt() != 1 || !plan.value("files").isArray())
        throw QString("Unsupported plan");
    checkGui();
    const auto root = plan.value("root").toString();
    RunGuard guard("greenrhythm" + QDir(root).absolutePath());
    quint64 data = 0;
    if (!guard.tryToRun(&data)) throw QString("GreenRhythm owns this config; no files changed");
    // Recompute the entire manifest: reject missing/new files, changed bytes,
    // edited mutations and a completion marker missing any scheme.
    if (makePlan(root) != plan) throw QString("Stale or modified plan; no files changed. Regenerate after GUI shutdown.");
    // Hold the app's own single-instance guard through the whole transaction.
    // -many bypasses it; additional process checks reject existing -many GUIs.
    const auto files = plan.value("files").toArray();
    for (const auto &item : files) {
        const auto entry = item.toObject();
        const auto path = validatedPath(root, entry.value("file").toString());
        if (hash(read(path)) != entry.value("sha256").toString())
            throw QString("Stale plan; no files changed. Regenerate after GUI shutdown: ") + path;
    }
    checkGui();
    const auto backup = QDir(root).filePath("system-routes-backup-" + QString::number(QDateTime::currentMSecsSinceEpoch()));
    if (!QDir().mkpath(backup)) throw QString("Cannot create backup");
    for (const auto &item : files) {
        const auto entry = item.toObject();
        if (entry.value("set").toObject().isEmpty()) continue;
        const auto relative = entry.value("file").toString();
        const auto path = validatedPath(root, relative);
        const auto target = QDir(backup).filePath(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(path, target)) throw QString("Cannot back up: ") + path;
    }
    for (const auto &item : files) {
        const auto entry = item.toObject();
        const auto set = entry.value("set").toObject();
        if (set.isEmpty()) continue;
        checkGui();
        const auto path = validatedPath(root, entry.value("file").toString());
        const auto bytes = read(path);
        if (hash(bytes) != entry.value("sha256").toString())
            throw QString("Config changed while applying; stop and regenerate plan: ") + path;
        auto value = object(bytes);
        for (auto i = set.begin(); i != set.end(); ++i) {
            if (i.key() != "block_domain" && i.key() != "vpn_impl" && i.key() != "routing_ads_removed")
                throw QString("Unexpected mutation");
            value.insert(i.key(), i.value());
        }
        write(path, value);
    }
    std::printf("Applied System + exact ads removal; backup: %s\n", backup.toUtf8().constData());
}
#ifndef SYSTEM_ROUTES_PATCH_TEST
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 2 && args[1] == "--check-closed") {
            refuseRunningGui();
            std::printf("No supported GUI process is running.\n");
        } else if (args.size() == 4 && args[1] == "--plan") {
            write(args[3], makePlan(args[2]));
            std::printf("Plan prepared; user config unchanged.\n");
        } else if (args.size() == 3 && args[1] == "--apply") {
            apply(object(read(args[2])));
        } else {
            throw QString("Usage: system_routes_patch --plan ROOT PLAN.json | --apply PLAN.json | --check-closed");
        }
    } catch (const QString &error) {
        std::fprintf(stderr, "%s\n", error.toUtf8().constData());
        return 1;
    }
    return 0;
}
#endif
