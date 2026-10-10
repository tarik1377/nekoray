#pragma once

#include <QJsonObject>
#include <QString>

namespace GreenRhythm::VideoRoutes {

inline constexpr int MigrationRevision = 2;

QString appendDefaults(const QString &directDomains);
QJsonObject migrate(const QJsonObject &scheme, const QString &globalRules = {});

enum class FileResult { Unchanged, Updated, Failed };
// Update the loaded scheme only after the replacement has committed to disk.
FileResult migrateFile(const QString &path, const QString &globalRules,
                       QString *loadedDirectDomains = nullptr, QString *error = nullptr);
bool markComplete(const QString &settingsPath, QString *error = nullptr);

} // namespace GreenRhythm::VideoRoutes
