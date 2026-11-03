#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <functional>

namespace GreenRhythm::UserData {
struct StorageOptions { bool appData = true; QString path, error; };
inline StorageOptions windowsStorage(const QStringList &arguments, bool portableMarker) {
    StorageOptions options;
    const bool explicitAppData = arguments.contains("-appdata"), portable = arguments.contains("-portable");
    if ((explicitAppData && portable) || arguments.count("-appdata") > 1 || arguments.count("-portable") > 1) {
        options.error = QStringLiteral("Выберите один режим хранения: -appdata или -portable."); return options;
    }
    options.appData = explicitAppData || (!portable && !portableMarker);
    const int index = arguments.indexOf("-appdata");
    if (index >= 0 && index + 1 < arguments.size() && !arguments.at(index + 1).startsWith('-')) options.path = arguments.at(index + 1);
    return options;
}
inline bool hasProfiles(const QString &config) {
    return QFileInfo::exists(config + "/device.dat")
        || !QDir(config + "/profiles").entryList({"*.json"}, QDir::Files).isEmpty()
        || QFileInfo::exists(config + "/groups/greenrhythm.json");
}
inline QString legacyConfigDirectory(const QString &executable, const QStringList &arguments, const QString &standardData) {
    const auto install = QFileInfo(executable).absoluteDir();
    const int index = arguments.indexOf("-appdata");
    if (index < 0) return install.absoluteFilePath("config");
    auto root = standardData;
    if (index + 1 < arguments.size() && !arguments.at(index + 1).startsWith('-')) root = arguments.at(index + 1);
    if (QDir::isRelativePath(root)) root = install.absoluteFilePath(root);
    return QDir::cleanPath(QDir(root).absoluteFilePath("config"));
}
inline bool emptyDirectory(const QString &path) {
    return QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty();
}
inline bool linked(const QFileInfo &info) {
    return info.isSymLink() || info.isShortcut() || info.isJunction();
}
inline bool inside(const QString &root, const QString &path) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return path.compare(root, sensitivity) == 0 || path.startsWith(root + '/', sensitivity);
}
inline QString resolvedFuturePath(const QString &path) {
    const QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    if (info.exists() || info.isSymLink()) return info.canonicalFilePath();
    const auto parent = info.absolutePath();
    if (parent == info.absoluteFilePath()) return {};
    const auto canonicalParent = resolvedFuturePath(parent);
    return canonicalParent.isEmpty() ? QString() : QDir(canonicalParent).filePath(info.fileName());
}
enum class Migration { NoSource, Existing, Copied, Failed };
// Publish a complete verified copy. The old config remains a rollback source;
// existing AppData is authoritative and is never merged with another copy.
inline Migration migrate(const QString &source, const QString &dataRoot, QString *error,
                         const std::function<void()> &beforePublish = {}) {
    const auto fail = [&](const QString &text) {
        if (error) *error = text;
        return Migration::Failed;
    };
    const auto target = QDir(dataRoot).absoluteFilePath("config");
    if (QDir(target).exists() && !emptyDirectory(target)) return Migration::Existing;
    if (!QDir(source).exists() || emptyDirectory(source)) return Migration::NoSource;
    const auto origin = QFileInfo(source).canonicalFilePath();
    const auto futureRoot = resolvedFuturePath(dataRoot);
    if (origin.isEmpty() || futureRoot.isEmpty()) return fail(QStringLiteral("Не удалось открыть каталог пользовательских данных."));
    const auto destination = QDir::cleanPath(futureRoot + "/config");
    if (inside(origin, destination) || inside(destination, origin))
        return fail(QStringLiteral("Каталоги переноса данных пересекаются. Исходные данные сохранены."));
    if (!QDir().mkpath(dataRoot)) return fail(QStringLiteral("Не удалось создать каталог пользовательских данных."));
    QTemporaryDir staging(QDir(dataRoot).absoluteFilePath(".profile-migration-XXXXXX"));
    if (!staging.isValid()) return fail(QStringLiteral("Не удалось создать временный каталог миграции."));
    const auto staged = staging.path() + "/config";
    QMap<QString, QByteArray> hashes;
    QSet<QString> visited;
    std::function<bool(const QString &)> copy = [&](const QString &relative) {
        const auto from = relative.isEmpty() ? source : QDir(source).filePath(relative);
        const QFileInfo directory(from);
        const auto canonical = directory.canonicalFilePath();
        if (linked(directory) || !inside(origin, canonical) || visited.contains(canonical)) return false;
        visited.insert(canonical);
        if (!QDir().mkpath(QDir(staged).filePath(relative))) return false;
        const QDir listing(from);
        for (const auto &entry : listing.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
            if (relative.isEmpty() && (entry.fileName() == "temp" || entry.fileName() == "neko.log")) continue;
            const auto name = relative.isEmpty() ? entry.fileName() : relative + '/' + entry.fileName();
            if (linked(entry) || !inside(origin, entry.canonicalFilePath())) return false;
            if (entry.isDir()) { if (!copy(name)) return false; continue; }
            if (!entry.isFile()) return false;
            QFile input(entry.absoluteFilePath());
            if (!input.open(QIODevice::ReadOnly)) return false;
            QCryptographicHash hash(QCryptographicHash::Sha256);
            QSaveFile output(QDir(staged).filePath(name)); output.setDirectWriteFallback(false);
            if (!output.open(QIODevice::WriteOnly)) return false;
            output.setPermissions(entry.permissions());
            while (!input.atEnd()) {
                const auto block = input.read(128 * 1024);
                if (input.error() != QFileDevice::NoError || output.write(block) != block.size()) return false;
                hash.addData(block);
            }
            if (!output.commit()) return false;
            QFile verify(QDir(staged).filePath(name));
            QCryptographicHash copied(QCryptographicHash::Sha256);
            if (!verify.open(QIODevice::ReadOnly) || !copied.addData(&verify) || copied.result() != hash.result()) return false;
            hashes.insert(name, hash.result());
        }
        return true;
    };
    if (!copy({})) return fail(QStringLiteral("Не удалось скопировать профили и настройки. Исходная папка сохранена; пустая конфигурация не будет запущена."));
    if (beforePublish) beforePublish();
    // A still-running legacy client must not race a migration snapshot.
    QSet<QString> currentFiles, checkedDirectories;
    std::function<bool(const QString &)> inventory = [&](const QString &relative) {
        const auto path = QDir(source).filePath(relative);
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (!inside(origin, canonical) || checkedDirectories.contains(canonical)) return false;
        checkedDirectories.insert(canonical);
        for (const auto &entry : QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
            if (relative.isEmpty() && (entry.fileName() == "temp" || entry.fileName() == "neko.log")) continue;
            const auto name = relative.isEmpty() ? entry.fileName() : relative + '/' + entry.fileName();
            if (linked(entry) || !inside(origin, entry.canonicalFilePath())) return false;
            if (entry.isDir()) { if (!inventory(name)) return false; }
            else if (entry.isFile()) currentFiles.insert(name);
            else return false;
        }
        return true;
    };
    if (!inventory({}) || currentFiles.size() != hashes.size())
        return fail(QStringLiteral("Набор файлов изменился во время переноса. Закройте прежнюю копию клиента и повторите запуск."));
    for (auto it = hashes.begin(); it != hashes.end(); ++it) {
        if (!currentFiles.contains(it.key())) return fail(QStringLiteral("Набор файлов изменился во время переноса."));
        QFile original(QDir(source).filePath(it.key())); QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!original.open(QIODevice::ReadOnly) || !hash.addData(&original) || hash.result() != it.value())
            return fail(QStringLiteral("Исходные настройки изменились во время переноса. Закройте прежнюю копию клиента и повторите запуск."));
    }
    if (QDir(target).exists() && (!emptyDirectory(target) || !QDir().rmdir(target)))
        return fail(QStringLiteral("В AppData уже появились данные. Перенос отменён без замены файлов."));
    if (!QDir().rename(staged, target)) return fail(QStringLiteral("Не удалось завершить перенос пользовательских данных. Исходные файлы сохранены."));
    return Migration::Copied;
}
inline QString quoteWindowsArgument(const QString &value) {
    QString result = "\""; int slashes = 0;
    for (const auto ch : value) {
        if (ch == '\\') { ++slashes; continue; }
        if (ch == '"') result += QString(slashes * 2 + 1, '\\') + ch;
        else result += QString(slashes, '\\') + ch;
        slashes = 0;
    }
    return result + QString(slashes * 2, '\\') + '"';
}
inline QStringList guiArguments(const QString &dataRoot, bool appData, bool tray = false) {
    QStringList args;
    if (tray) args << "-tray";
    if (appData) args << "-appdata" << QDir::toNativeSeparators(dataRoot);
    else args << "-portable";
    return args;
}
inline QStringList updaterArguments(const QString &install, const QString &configDirectory, bool appData) {
    QStringList args{"--install-dir", install};
    if (appData) args << "--data-dir" << QDir(configDirectory).absoluteFilePath("..");
    else args << "--portable";
    return args;
}
} // namespace GreenRhythm::UserData
