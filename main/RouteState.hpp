#pragma once
#include "NekoGui.hpp"
#include "RouteDefaultsPolicy.hpp"
#include <QFile>
#include <memory>

namespace GreenRhythm::RouteState {
// JsonStore::ToJson omits empty strings. A route-editor replacement must include
// them explicitly, otherwise clearing a field retains its previous disk value.
inline QJsonObject completeSnapshot(const NekoGui::Routing &route) {
    return {{"direct_ip", route.direct_ip}, {"direct_domain", route.direct_domain},
            {"proxy_ip", route.proxy_ip}, {"proxy_domain", route.proxy_domain},
            {"block_ip", route.block_ip}, {"block_domain", route.block_domain},
            {"def_outbound", route.def_outbound}, {"custom", route.custom},
            {"remote_dns", route.remote_dns}, {"remote_dns_strategy", route.remote_dns_strategy},
            {"direct_dns", route.direct_dns}, {"direct_dns_strategy", route.direct_dns_strategy},
            {"local_dns_domains", route.local_dns_domains}, {"local_dns_server", route.local_dns_server},
            {"local_dns_hosts", route.local_dns_hosts}, {"dns_routing", route.dns_routing},
            {"use_dns_object", route.use_dns_object}, {"dns_object", route.dns_object},
            {"dns_final_out", route.dns_final_out}, {"domain_strategy", route.domain_strategy},
            {"outbound_domain_strategy", route.outbound_domain_strategy}, {"sniffing_mode", route.sniffing_mode}};
}
inline QJsonObject mergeEditor(const QJsonObject &disk, const NekoGui::Routing &displayed) {
    auto merged = disk;
    const auto visible = completeSnapshot(displayed);
    for (const auto &key : visible.keys()) merged.insert(key, visible.value(key));
    return merged;
}
inline RouteDefaultsPolicy::Preview defaultsPreview(const QJsonObject &current,
        const NekoGui::Routing &preset, RouteDefaultsPolicy::Mode mode,
        const QString &globalRules = {}) {
    // Empty defaults must clear custom values during an explicitly chosen reset.
    return RouteDefaultsPolicy::preview(current, completeSnapshot(preset), mode, globalRules);
}
// FromJson updates present keys only. Start from the empty schema for every
// saved snapshot, so absent keys cannot retain the previously active rules.
inline void installSaved(std::unique_ptr<NekoGui::Routing> &target,
                         const QJsonObject &snapshot, const QString &path) {
    auto replacement = std::make_unique<NekoGui::Routing>(0);
    replacement->fn = path;
    replacement->FromJson(snapshot);
    target = std::move(replacement);
}
inline std::unique_ptr<NekoGui::Routing> initialForPath(const QString &path) {
    // Only a genuinely new scheme inherits the current bundled preset.
    auto route = std::make_unique<NekoGui::Routing>(QFile::exists(path) ? 0 : 1);
    route->fn = path;
    return route;
}
} // namespace GreenRhythm::RouteState
