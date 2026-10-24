#pragma once

#include "RuRoutingPolicy.hpp"
#include "GameCatalog.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace GreenRhythm::GameVoiceRouting {

inline QStringList directProcesses() {
    return GameCatalog::processes();
}
inline QStringList voiceProcesses() {
    return {"Discord.exe", "discord.exe", "DiscordPTB.exe", "DiscordCanary.exe"};
}
inline QJsonArray values(const QJsonValue &value) {
    return value.isArray() ? value.toArray() : QJsonArray{value};
}
inline bool contains(const QStringList &names, const QString &name) {
    return names.contains(name, Qt::CaseInsensitive);
}

// The previous factory preset lacked Counter-Strike. Recognize only exact
// factory objects, including that older object; edited/qualified rules belong
// to the user and must not be reinterpreted as defaults.
inline bool factoryRule(const QJsonObject &rule, const QJsonArray &factory) {
    if (!rule.contains("process_name") && !rule.contains("process_path_regex")) return false;
    for (const auto &value : factory) {
        const auto standard = value.toObject();
        if (rule == standard) return true;
        if (standard.value("outbound") != "bypass" || !standard.contains("process_name")) continue;
        auto legacy = standard;
        QJsonArray names;
        for (const auto &name : standard.value("process_name").toArray())
            if (name != "cs2.exe" && name != "csgo.exe") names.append(name);
        legacy.insert("process_name", names);
        if (rule == legacy) return true;
    }
    return false;
}
inline bool namesProcess(const QJsonObject &rule, const QString &name) {
    for (const auto &entry : values(rule.value("process_name")))
        if (entry.toString().compare(name, Qt::CaseInsensitive) == 0) return true;
    for (const auto &entry : values(rule.value("process_path"))) {
        const auto path = entry.toString().replace('\\', '/');
        if (path.section('/', -1).compare(name, Qt::CaseInsensitive) == 0) return true;
    }
    // Arbitrary path patterns cannot be evaluated without the process path.
    // Conservatively preserve their precedence instead of guessing it away.
    if (rule.contains("process_path_regex")) return true;
    for (const auto &child : rule.value("rules").toArray())
        if (namesProcess(child.toObject(), name)) return true;
    return false;
}

inline QJsonArray defaults(const QJsonArray &scheme, const QJsonArray &factory,
                           const QJsonArray &global, const QString &choice = {}, bool gamesViaTunnel = false) {
    if (choice == "keep" || choice == "restored") return {};
    bool inherited = false;
    QJsonArray explicitRules = global;
    for (const auto &entry : scheme) {
        const auto rule = entry.toObject();
        bool standard = false;
        for (const auto &value : factory) if (value == entry) { standard = true; break; }
        const bool processDefault = factoryRule(rule, factory);
        if (!standard && !processDefault) { explicitRules.append(entry); continue; }
        int copies = 0;
        for (const auto &other : scheme) if (other == entry) ++copies;
        if (copies == 1) inherited = inherited || processDefault;
        else explicitRules.append(entry);
    }
    if (!inherited) return {}; // Custom schemes have no implicit factory policy.
    auto remaining = [&](const QStringList &names) {
        QJsonArray result;
        for (const auto &name : names) {
            bool explicitChoice = false;
            for (const auto &value : explicitRules) {
                const auto rule = value.toObject();
                const bool block = rule.value("outbound") == "block" || rule.value("action") == "reject";
                const bool processScoped = rule.contains("process_name") || rule.contains("process_path") || rule.contains("process_path_regex");
                if (namesProcess(rule, name) || (block && !processScoped)) { explicitChoice = true; break; }
            }
            if (!explicitChoice) result.append(name);
        }
        return result;
    };
    QJsonArray result;
    QJsonArray direct;
    auto voice = remaining(voiceProcesses());
    for (const auto &value : remaining(directProcesses())) {
        if (gamesViaTunnel && !contains(GameCatalog::alwaysDirect(), value.toString())) voice.append(value);
        else direct.append(value);
    }
    if (!direct.isEmpty()) result.append(QJsonObject{{"process_name", direct}, {"outbound", "bypass"}});
    if (!voice.isEmpty()) result.append(QJsonObject{{"process_name", voice}, {"outbound", "proxy"}});
    return result;
}

// Only unqualified process routes already ahead of the preset can skip full
// UDP protocol discovery. DNS is still recognized for the mandatory DNS rule.
// An earlier unfamiliar condition or scoped work-DNS policy disables guessing.
inline QJsonArray tunSniffRules(const QJsonArray &first, bool optimize,
                               const QStringList &customPrograms = {}, bool includeDefaults = true) {
    QJsonArray names;
    if (optimize) for (const auto &value : first) {
        const auto rule = value.toObject();
        if (rule.size() != 2 || !rule.contains("outbound")
            || (!rule.contains("process_name") && !rule.contains("ip_cidr"))) break;
        for (const auto &entry : values(rule.value("process_name"))) {
            const auto name = entry.toString();
            const bool eligible = contains(customPrograms, name)
                || (includeDefaults && (contains(directProcesses(), name) || contains(voiceProcesses(), name)));
            if (eligible && !names.contains(entry)) names.append(entry);
        }
    }
    const QJsonObject inbound{{"inbound", QJsonArray{"tun-in"}}};
    if (names.isEmpty()) return {QJsonObject{{"inbound", QJsonArray{"tun-in"}}, {"action", "sniff"}}};
    return {
        QJsonObject{{"inbound", QJsonArray{"tun-in"}}, {"network", "udp"}, {"process_name", names},
                    {"action", "sniff"}, {"sniffer", QJsonArray{"dns"}}},
        QJsonObject{{"type", "logical"}, {"mode", "and"}, {"action", "sniff"}, {"rules", QJsonArray{
            inbound, QJsonObject{{"type", "logical"}, {"mode", "and"}, {"invert", true}, {"rules", QJsonArray{
                QJsonObject{{"network", "udp"}}, QJsonObject{{"process_name", names}}
            }}}}}}
    };
}

// The games switch must not tunnel the launcher or communication software.
// Explicit per-application VPN choices are assembled before these defaults.
inline QJsonObject gameTunnelRule(QJsonObject rule, const QString &choice = {}) {
    if (rule.value("outbound") != "bypass") return {};
    if (!rule.contains("process_name") && !rule.contains("process_path_regex")) return {};
    if (choice == "keep" || choice == "restored") { rule.insert("outbound", "proxy"); return rule; }
    if (rule.contains("process_name")) {
        QJsonArray names;
        for (const auto &value : values(rule.value("process_name"))) {
            const auto name = value.toString();
            if (!contains(GameCatalog::alwaysDirect(), name) && !contains(voiceProcesses(), name)) names.append(value);
        }
        if (names.isEmpty()) return {};
        rule.insert("process_name", names);
    }
    rule.insert("outbound", "proxy");
    return rule;
}
} // namespace GreenRhythm::GameVoiceRouting
