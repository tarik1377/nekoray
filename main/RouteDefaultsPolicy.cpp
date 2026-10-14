#include "RouteDefaultsPolicy.hpp"
#include "DefaultRoutes.hpp"
#include "VideoRoutes.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace GreenRhythm::RouteDefaultsPolicy {
namespace {
QByteArray hash(const QByteArray &data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}
bool writeBytes(const QString &path, const QByteArray &data, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
QString backupDirectory(const QString &routePath) {
    QDir routes(QFileInfo(routePath).absolutePath());
    return QDir::cleanPath(routes.absoluteFilePath("../route_backups"));
}
bool validSchemePath(const QString &routePath) {
    const QFileInfo route(routePath);
    const QFileInfo directory(route.absolutePath());
    return directory.fileName() == QStringLiteral("routes_box") && !directory.isSymLink()
           && !route.isSymLink() && !route.fileName().isEmpty();
}
QString backupPath(const QString &routePath, const QString &name) {
    // Settings supply only a basename; they cannot redirect reads to another
    // directory (including profiles) or to another scheme's backup.
    if (name.isEmpty() || QFileInfo(name).fileName() != name
        || name.contains('/') || name.contains('\\') || name.contains(':')) return {};
    return QDir(backupDirectory(routePath)).filePath(name);
}
QStringList rules(const QJsonObject &object) {
    QStringList result;
    for (const auto &field : {"direct_domain", "proxy_domain", "block_domain",
                             "direct_ip", "proxy_ip", "block_ip"}) {
        for (const auto &raw : object.value(field).toString().split('\n')) {
            const auto line = raw.trimmed();
            if (!line.isEmpty() && !line.startsWith('#')) result.append(QString::fromLatin1(field) + ':' + line);
        }
    }
    const auto custom = QJsonDocument::fromJson(object.value("custom").toString().toUtf8()).object();
    for (const auto &rule : custom.value("rules").toArray())
        if (rule.isObject()) result.append("custom:" + QString::fromUtf8(QJsonDocument(rule.toObject()).toJson(QJsonDocument::Compact)));
    return result;
}
bool quicRule(const QJsonObject &rule) {
    if (rule.value("protocol").toString() == "quic") return true;
    if (rule.value("network").toString() != "udp") return false;
    const auto port = rule.value("port");
    if (port.isDouble()) return port.toInt() == 443;
    for (const auto &p : port.toArray()) if (p.toInt() == 443) return true;
    return false;
}
void addMissingGuards(QJsonObject &scheme, const QJsonObject &preset) {
    auto selectors = [](const QString &text) {
        QStringList result;
        for (const auto &line : text.split('\n')) result.append(line.trimmed());
        return result;
    };
    if (scheme.value("def_outbound").toString() != "proxy"
        || !selectors(scheme.value("direct_domain").toString()).contains("domain:ru")
        || !selectors(scheme.value("direct_ip").toString()).contains("geoip:ru")) return;
    const auto text = scheme.value("custom").toString();
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (!text.trimmed().isEmpty() && (error.error != QJsonParseError::NoError
        || !document.isObject() || !document.object().value("rules").isArray())) return;
    auto custom = document.object();
    auto existing = custom.value("rules").toArray();
    for (const auto &value : existing) {
        if (!value.isObject() || quicRule(value.toObject())) return;
    }
    const auto standard = QJsonDocument::fromJson(preset.value("custom").toString().toUtf8()).object();
    bool added = false;
    for (const auto &value : standard.value("rules").toArray()) {
        if (value.isObject() && quicRule(value.toObject())) {
            // Existing process/domain choices retain their original precedence.
            existing.append(value);
            added = true;
        }
    }
    if (added) {
        custom.insert("rules", existing);
        scheme.insert("custom", QString::fromUtf8(QJsonDocument(custom).toJson(QJsonDocument::Compact)));
    }
}
Result commit(const QString &routePath, const QString &settingsPath, const QByteArray &beforeRoute,
              const QByteArray &afterRoute, QJsonObject settings, const QByteArray &beforeSettings) {
    Result result;
    QString error;
    QJsonObject currentRoute, currentSettings;
    QByteArray nowRoute, nowSettings;
    if (!readRoute(routePath, &currentRoute, &nowRoute, &error)
        || !readRoute(settingsPath, &currentSettings, &nowSettings, &error)
        || nowRoute != beforeRoute || nowSettings != beforeSettings) {
        result.error = error.isEmpty() ? QStringLiteral("Настройки изменились. Откройте предпросмотр заново.") : error;
        return result;
    }
    result.routeChanged = beforeRoute != afterRoute;
    if (result.routeChanged && !writeBytes(routePath, afterRoute, &error)) {
        result.error = error;
        return result;
    }
    if (!writeBytes(settingsPath, QJsonDocument(settings).toJson(), &error)) {
        result.error = error;
        if (result.routeChanged && !writeBytes(routePath, beforeRoute, &error)) {
            result.rollbackFailed = true;
            result.error += QStringLiteral("; не удалось вернуть маршруты: ") + error;
            readRoute(routePath, &result.scheme);
        }
        return result;
    }
    result.scheme = QJsonDocument::fromJson(afterRoute).object();
    result.settings = settings;
    result.success = true;
    return result;
}
bool readBackup(const QString &routePath, const QJsonObject &settings, QJsonObject *backup) {
    const auto path = backupPath(routePath, settings.value("routing_defaults_backup").toString());
    if (path.isEmpty() || QFileInfo(path).isSymLink() || !readRoute(path, backup)) return false;
    const auto name = QFileInfo(routePath).fileName();
    const auto before = QByteArray::fromBase64(backup->value("before").toString().toLatin1());
    return backup->value("version").toInt() == 1
           && backup->value("route_name").toString() == name
           && backup->value("before_sha256").toString().toLatin1() == hash(before)
           && QJsonDocument::fromJson(before).isObject();
}
} // namespace

bool readRoute(const QString &path, QJsonObject *object, QByteArray *bytes, QString *error) {
    QFile file(path);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const auto data = file.readAll();
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Повреждённый файл маршрутов или настроек.");
        return false;
    }
    if (object) *object = doc.object();
    if (bytes) *bytes = data;
    return true;
}

