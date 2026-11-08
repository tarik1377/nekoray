#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUrl>
#include <algorithm>

namespace GreenRhythm::SiteRouting {
struct Entry {
    QString pattern, mode;
};
struct State { bool enabled = false; QList<Entry> entries; };

inline bool pattern(const QString &input, QString *result) {
    auto value = input.trimmed().toLower();
    const bool exact = value.startsWith("full:");
    if (exact) value.remove(0, 5);
    else if (value.startsWith("domain:")) value.remove(0, 7);
    if (value.endsWith('.')) value.chop(1);
    const auto host = QString::fromLatin1(QUrl::toAce(value));
    static const QRegularExpression labels(QStringLiteral("^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    QHostAddress ip;
    if (host.isEmpty() || host.size() > 253 || !labels.match(host).hasMatch() || ip.setAddress(host)
        || host.endsWith(".local") || host.endsWith(".lan") || host.endsWith(".localhost")
        || host.endsWith(".internal") || host.endsWith(".home.arpa")) return false;
    // Shared hosting is exact-only: importing one CDN name must never capture
    // another tenant, let alone an entire provider.
    const QStringList shared{"cloudfront.net", "amazonaws.com", "cloudflare.net", "edgesuite.net", "fastly.net", "arkoselabs.com", "llnwi.net"};
    if (shared.contains(host) || host == "co.uk" || host == "co.jp" || host == "com.au") return false;
    bool forceExact = exact;
    for (const auto &provider : shared) if (host.endsWith('.' + provider)) forceExact = true;
    *result = (forceExact ? "full:" : "domain:") + host;
    return true;
}
inline bool fromLists(bool enabled, const QString &automatic, const QString &direct, const QString &proxy, State *state, QString *error) {
    State candidate; candidate.enabled = enabled;
    QMap<QString, QString> unique;
    const QList<QPair<QString, QString>> lists{{automatic,"auto"},{direct,"direct"},{proxy,"proxy"}};
    for (const auto &list : lists) {
        for (const auto &raw : list.first.split('\n')) {
            if (raw.trimmed().isEmpty() || raw.trimmed().startsWith('#')) continue;
            QString canonical;
            if (!pattern(raw, &canonical)) {
                if (error) *error = QStringLiteral("Не распознан домен: %1. Используйте domain:имя или full:имя без URL, порта и маски.").arg(raw.left(160));
                return false;
            }
            if (unique.contains(canonical) && unique.value(canonical) != list.second) {
                if (error) *error = QStringLiteral("Для %1 выбраны разные режимы. Оставьте его в одном списке.").arg(canonical);
                return false;
            }
            unique[canonical] = list.second;
            if (unique.size() > 512) { if (error) *error = QStringLiteral("В одном наборе допускается до 512 доменов."); return false; }
        }
    }
    for (auto it = unique.begin(); it != unique.end(); ++it) candidate.entries.append({it.key(), it.value()});
    *state = candidate;
    return true;
}
inline QString serialize(const State &state) {
    QJsonArray rules;
    for (const auto &entry : state.entries) rules.append(QJsonObject{{"pattern",entry.pattern},{"mode",entry.mode}});
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"enabled",state.enabled},{"rules",rules}}).toJson(QJsonDocument::Compact));
}
inline bool parse(const QString &text, State *state, QString *error) {
    if (text.isEmpty()) { *state = {}; return true; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    const auto object = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || object.value("schema") != 1
        || !object.value("enabled").isBool() || !object.value("rules").isArray() || object.value("rules").toArray().size() > 512 || object.size() != 3) {
        if (error) *error = QStringLiteral("Неизвестный формат выбора сайтов. Настройки не изменены."); return false;
    }
    QString automatic, direct, proxy;
    for (const auto &value : object.value("rules").toArray()) {
        const auto rule = value.toObject();
        if (rule.size() != 2 || !rule.value("pattern").isString() || !rule.value("mode").isString()) {
            if (error) *error = QStringLiteral("Повреждён список сайтов."); return false;
        }
        const auto mode = rule.value("mode").toString(), name = rule.value("pattern").toString();
        if (name.contains('\n') || name.contains('\r')) { if(error) *error = QStringLiteral("Повреждён домен."); return false; }
        if (mode == "auto") automatic += name + '\n';
        else if (mode == "direct") direct += name + '\n';
        else if (mode == "proxy") proxy += name + '\n';
        else { if (error) *error = QStringLiteral("Неизвестный режим сайта."); return false; }
    }
    return fromLists(object.value("enabled").toBool(), automatic, direct, proxy, state, error);
}
inline QJsonArray rules(State state, const QString &proxy, bool exported) {
    QJsonArray result;
    if (!state.enabled) return result;
    // The most specific match wins across modes; an exact host beats a suffix.
    std::sort(state.entries.begin(), state.entries.end(), [](const Entry &a, const Entry &b) {
        const auto ah = a.pattern.mid(a.pattern.indexOf(':') + 1), bh = b.pattern.mid(b.pattern.indexOf(':') + 1);
        if (ah.size() != bh.size()) return ah.size() > bh.size();
        return a.pattern.startsWith("full:") && !b.pattern.startsWith("full:");
    });
    for (const auto &entry : state.entries) {
        const bool exact = entry.pattern.startsWith("full:");
        QJsonObject rule{{exact ? "domain" : "domain_suffix", QJsonArray{entry.pattern.mid(exact ? 5 : 7)}},
                         {"outbound",entry.mode == "direct" ? QString("bypass") : entry.mode == "proxy" || exported ? proxy : QString("site-auto")}};
        if (entry.mode == "auto") { rule["network"] = "tcp"; rule["port"] = QJsonArray{443}; }
        result.append(rule);
    }
    return result;
}
inline bool save(const QString &path, const QString &expected, const QString &replacement, QString *error) {
    State checked;
    if (!parse(replacement, &checked, error)) return false;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) { if(error) *error = QStringLiteral("Не удалось прочитать настройки."); return false; }
    const auto original = input.readAll(); input.close();
    QJsonParseError jsonError; const auto document = QJsonDocument::fromJson(original, &jsonError);
    if (!document.isObject() || jsonError.error != QJsonParseError::NoError) { if(error) *error = QStringLiteral("Файл настроек повреждён."); return false; }
    auto settings = document.object();
    if ((settings.contains("site_routing") && !settings.value("site_routing").isString()) || settings.value("site_routing").toString() != expected) {
        if(error) *error = QStringLiteral("Список уже изменён. Откройте окно снова."); return false;
    }
    if (expected == replacement) return true;
    const auto directory = QFileInfo(path).absoluteDir().filePath(".site-routing-backups");
    if (!QDir().mkpath(directory)) { if(error) *error = QStringLiteral("Не удалось создать резервную копию."); return false; }
    QTemporaryFile backup(directory + "/before-XXXXXX.json");
    if (!backup.open() || backup.write(original) != original.size() || !backup.flush()) { if(error) *error = QStringLiteral("Не удалось сохранить резервную копию."); return false; }
    backup.setAutoRemove(false); backup.close();
    settings.insert("site_routing", replacement);
    QSaveFile output(path); output.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) { if(error) *error = QStringLiteral("Не удалось сохранить список сайтов."); return false; }
    return true;
}
} // namespace GreenRhythm::SiteRouting
