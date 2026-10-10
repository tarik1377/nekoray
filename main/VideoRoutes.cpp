#include "VideoRoutes.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

namespace GreenRhythm::VideoRoutes {
namespace {

struct Target { QString host; bool suffix; };
const QList<Target> twitch{{QStringLiteral("twitch.tv"), true},
                          {QStringLiteral("ttvnw.net"), true}};
const QList<Target> anistar{{QStringLiteral("sfv.an-media.org"), false}};

QStringList lines(const QString &text) {
    QStringList result;
    for (const auto &raw : text.split('\n')) {
        const auto line = raw.trimmed().toLower();
        if (!line.isEmpty() && !line.startsWith('#')) result.append(line);
    }
    return result;
}

QString hostname(QString value) {
    while (value.startsWith('.')) value.remove(0, 1);
    while (value.endsWith('.')) value.chop(1);
    return value;
}

bool within(const QString &host, const QString &suffix) {
    return !suffix.isEmpty() && (host == suffix || host.endsWith('.' + suffix));
}

bool conflicts(const QString &line, const QList<Target> &targets) {
    const int colon = line.indexOf(':');
    const auto kind = colon < 0 ? QStringLiteral("domain") : line.left(colon);
    const auto value = colon < 0 ? line : line.mid(colon + 1);
    if (kind == "geosite" && value == "category-ads-all") return false;
    for (const auto &target : targets) {
        if (kind == "full") {
            if (target.suffix ? within(hostname(value), target.host) : hostname(value) == target.host) return true;
        } else if (kind == "domain") {
            if (within(target.host, hostname(value)) || (target.suffix && within(hostname(value), target.host))) return true;
        } else if (kind == "keyword") {
            // A suffix includes arbitrary subdomains. An unrelated-looking keyword
            // can still match one of them; do not widen a curated exception.
            if (target.suffix || target.host.contains(value)) return true;
        } else if (kind == "regexp") {
            const QRegularExpression expression(value);
            if (target.suffix || !expression.isValid() || expression.match(target.host).hasMatch()) return true;
        } else {
            // External/geosite selectors cannot be resolved safely by a migration.
            return true;
        }
    }
    return false;
}

bool customConflict(const QJsonObject &rule, const QList<Target> &targets) {
    if (rule.value("invert").toBool()) return true;
    for (const auto &value : rule.value("rules").toArray()) {
        if (!value.isObject() || customConflict(value.toObject(), targets)) return true;
    }
    for (const auto &field : {QStringLiteral("domain"), QStringLiteral("domain_suffix"),
                              QStringLiteral("domain_keyword"), QStringLiteral("domain_regex"),
                              QStringLiteral("geosite"), QStringLiteral("rule_set")}) {
        if (!rule.contains(field)) continue;
        const auto values = rule[field].isArray() ? rule[field].toArray() : QJsonArray{rule[field]};
        if (field == "rule_set") return true;
        QString prefix = "full:";
        if (field == "domain_suffix") prefix = "domain:";
        if (field == "domain_keyword") prefix = "keyword:";
        if (field == "domain_regex") prefix = "regexp:";
        if (field == "geosite") prefix = "geosite:";
        for (const auto &value : values) {
            if (!value.isString() || conflicts(prefix + value.toString().toLower(), targets)) return true;
        }
    }
    return false;
}

bool customAllows(const QString &custom, const QList<Target> &targets) {
    if (custom.trimmed().isEmpty()) return true;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(custom.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || !document.object().value("rules").isArray()) return false;
    for (const auto &rule : document.object().value("rules").toArray()) {
        if (!rule.isObject() || customConflict(rule.toObject(), targets)) return false;
    }
    return true;
}

bool allowed(const QJsonObject &scheme, const QString &global, const QList<Target> &targets) {
    for (const auto &field : {QStringLiteral("proxy_domain"), QStringLiteral("block_domain")}) {
        for (const auto &line : lines(scheme.value(field).toString())) {
            if (conflicts(line, targets)) return false;
        }
    }
    return customAllows(scheme.value("custom").toString(), targets) && customAllows(global, targets);
}

bool covers(const QString &direct, const Target &target) {
    for (const auto &line : lines(direct)) {
        const int colon = line.indexOf(':');
        const auto kind = colon < 0 ? QStringLiteral("domain") : line.left(colon);
        const auto value = hostname(colon < 0 ? line : line.mid(colon + 1));
        if (kind == "domain" && within(target.host, value)) return true;
        if (!target.suffix && kind == "full" && value == target.host) return true;
    }
    return false;
}

QString append(QString direct, const QList<Target> &targets) {
    for (const auto &target : targets) {
        if (covers(direct, target)) continue;
        if (!direct.isEmpty() && !direct.endsWith('\n')) direct += '\n';
        direct += (target.suffix ? "domain:" : "full:") + target.host;
    }
    return direct;
}

bool readObject(const QString &path, QJsonObject *object, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Invalid JSON object");
        return false;
    }
    *object = document.object();
    return true;
}

bool writeObject(const QString &path, const QJsonObject &object, QString *error) {
    QSaveFile file(path);
    const auto data = QJsonDocument(object).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace

QString appendDefaults(const QString &directDomains) {
    return append(append(directDomains, twitch), anistar);
}

QJsonObject migrate(const QJsonObject &scheme, const QString &globalRules) {
    // Only the RU preset receives new direct defaults. A custom full-tunnel scheme
    // stays as chosen. A scheme already routing Twitch direct gets its media too.
    // A new domain rule could precede an existing IP choice. CDN addresses change,
    // so do not try to prove an IP selector unrelated to these services.
    if (!lines(scheme.value("proxy_ip").toString()).isEmpty()
        || !lines(scheme.value("block_ip").toString()).isEmpty()) return scheme;
    const auto direct = scheme.value("direct_domain").toString();
    const bool ruPreset = lines(direct).contains("domain:ru")
                          && lines(scheme.value("direct_ip").toString()).contains("geoip:ru")
                          && scheme.value("def_outbound").toString() == "proxy";
    const bool twitchDirect = covers(direct, twitch.first());
    QString updated = direct;
    if ((ruPreset || twitchDirect) && allowed(scheme, globalRules, twitch)) updated = append(updated, twitch);
    if (ruPreset && allowed(scheme, globalRules, anistar)) updated = append(updated, anistar);
    auto result = scheme;
    if (updated != direct) result.insert("direct_domain", updated);
    return result;
}

FileResult migrateFile(const QString &path, const QString &globalRules,
                       QString *loadedDirectDomains, QString *error) {
    QJsonObject original;
    if (!readObject(path, &original, error)) return FileResult::Failed;
    if (loadedDirectDomains && *loadedDirectDomains != original.value("direct_domain").toString()) {
        if (error) *error = QStringLiteral("Loaded routing differs from its file");
        return FileResult::Failed;
    }
    const auto updated = migrate(original, globalRules);
    if (updated == original) return FileResult::Unchanged;
    if (!writeObject(path, updated, error)) return FileResult::Failed;
    if (loadedDirectDomains) *loadedDirectDomains = updated.value("direct_domain").toString();
    return FileResult::Updated;
}

bool markComplete(const QString &settingsPath, QString *error) {
    QJsonObject settings;
    if (!readObject(settingsPath, &settings, error)) return false;
    settings.insert("routing_video_migrated", true);
    return writeObject(settingsPath, settings, error);
}

} // namespace GreenRhythm::VideoRoutes