bool needsReview(int reviewedRevision) { return reviewedRevision < Revision; }

Preview preview(const QJsonObject &current, const QJsonObject &preset, Mode mode, const QString &globalRules) {
    Preview result;
    result.mode = mode;
    result.after = current;
    if (mode == Mode::SafeUpgrade) {
        result.after = DefaultRoutes::repairDns(current);
        result.after = VideoRoutes::migrate(result.after, globalRules);
        addMissingGuards(result.after, preset);
    } else {
        // Reset the scheme's route/public DNS fields, while retaining unknown
        // fields and scoped work DNS/hosts. Global routes are a separate policy.
        for (const auto &field : {"direct_ip", "direct_domain", "proxy_ip", "proxy_domain",
                                 "block_ip", "block_domain", "def_outbound", "custom",
                                 "remote_dns", "remote_dns_strategy", "direct_dns", "direct_dns_strategy",
                                 "dns_routing", "use_dns_object", "dns_object", "dns_final_out",
                                 "sniffing_mode", "domain_strategy", "outbound_domain_strategy"}) {
            if (preset.contains(field)) result.after.insert(field, preset.value(field));
        }
    }
    for (const auto &key : result.after.keys())
        if (current.value(key) != result.after.value(key)) result.changedFields.append(key);
    auto oldRules = rules(current), newRules = rules(result.after);
    for (const auto &rule : newRules) {
        const auto at = oldRules.indexOf(rule);
        if (at < 0) ++result.addedRules;
        else oldRules.removeAt(at);
    }
    result.removedRules = oldRules.size();
    return result;
}

