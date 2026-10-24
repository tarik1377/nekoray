#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QFile>
#include <QSaveFile>

namespace GreenRhythm::GameAppRules {

// This independent preference never rewrites a route scheme or the legacy TUN
// inclusion/exclusion lists (whose meaning changes in whitelist mode).
inline bool executableName(const QString &name) {
    if (name != name.trimmed() || name.size() < 5 || name.size() > 255
        || !name.endsWith(".exe", Qt::CaseInsensitive)) return false;
    for (const auto ch : name)
        if (ch.unicode() < 32 || QStringLiteral("/\\:*?\"<>|").contains(ch)) return false;
    return name != ".exe" && !name.startsWith('#');
}

inline bool parse(const QString &text, QJsonObject *programs, QString *error = nullptr) {
    auto fail = [&] {
        if (error) *error = QStringLiteral("Не удалось прочитать выбор программ. Сохранённые правила не изменены.");
        return false;
    };
    if (text.trimmed().isEmpty()) { *programs = {}; return true; }
    if (text.toUtf8().size() > 65536) return fail();
    QJsonParseError jsonError;
    const auto document = QJsonDocument::fromJson(text.toUtf8(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject()) return fail();
    const auto root = document.object();
    if (root.value("schema") != 1 || !root.value("programs").isObject() || root.size() != 2) return fail();
    const auto entries = root.value("programs").toObject();
    if (entries.size() > 256) return fail();
    QSet<QString> seen;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (!executableName(it.key()) || seen.contains(it.key().toCaseFolded())
            || (it.value() != "bypass" && it.value() != "proxy")) return fail();
        seen.insert(it.key().toCaseFolded());
    }
    *programs = entries;
    return true;
}

inline QString serialize(const QJsonObject &programs) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"schema", 1}, {"programs", programs}}).toJson(QJsonDocument::Compact));
}
inline QString mode(const QJsonObject &programs, const QString &name) {
    for (auto it = programs.begin(); it != programs.end(); ++it)
        if (it.key().compare(name, Qt::CaseInsensitive) == 0) return it.value().toString();
    return QStringLiteral("auto");
}
inline bool setMode(QJsonObject *programs, const QString &name, const QString &route) {
    if (!executableName(name) || (route != "auto" && route != "bypass" && route != "proxy")) return false;
    auto copy = *programs;
    for (auto it = copy.begin(); it != copy.end();) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0) it = copy.erase(it);
        else ++it;
    }
    if (route != "auto") {
        if (copy.size() >= 256) return false;
        copy.insert(name, route);
    }
    *programs = copy;
    return true;
}
inline QJsonArray rules(const QJsonObject &programs) {
    QJsonArray direct, proxy;
    for (auto it = programs.begin(); it != programs.end(); ++it) {
        if (it.value() == "bypass") direct.append(it.key());
        else if (it.value() == "proxy") proxy.append(it.key());
    }
    QJsonArray result;
    if (!direct.isEmpty()) result.append(QJsonObject{{"process_name", direct}, {"outbound", "bypass"}});
    if (!proxy.isEmpty()) result.append(QJsonObject{{"process_name", proxy}, {"outbound", "proxy"}});
    return result;
}

inline bool saveSetting(const QString &path, const QString &expected, const QString &replacement, QString *error) {
    QJsonObject checked;
    if (!parse(replacement, &checked, error)) return false;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) { *error = QStringLiteral("Не удалось прочитать настройки: ") + input.errorString(); return false; }
    const auto bytes = input.readAll(); input.close();
    QJsonParseError jsonError;
    const auto document = QJsonDocument::fromJson(bytes, &jsonError);
    if (!document.isObject() || jsonError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("Файл настроек повреждён. Изменения не записаны."); return false;
    }
    auto settings = document.object();
    if ((settings.contains("game_app_rules") && !settings.value("game_app_rules").isString())
        || settings.value("game_app_rules").toString() != expected) {
        *error = QStringLiteral("Выбор программ уже изменился. Закройте окно и откройте его снова."); return false;
    }
    settings.insert("game_app_rules", replacement);
    QSaveFile output(path);
    const auto updated = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(updated) != updated.size() || !output.commit()) {
        *error = QStringLiteral("Не удалось сохранить выбор программ: ") + output.errorString(); return false;
    }
    return true;
}
} // namespace GreenRhythm::GameAppRules
