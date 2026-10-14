#include "DefaultRoutes.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>

namespace GreenRhythm::DefaultRoutes {
namespace {
bool readObject(const QString &path, QJsonObject *object, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid JSON object: ") + parseError.errorString();
        return false;
    }
    *object = doc.object();
    return true;
}

bool writeObject(const QString &path, const QJsonObject &object, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const auto bytes = QJsonDocument(object).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
} // namespace

QString removeAdsSelector(const QString &blockDomains) {
    auto lines = blockDomains.split('\n');
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (lines[i].trimmed() == QStringLiteral("geosite:category-ads-all"))
            lines.removeAt(i);
    }
    return lines.join('\n');
}

QJsonObject migrate(const QJsonObject &scheme) {
    auto updated = scheme;
    if (scheme.value("block_domain").isString()) {
        const auto before = scheme.value("block_domain").toString();
        const auto after = removeAdsSelector(before);
        if (before != after) updated.insert("block_domain", after);
    }
    return updated;
}

FileResult migrateFile(const QString &path, QString *loadedBlockDomains, QString *error) {
    QJsonObject before;
    if (!readObject(path, &before, error)) return FileResult::Failed;
    const auto after = migrate(before);
    if (before != after && !writeObject(path, after, error)) return FileResult::Failed;
    // Also synchronize unchanged disk state: an active preset may have inherited
    // its block list when the saved file omitted the property.
    if (loadedBlockDomains && before.value("block_domain").isString())
        *loadedBlockDomains = after.value("block_domain").toString();
    return before == after ? FileResult::Unchanged : FileResult::Updated;
}

bool markComplete(const QString &settingsPath, QString *error) {
    QJsonObject settings;
    if (!readObject(settingsPath, &settings, error)) return false;
    settings.insert("routing_ads_removed", true);
    return writeObject(settingsPath, settings, error);
}
} // namespace GreenRhythm::DefaultRoutes
