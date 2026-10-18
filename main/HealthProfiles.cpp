#include "HealthProfiles.hpp"
#include "db/Database.hpp"
#include "sub/GroupUpdater.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSet>

namespace GreenRhythm {
namespace {
QByteArray identity(const HealthCandidate &candidate) {
    return QCryptographicHash::hash(QJsonDocument(candidate.outbound).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}
}
HealthCandidate healthCandidate(const std::shared_ptr<NekoGui::ProxyEntity> &entity, bool hasFrontProxy) {
    HealthCandidate result;
    if (!entity || !entity->bean) { result.unavailable = "no_profile"; return result; }
    result.protocol = entity->type;
    const auto endpoint = entity->type + ':' + entity->bean->serverAddress.toLower() + ':' + QString::number(entity->bean->serverPort);
    result.nodeId = QString::fromLatin1(QCryptographicHash::hash(endpoint.toUtf8(), QCryptographicHash::Sha256).toHex().left(20));
    if (hasFrontProxy) { result.unavailable = "front_proxy"; return result; }
    if (!QStringList{"socks", "http", "shadowsocks", "vmess", "vless", "trojan", "hysteria2", "tuic"}.contains(entity->type)) {
        result.unavailable = "profile_type"; return result;
    }
    if (!entity->bean->custom_config.trimmed().isEmpty() || !entity->bean->custom_outbound.trimmed().isEmpty()) {
        result.unavailable = "custom_config"; return result;
    }
    // Some bean builders normalize flow in place. Never give them the actual
    // entity, traffic_data, group, connection controller or profile manager.
    const auto clone = NekoGui::ProfileManager::NewProxyEntity(entity->type);
    if (!clone) { result.unavailable = "profile_type"; return result; }
    clone->bean->FromJson(entity->bean->ToJson());
    if (clone->bean->NeedExternal(true) != 0) { result.unavailable = "external_core"; return result; }
    const auto built = clone->bean->BuildCoreObjSingBox();
    if (!built.error.isEmpty() || built.outbound.isEmpty()) { result.unavailable = "outbound_build"; return result; }
    if (!built.outbound.value("plugin").toString().isEmpty()) { result.unavailable = "external_plugin"; return result; }
    result.outbound = built.outbound;
    return result;
}
HealthSubscription healthSubscription(const QByteArray &body, const HealthCandidate &current) {
    HealthSubscription result;
    NekoGui_sub::RawUpdater parser; parser.collect_only = true; parser.update(QString::fromUtf8(body));
    result.count = parser.updated_order.size();
    if (!result.count) return result;
    const bool comparable = !current.outbound.isEmpty(); bool present = false;
    const auto currentIdentity = comparable ? identity(current) : QByteArray();
    QSet<QByteArray> seen;
    if (comparable) seen.insert(currentIdentity);
    int inspected = 0;
    for (const auto &entity : parser.updated_order) {
        // Parsing is already bounded by a 1 MiB response; also cap synchronous
        // outbound construction. A truncated scan cannot claim absence.
        if (++inspected > 512) break;
        auto candidate = healthCandidate(entity);
        if (candidate.outbound.isEmpty()) { ++result.unsupported; continue; }
        const auto id = identity(candidate);
        if (comparable && id == currentIdentity) present = true;
        if (seen.contains(id)) continue;
        seen.insert(id);
        if (result.alternatives.size() < 2) result.alternatives.append(candidate);
    }
    result.currentPresence = present ? "present" : comparable && inspected <= 512 ? "absent" : "unknown";
    return result;
}
} // namespace GreenRhythm
