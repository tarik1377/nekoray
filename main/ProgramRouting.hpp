#pragma once

#include "GameAppRules.hpp"
#include "RobloxRouting.hpp"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>

namespace GreenRhythm::ProgramRouting {
struct State {
    QString bypass, proxy, applications;
    bool whitelist = false;
    QString robloxSplit, robloxAdaptive;
    bool operator==(const State &other) const {
        return bypass == other.bypass && proxy == other.proxy
            && applications == other.applications && whitelist == other.whitelist && robloxSplit == other.robloxSplit && robloxAdaptive == other.robloxAdaptive;
    }
};
inline bool listed(const QString &list, const QString &name) {
    for (const auto &line : list.split('\n'))
        if (line.trimmed().compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}
inline QString without(const QString &list, const QString &name) {
    auto lines = list.split('\n');
    for (auto it = lines.begin(); it != lines.end();)
        if (it->trimmed().compare(name, Qt::CaseInsensitive) == 0) it = lines.erase(it);
        else ++it;
    return lines.join('\n');
}
inline QStringList browsers() {
    return {"browser.exe", "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", "opera.exe", "vivaldi.exe", "librewolf.exe"};
}
inline bool isBrowser(const QString &name) { return browsers().contains(name, Qt::CaseInsensitive); }
inline QString overrideMode(const State &state, const QString &name) {
    QJsonObject apps;
    if (!GameAppRules::parse(state.applications, &apps)) return "unknown";
    QStringList split;
    if(!RobloxRouting::parse(state.robloxSplit,&split)) return "unknown";
    QStringList adaptive;
    if(!RobloxRouting::parse(state.robloxAdaptive,&adaptive)) return "unknown";
    const auto mode = GameAppRules::mode(apps, name);
    if(mode=="proxy" && adaptive.contains(name,Qt::CaseInsensitive)) return "adaptive";
    if(mode=="proxy" && split.contains(name,Qt::CaseInsensitive)) return "smart";
    if (mode != "auto") return mode;
    if (listed(state.bypass, name)) return state.whitelist ? "proxy" : "bypass";
    return listed(state.proxy, name) ? QStringLiteral("proxy") : QStringLiteral("auto");
}
inline QStringList browserOverrides(const State &state) {
    QJsonObject apps;
    GameAppRules::parse(state.applications, &apps);
    QStringList result;
    for (const auto &name : browsers())
        if (listed(state.bypass, name) || listed(state.proxy, name) || GameAppRules::mode(apps, name) != "auto") result.append(name);
    return result;
}
inline bool restore(const State &before, const QString &name, State *after, QString *error) {
    // Legacy lists also contain native macOS process names without .exe.
    bool validName = !name.isEmpty() && name == name.trimmed() && name.size() <= 255
        && !name.startsWith('#') && name != "." && name != "..";
    for (const auto ch : name) validName &= ch.unicode() >= 32 && !QStringLiteral("/\\:*?\"<>|").contains(ch);
    if (!validName) {
        if (error) *error = QStringLiteral("Выберите точное имя программы без пути и маски.");
        return false;
    }
    if (before.whitelist) {
        if (error) *error = QStringLiteral("В режиме белого списка удаление программы не возвращает выбор по сайтам. Сначала выберите обычный режим TUN в его настройках.");
        return false;
    }
    QJsonObject apps;
    if (!GameAppRules::parse(before.applications, &apps, error)) return false;
    auto candidate = before;
    QStringList split;
    if(!RobloxRouting::parse(before.robloxSplit,&split,error)) return false;
    if(!RobloxRouting::parse(before.robloxAdaptive,nullptr,error)) return false;
    candidate.robloxAdaptive=without(before.robloxAdaptive,name);
    if(split.contains(name,Qt::CaseInsensitive)) candidate.robloxSplit=without(before.robloxSplit,name);
    candidate.bypass = without(before.bypass, name);
    candidate.proxy = without(before.proxy, name);
    if (GameAppRules::mode(apps, name) != "auto") {
        GameAppRules::setMode(&apps, name, "auto");
        candidate.applications = GameAppRules::serialize(apps);
    }
    *after = candidate;
    return true;
}
inline bool choose(const State &before, const QString &name, const QString &route, State *after, QString *error) {
    if (route == "auto") return restore(before, name, after, error);
    QJsonObject apps;
    if (!GameAppRules::parse(before.applications, &apps, error)) return false;
    QStringList split;
    if(!RobloxRouting::parse(before.robloxSplit,&split,error)) return false;
    if(!RobloxRouting::parse(before.robloxAdaptive,nullptr,error)) return false;
    const bool automatic=route=="smart" || route=="adaptive";
    if ((automatic && !RobloxRouting::contains(name)) || !GameAppRules::setMode(&apps, name, automatic?QStringLiteral("proxy"):route)) {
        if (error) *error = QStringLiteral("Выберите точное имя .exe и направление подключения.");
        return false;
    }
    *after = before;
    split.removeAll(name);
    for(auto it=split.begin();it!=split.end();) if(it->compare(name,Qt::CaseInsensitive)==0) it=split.erase(it);else ++it;
    after->robloxAdaptive=without(before.robloxAdaptive,name);
    if(route=="adaptive") after->robloxAdaptive += (after->robloxAdaptive.isEmpty()?QString():QStringLiteral("\n"))+name;
    if(automatic) {
        if(route=="smart") split.append(name);
        after->bypass=without(before.bypass,name); after->proxy=without(before.proxy,name);
    }
    after->robloxSplit=split.join('\n');
    after->applications = GameAppRules::serialize(apps);
    return true;
}
// Commit the whole process preference snapshot, atomically, with a backup.
// Route schemes, connection mode, DNS and unknown settings remain untouched.
inline bool saveState(const QString &path, const State &expected, const State &candidate, QString *error) {
    QJsonObject apps;
    QStringList split;
    if (candidate.whitelist != expected.whitelist || !GameAppRules::parse(candidate.applications, &apps, error)
        || !RobloxRouting::parse(candidate.robloxSplit,&split,error)
        || !RobloxRouting::parse(candidate.robloxAdaptive,nullptr,error)) {
        if (error && error->isEmpty()) *error = QStringLiteral("Режим TUN не может меняться вместе с выбором программы.");
        return false;
    }
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) { if (error) *error = input.errorString(); return false; }
    const auto bytes = input.readAll(); input.close();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (!document.isObject() || parseError.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("Файл настроек повреждён. Изменения не записаны.");
        return false;
    }
    auto settings = document.object();
    for (const auto *key : {"vpn_bypass_process", "vpn_proxy_process", "game_app_rules", "roblox_split_processes", "roblox_auto_processes"})
        if (settings.contains(key) && !settings.value(key).isString()) {
            if (error) *error = QStringLiteral("Нераспознанный формат правил программ. Изменения не записаны.");
            return false;
        }
    if (settings.contains("vpn_rule_white") && !settings.value("vpn_rule_white").isBool()) {
        if (error) *error = QStringLiteral("Нераспознанный режим TUN. Изменения не записаны.");
        return false;
    }
    const State saved{settings.value("vpn_bypass_process").toString(), settings.value("vpn_proxy_process").toString(),
                      settings.value("game_app_rules").toString(), settings.value("vpn_rule_white").toBool(),settings.value("roblox_split_processes").toString(),settings.value("roblox_auto_processes").toString()};
    if (!(saved == expected)) {
        if (error) *error = QStringLiteral("Правила программ уже изменились. Откройте окно снова.");
        return false;
    }
    if (candidate == expected) return true;
    const auto directory = QFileInfo(path).absoluteDir().filePath(".program-routing-backups");
    if (!QDir().mkpath(directory)) {
        if (error) *error = QStringLiteral("Не удалось создать папку резервной копии. Изменения не записаны.");
        return false;
    }
    QTemporaryFile backup(directory + "/before-XXXXXX.json");
    if (!backup.open() || backup.write(bytes) != bytes.size() || !backup.flush()) {
        if (error) *error = QStringLiteral("Не удалось сохранить резервную копию. Изменения не записаны.");
        return false;
    }
    backup.setAutoRemove(false); backup.close();
    if (candidate.bypass != expected.bypass) settings.insert("vpn_bypass_process", candidate.bypass);
    if (candidate.proxy != expected.proxy) settings.insert("vpn_proxy_process", candidate.proxy);
    if (candidate.applications != expected.applications) settings.insert("game_app_rules", candidate.applications);
    if (candidate.robloxSplit != expected.robloxSplit) settings.insert("roblox_split_processes", candidate.robloxSplit);
    if (candidate.robloxAdaptive != expected.robloxAdaptive) settings.insert("roblox_auto_processes", candidate.robloxAdaptive);
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    const auto updated = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(updated) != updated.size() || !output.commit()) {
        if (error) *error = QStringLiteral("Не удалось сохранить правила: ") + output.errorString();
        return false;
    }
    return true;
}
inline bool saveSetting(const QString &path, const State &expected, const QString &name, State *replacement, QString *error) {
    State candidate;
    if (!restore(expected, name, &candidate, error) || !saveState(path, expected, candidate, error)) return false;
    *replacement = candidate;
    return true;
}
} // namespace GreenRhythm::ProgramRouting