Result keep(const QString &settingsPath) {
    Result result;
    QByteArray original;
    if (!readRoute(settingsPath, &result.settings, &original, &result.error)) return result;
    result.settings.insert("routing_defaults_review_revision", Revision);
    result.settings.insert("routing_defaults_choice", QStringLiteral("keep"));
    result.success = writeBytes(settingsPath, QJsonDocument(result.settings).toJson(), &result.error);
    return result;
}

Result apply(const QString &routePath, const QString &settingsPath, const Preview &plan,
             const QByteArray &expectedRouteBytes) {
    Result failure;
    if (!validSchemePath(routePath)) {
        failure.error = QStringLiteral("Выберите сохранённую схему из каталога маршрутов.");
        return failure;
    }
    QJsonObject route, settings;
    QByteArray before, beforeSettings;
    if (!readRoute(routePath, &route, &before, &failure.error)
        || before != expectedRouteBytes
        || !readRoute(settingsPath, &settings, &beforeSettings, &failure.error)) {
        if (failure.error.isEmpty()) failure.error = QStringLiteral("Маршруты изменились. Откройте предпросмотр заново.");
        return failure;
    }
    const auto after = route == plan.after ? before : QJsonDocument(plan.after).toJson();
    const auto directory = backupDirectory(routePath);
    if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)) {
        failure.error = QStringLiteral("Не удалось создать резервную копию маршрутов.");
        return failure;
    }
    const auto name = QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json";
    const QJsonObject backup{{"version", 1}, {"route_name", QFileInfo(routePath).fileName()},
                             {"before", QString::fromLatin1(before.toBase64())},
                             {"before_sha256", QString::fromLatin1(hash(before))},
                             {"after_sha256", QString::fromLatin1(hash(after))}};
    if (!writeBytes(QDir(directory).filePath(name), QJsonDocument(backup).toJson(), &failure.error)) return failure;
    settings.insert("routing_defaults_review_revision", Revision);
    settings.insert("routing_defaults_choice", plan.mode == Mode::SafeUpgrade ? "upgrade" : "reset");
    settings.insert("routing_defaults_backup", name);
    settings.insert("active_routing", QFileInfo(routePath).fileName());
    return commit(routePath, settingsPath, before, after, settings, beforeSettings);
}

bool canRestore(const QString &routePath, const QJsonObject &settings) {
    QJsonObject backup;
    return validSchemePath(routePath) && readBackup(routePath, settings, &backup);
}

Result restore(const QString &routePath, const QString &settingsPath) {
    Result failure;
    if (!validSchemePath(routePath)) {
        failure.error = QStringLiteral("Выберите сохранённую схему из каталога маршрутов.");
        return failure;
    }
    QJsonObject route, settings, backup;
    QByteArray before, beforeSettings;
    if (!readRoute(routePath, &route, &before, &failure.error)
        || !readRoute(settingsPath, &settings, &beforeSettings, &failure.error)) return failure;
    if (!readBackup(routePath, settings, &backup)) {
        failure.error = QStringLiteral("Резервная копия этой схемы недоступна.");
        return failure;
    }
    if (hash(before) != backup.value("after_sha256").toString().toLatin1()) {
        failure.error = QStringLiteral("После применения вы меняли маршруты. Возврат отменён, чтобы сохранить эти правки.");
        return failure;
    }
    const auto restored = QByteArray::fromBase64(backup.value("before").toString().toLatin1());
    settings.insert("routing_defaults_review_revision", Revision);
    settings.insert("routing_defaults_choice", QStringLiteral("restored"));
    settings.remove("routing_defaults_backup");
    settings.insert("active_routing", QFileInfo(routePath).fileName());
    return commit(routePath, settingsPath, before, restored, settings, beforeSettings);
}
} // namespace GreenRhythm::RouteDefaultsPolicy
