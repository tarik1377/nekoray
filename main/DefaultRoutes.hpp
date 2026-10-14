#pragma once

#include <QJsonObject>
#include <QString>

namespace GreenRhythm::DefaultRoutes {

// Only remove the inherited selector from the block list. Keep custom JSON,
// order, comments, unrelated selectors and unknown scheme fields intact.
QString removeAdsSelector(const QString &blockDomains);
QJsonObject migrate(const QJsonObject &scheme);
enum class FileResult { Unchanged, Updated, Failed };
FileResult migrateFile(const QString &path, QString *loadedBlockDomains = nullptr,
                       QString *error = nullptr);
bool markComplete(const QString &settingsPath, QString *error = nullptr);

// Repair only the complete inherited RU DNS tuple. DNS objects, custom server
// choices, scoped DNS/hosts and all routing fields are preserved.
QJsonObject repairDns(const QJsonObject &scheme);
FileResult repairDnsFile(const QString &path, QString *loadedDirectDns = nullptr,
                         QString *loadedFinalOutbound = nullptr, QString *error = nullptr);
bool markDnsComplete(const QString &settingsPath, QString *error = nullptr);

} // namespace GreenRhythm::DefaultRoutes
