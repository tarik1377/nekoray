#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace GreenRhythm::RuRoutingPolicy {

// This exact, unqualified group was shipped in Routing(1). It is a fallback
// for applications that need the proxy, rather than an explicit user choice
// to tunnel every connection made by one of those applications.
inline QJsonObject standardProcessProxyRule() {
    return {{"outbound", "proxy"},
            {"process_name", QJsonArray{"Discord.exe", "discord.exe", "Telegram.exe", "telegram.exe",
                                        "Codex.exe", "codex.exe", "Claude.exe", "claude.exe", "claude-code.exe"}}};
}

struct SchemeRules {
    QJsonArray primary;
    QJsonArray fallback;
};

inline bool usesRuDirect(const QString &directIPs, const QString &finalOutbound) {
    if (finalOutbound != QStringLiteral("proxy")) return false;
    for (const auto &line : directIPs.split('\n'))
        if (line.trimmed() == QStringLiteral("geoip:ru")) return true;
    return false;
}

inline SchemeRules splitStandardFallback(const QJsonArray &customRules, const QString &directIPs,
                                        const QString &finalOutbound, const QString &defaultsChoice = {}) {
    SchemeRules result{customRules, {}};
    if (defaultsChoice == QStringLiteral("keep") || defaultsChoice == QStringLiteral("restored")) return result;
    if (!usesRuDirect(directIPs, finalOutbound)) return result;
    const auto standard = standardProcessProxyRule();
    int matched = -1;
    for (int i = 0; i < customRules.size(); ++i) {
        if (customRules[i].isObject() && customRules[i].toObject() == standard) {
            // Duplicated groups can be intentional user rules. Do not guess.
            if (matched >= 0) return result;
            matched = i;
        }
    }
    if (matched >= 0) {
        result.fallback.append(result.primary.takeAt(matched));
    }
    return result;
}

} // namespace GreenRhythm::RuRoutingPolicy
