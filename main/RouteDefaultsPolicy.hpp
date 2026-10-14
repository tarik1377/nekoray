#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>

namespace GreenRhythm::RouteDefaultsPolicy {
// Increment when the bundled Routing(1) policy changes and requires review.
inline constexpr int Revision = 1;
enum class Mode { SafeUpgrade, ResetRoutes };
struct Preview {
    QJsonObject after;
    QStringList changedFields;
    int addedRules = 0;
    int removedRules = 0;
    Mode mode = Mode::SafeUpgrade;
};
struct Result {
    bool success = false;
    bool rollbackFailed = false;
    bool routeChanged = false;
    bool hasScheme = false; // distinguishes a valid empty scheme from a failed read
    QJsonObject scheme;
    QJsonObject settings;
    QString error;
};

bool needsReview(int reviewedRevision);
Preview preview(const QJsonObject &current, const QJsonObject &preset, Mode mode,
                const QString &globalRules = {});
// Only route JSON is backed up. Profiles and subscription files are never read.
Result apply(const QString &routePath, const QString &settingsPath, const Preview &plan,
             const QByteArray &expectedRouteBytes);
Result keep(const QString &settingsPath);
Result restore(const QString &routePath, const QString &settingsPath);
bool canRestore(const QString &routePath, const QJsonObject &settings);
bool readRoute(const QString &path, QJsonObject *object, QByteArray *bytes = nullptr,
               QString *error = nullptr);
} // namespace GreenRhythm::RouteDefaultsPolicy
