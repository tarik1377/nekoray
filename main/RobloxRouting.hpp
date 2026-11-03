#pragma once
#include "GameAppRules.hpp"

namespace GreenRhythm::RobloxRouting {
inline QStringList processes() { return {"RobloxPlayerBeta.exe", "RobloxStudioBeta.exe"}; }
inline bool contains(const QString &name) { return processes().contains(name,Qt::CaseInsensitive); }
inline bool parse(const QString &text, QStringList *names, QString *error=nullptr) {
    QStringList result;
    if(text.size()>1024) { if(error) *error=QStringLiteral("Повреждён автовыбор Roblox."); return false; }
    for(const auto &line:text.split('\n')) {
        const auto name=line.trimmed(); if(name.isEmpty()) continue;
        if(!contains(name) || result.contains(name,Qt::CaseInsensitive)) {
            if(error) *error=QStringLiteral("Не удалось прочитать автовыбор Roblox. Настройки не изменены.");
            return false;
        }
        for(const auto &canonical:processes()) if(canonical.compare(name,Qt::CaseInsensitive)==0) result.append(canonical);
    }
    *names=result; return true;
}
// game_app_rules retains a compatible full-VPN fallback for older clients.
// A later manual DIRECT choice takes precedence over a leftover split marker.
inline QStringList active(const QStringList &names,const QJsonObject &programs) {
    QStringList result;
    for(const auto &name:names) if(GameAppRules::mode(programs,name)=="proxy") result.append(name);
    return result;
}
inline QJsonObject udpRule(const QStringList &names) {
    QJsonArray array; for(const auto &name:names) array.append(name);
    return {{"process_name",array},{"network","udp"},{"outbound","bypass"}};
}
inline bool isUdpRule(const QJsonObject &rule) {
    if(rule.size()!=3 || rule.value("network")!="udp" || rule.value("outbound")!="bypass" || !rule.value("process_name").isArray()) return false;
    const auto names=rule.value("process_name").toArray();
    if(names.isEmpty()) return false;
    for(const auto &name:names) if(!name.isString() || !contains(name.toString())) return false;
    return true;
}
}
