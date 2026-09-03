#include "db/ConfigBuilder.hpp"
#include "db/Database.hpp"
#include "fmt/includes.h"
#include "fmt/Preset.hpp"

#include <QApplication>
#include <QFile>
#include <QFileInfo>

#define BOX_UNDERLYING_DNS dataStore->core_box_underlying_dns.isEmpty() ? "local" : dataStore->core_box_underlying_dns

namespace NekoGui {

    QStringList getAutoBypassExternalProcessPaths(const std::shared_ptr<BuildConfigResult> &result) {
        QStringList paths;
        for (const auto &extR: result->extRs) {
            auto path = extR->program;
            if (path.trimmed().isEmpty()) continue;
            paths << path.replace("\\", "/");
        }
        return paths;
    }

    /**
     * Имя сетевого интерфейса туннеля.
     *
     * НА MACOS — ПУСТАЯ СТРОКА, и это не «не сделали», а единственный рабочий
     * вариант. Здесь стояло «utun9», и на маке имя утилитарного интерфейса — не
     * ярлык, а НОМЕР блока управления в ядре: попросив utun9, мы требуем именно
     * девятый, и если он занят — а его занимают iCloud Private Relay и любой
     * другой VPN, — sing-tun падает с EBUSY. Для человека это «включил туннель,
     * он сразу выключился», причём воспроизводится через раз, в зависимости от
     * того, что он запускал до нас.
     *
     * Пустая строка означает «дай любой свободный», и его выдаёт ядро.
     *
     * На Windows имя остаётся прежним: там оно и есть ярлык, по нему туннель
     * узнают скрипты диагностики и правила фаервола, и менять его нельзя.
     */
    QString genTunName() {
#ifdef Q_OS_MACOS
        return {};
#else
        return "neko-tun";
#endif
    }

    void MergeJson(QJsonObject &dst, const QJsonObject &src) {
        // 合并
        if (src.isEmpty()) return;
        for (const auto &key: src.keys()) {
            auto v_src = src[key];
            if (dst.contains(key)) {
                auto v_dst = dst[key];
                if (v_src.isObject() && v_dst.isObject()) { // isObject 则合并？
                    auto v_src_obj = v_src.toObject();
                    auto v_dst_obj = v_dst.toObject();
                    MergeJson(v_dst_obj, v_src_obj);
                    dst[key] = v_dst_obj;
                } else {
                    dst[key] = v_src;
                }
            } else if (v_src.isArray()) {
                if (key.startsWith("+")) {
                    auto key2 = SubStrAfter(key, "+");
                    auto v_dst = dst[key2];
                    auto v_src_arr = v_src.toArray();
                    auto v_dst_arr = v_dst.toArray();
                    QJSONARRAY_ADD(v_src_arr, v_dst_arr)
                    dst[key2] = v_src_arr;
                } else if (key.endsWith("+")) {
                    auto key2 = SubStrBefore(key, "+");
                    auto v_dst = dst[key2];
                    auto v_src_arr = v_src.toArray();
                    auto v_dst_arr = v_dst.toArray();
                    QJSONARRAY_ADD(v_dst_arr, v_src_arr)
                    dst[key2] = v_dst_arr;
                } else {
                    dst[key] = v_src;
                }
            } else {
                dst[key] = v_src;
            }
        }
    }

    // Common

    std::shared_ptr<BuildConfigResult> BuildConfig(const std::shared_ptr<ProxyEntity> &ent, bool forTest, bool forExport) {
        auto result = std::make_shared<BuildConfigResult>();
        auto status = std::make_shared<BuildConfigStatus>();
        status->ent = ent;
        status->result = result;
        status->forTest = forTest;
        status->forExport = forExport;

        auto customBean = dynamic_cast<NekoGui_fmt::CustomBean *>(ent->bean.get());
        if (customBean != nullptr && customBean->core == "internal-full") {
            result->coreConfig = QString2QJsonObject(customBean->config_simple);
        } else {
            BuildConfigSingBox(status);
        }

        // apply custom config
        MergeJson(result->coreConfig, QString2QJsonObject(ent->bean->custom_config));

        return result;
    }

    QString BuildChain(int chainId, const std::shared_ptr<BuildConfigStatus> &status) {
        auto group = profileManager->GetGroup(status->ent->gid);
        if (group == nullptr) {
            status->result->error = QStringLiteral("This profile is not in any group, your data may be corrupted.");
            return {};
        }

        auto resolveChain = [=](const std::shared_ptr<ProxyEntity> &ent) {
            QList<std::shared_ptr<ProxyEntity>> resolved;
            if (ent->type == "chain") {
                auto list = ent->ChainBean()->list;
                std::reverse(std::begin(list), std::end(list));
                for (auto id: list) {
                    resolved += profileManager->GetProfile(id);
                    if (resolved.last() == nullptr) {
                        status->result->error = QStringLiteral("chain missing ent: %1").arg(id);
                        break;
                    }
                    if (resolved.last()->type == "chain") {
                        status->result->error = QStringLiteral("chain in chain is not allowed: %1").arg(id);
                        break;
                    }
                }
            } else {
                resolved += ent;
            };
            return resolved;
        };

        // Auto-failover (opt-in): build every eligible internal server in the group as a
        // sibling outbound and wrap them in a sing-box urltest, which auto-picks the
        // lowest-latency server and fails over when the active one dies. The wrapper keeps
        // the public tag "proxy", so DNS detour and route rules need no change. External-core
        // protocols (hysteria2/naive etc.) are skipped — each would spawn its own helper
        // process. Falls back to the normal single-profile build if no candidate qualifies.
        if (dataStore->auto_failover && !status->forTest) {
            QJsonArray candidateTags;
            int cid = 1;
            for (const auto &m: group->ProfilesWithOrder()) {
                if (m == nullptr || m->type == "chain") continue;
                if (m->bean->NeedExternal(true) != 0) continue;
                QList<std::shared_ptr<ProxyEntity>> one;
                one += m;
                auto t = BuildChainInternal(cid++, one, status);
                if (!status->result->error.isEmpty()) return {};
                if (!t.isEmpty()) candidateTags += t;
            }
            if (!candidateTags.isEmpty()) {
                status->outbounds += QJsonObject{
                    {"type", "urltest"},
                    {"tag", "proxy"},
                    {"outbounds", candidateTags},
                    {"url", dataStore->test_latency_url},
                    {"interval", "3m"},
                    {"tolerance", 50},
                    {"idle_timeout", "30m"},
                };
                // Status-bar speed: query the urltest aggregate ("proxy"), not the started
                // member — the member loop overwrote ent->traffic_data->tag to g-<id>, and the
                // active server is usually a different member. id stays -1 so this virtual entry
                // feeds only the status-bar speed, not the per-row proxy list.
                auto proxyStat = std::make_shared<NekoGui_traffic::TrafficData>(std::string("proxy"));
                status->result->outboundStat = proxyStat;
                status->result->outboundStats += proxyStat;
                return "proxy";
            }
            // no eligible candidate → fall through to the normal single-profile build
        }

        // Make list
        auto ents = resolveChain(status->ent);
        if (!status->result->error.isEmpty()) return {};

        if (group->front_proxy_id >= 0) {
            auto fEnt = profileManager->GetProfile(group->front_proxy_id);
            if (fEnt == nullptr) {
                status->result->error = QStringLiteral("front proxy ent not found.");
                return {};
            }
            ents += resolveChain(fEnt);
            if (!status->result->error.isEmpty()) return {};
        }

        // BuildChain
        QString chainTagOut = BuildChainInternal(0, ents, status);

        // Chain ent traffic stat
        if (ents.length() > 1) {
            status->ent->traffic_data->id = status->ent->id;
            status->ent->traffic_data->tag = chainTagOut.toStdString();
            status->result->outboundStats += status->ent->traffic_data;
        }

        return chainTagOut;
    }

#define DOMAIN_USER_RULE                                                             \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->proxy_domain)) {  \
        if (dataStore->routing->dns_routing) status->domainListDNSRemote += line;    \
        status->domainListRemote += line;                                            \
    }                                                                                \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->direct_domain)) { \
        if (dataStore->routing->dns_routing) status->domainListDNSDirect += line;    \
        status->domainListDirect += line;                                            \
    }                                                                                \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->block_domain)) {  \
        status->domainListBlock += line;                                             \
    }

#define IP_USER_RULE                                                             \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->block_ip)) {  \
        status->ipListBlock += line;                                             \
    }                                                                            \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->proxy_ip)) {  \
        status->ipListRemote += line;                                            \
    }                                                                            \
    for (const auto &line: SplitLinesSkipSharp(dataStore->routing->direct_ip)) { \
        status->ipListDirect += line;                                            \
    }

    QString BuildChainInternal(int chainId, const QList<std::shared_ptr<ProxyEntity>> &ents,
                               const std::shared_ptr<BuildConfigStatus> &status) {
        QString chainTag = "c-" + Int2String(chainId);
        QString chainTagOut;
        bool muxApplied = false;

        QString pastTag;
        int pastExternalStat = 0;
        int index = 0;

        for (const auto &ent: ents) {
            // tagOut: v2ray outbound tag for a profile
            // profile2 (in) (global)   tag g-(id)
            // profile1                 tag (chainTag)-(id)
            // profile0 (out)           tag (chainTag)-(id) / single: chainTag=g-(id)
            auto tagOut = chainTag + "-" + Int2String(ent->id);

            // needGlobal: can only contain one?
            bool needGlobal = false;

            // first profile set as global
            auto isFirstProfile = index == ents.length() - 1;
            if (isFirstProfile) {
                needGlobal = true;
                tagOut = "g-" + Int2String(ent->id);
            }

            // last profile set as "proxy"
            if (chainId == 0 && index == 0) {
                needGlobal = false;
                tagOut = "proxy";
            }

            // ignoreConnTag
            if (index != 0) {
                status->result->ignoreConnTag << tagOut;
            }

            if (needGlobal) {
                if (status->globalProfiles.contains(ent->id)) {
                    continue;
                }
                status->globalProfiles += ent->id;
            }

            if (index > 0) {
                // chain rules: past
                if (pastExternalStat == 0) {
                    auto replaced = status->outbounds.last().toObject();
                    replaced["detour"] = tagOut;
                    status->outbounds.removeLast();
                    status->outbounds += replaced;
                } else {
                    status->routingRules += QJsonObject{
                        {"inbound", QJsonArray{pastTag + "-mapping"}},
                        {"outbound", tagOut},
                    };
                }
            } else {
                // index == 0 means last profile in chain / not chain
                chainTagOut = tagOut;
                status->result->outboundStat = ent->traffic_data;
            }

            // chain rules: this
            auto ext_mapping_port = 0;
            auto ext_socks_port = 0;
            auto thisExternalStat = ent->bean->NeedExternal(isFirstProfile);
            if (thisExternalStat < 0) {
                status->result->error = "This configuration cannot be set automatically, please try another.";
                return {};
            }

            // determine port
            if (thisExternalStat > 0) {
                if (ent->type == "custom") {
                    auto bean = ent->CustomBean();
                    if (IsValidPort(bean->mapping_port)) {
                        ext_mapping_port = bean->mapping_port;
                    } else {
                        ext_mapping_port = MkPort();
                    }
                    if (IsValidPort(bean->socks_port)) {
                        ext_socks_port = bean->socks_port;
                    } else {
                        ext_socks_port = MkPort();
                    }
                } else {
                    ext_mapping_port = MkPort();
                    ext_socks_port = MkPort();
                }
            }
            if (thisExternalStat == 2) dataStore->need_keep_vpn_off = true;
            if (thisExternalStat == 1) {
                // mapping
                status->inbounds += QJsonObject{
                    {"type", "direct"},
                    {"tag", tagOut + "-mapping"},
                    {"listen", "127.0.0.1"},
                    {"listen_port", ext_mapping_port},
                    {"override_address", ent->bean->serverAddress},
                    {"override_port", ent->bean->serverPort},
                };
                // no chain rule and not outbound, so need to set to direct
                if (isFirstProfile) {
                    status->routingRules += QJsonObject{
                        {"inbound", QJsonArray{tagOut + "-mapping"}},
                        {"outbound", "direct"},
                    };
                }
            }

            // Outbound

            QJsonObject outbound;
            auto stream = GetStreamSettings(ent->bean.get());

            if (thisExternalStat > 0) {
                auto extR = ent->bean->BuildExternal(ext_mapping_port, ext_socks_port, thisExternalStat);
                if (extR.program.isEmpty()) {
                    const auto core = ent->bean->DisplayCoreType();
                    QString msg = QObject::tr("Ядро «%1» не найдено.").arg(core);
                    if (core.compare("xray", Qt::CaseInsensitive) == 0) {
                        // xray.exe ships next to the app; if it vanished, antivirus is the
                        // usual culprit — xray is widely false-flagged on Windows.
                        msg += QObject::tr(" Оно входит в комплект (xray.exe рядом с программой). "
                                           "Скорее всего, его удалил антивирус — восстановите файл из "
                                           "архива GreenRhythm или добавьте программу в исключения антивируса.");
                    } else {
                        msg += QObject::tr(" Укажите путь к ядру в Настройках → Extra cores.");
                    }
                    status->result->error = msg;
                    return {};
                }
                if (!extR.error.isEmpty()) { // rejected
                    status->result->error = extR.error;
                    return {};
                }
                extR.tag = ent->bean->DisplayType();
                status->result->extRs.emplace_back(std::make_shared<NekoGui_fmt::ExternalBuildResult>(extR));

                // SOCKS OUTBOUND
                outbound["type"] = "socks";
                outbound["server"] = "127.0.0.1";
                outbound["server_port"] = ext_socks_port;
            } else {
                const auto coreR = ent->bean->BuildCoreObjSingBox();
                if (coreR.outbound.isEmpty()) {
                    status->result->error = "unsupported outbound";
                    return {};
                }
                if (!coreR.error.isEmpty()) { // rejected
                    status->result->error = coreR.error;
                    return {};
                }
                outbound = coreR.outbound;
            }

            // outbound misc
            outbound["tag"] = tagOut;
            ent->traffic_data->id = ent->id;
            ent->traffic_data->tag = tagOut.toStdString();
            status->result->outboundStats += ent->traffic_data;

            // mux common
            auto needMux = ent->type == "vmess" || ent->type == "trojan" || ent->type == "vless";
            needMux &= dataStore->mux_concurrency > 0;

            if (stream != nullptr) {
                if (stream->network == "grpc" || stream->network == "quic" || (stream->network == "http" && stream->security == "tls")) {
                    needMux = false;
                }
                if (stream->multiplex_status == 0) {
                    if (!dataStore->mux_default_on) needMux = false;
                } else if (stream->multiplex_status == 1) {
                    needMux = true;
                } else if (stream->multiplex_status == 2) {
                    needMux = false;
                }
            }
            // Use value() (const, non-inserting) — operator[] would insert a null
            // "flow" key into non-VLESS outbounds (e.g. the socks bridge to the xray
            // core), which sing-box then rejects as an unknown field.
            if (ent->type == "vless" && outbound.value("flow").toString() != "") {
                needMux = false;
            }
            // External cores (xray/naive/…) get a plain SOCKS bridge outbound. Apply this
            // clamp LAST so a per-profile multiplex_status==1 toggle above cannot re-enable
            // mux on the bridge: sing-box's SOCKSOutboundOptions has no `multiplex` field
            // and rejects the entire config on the unknown key.
            if (thisExternalStat != 0) needMux = false;

            // common
            // apply domain_strategy
            outbound["domain_strategy"] = dataStore->routing->outbound_domain_strategy;
            // Keep the NAT/CGNAT mapping fresh so long-lived proxy connections don't get
            // RST'd around the ~2min idle-eviction mark (sing-box default first probe = 5min).
            outbound["tcp_keep_alive"] = "20s";
            outbound["tcp_keep_alive_interval"] = "15s";
            // apply mux
            if (!muxApplied && needMux) {
                auto muxObj = QJsonObject{
                    {"enabled", true},
                    {"protocol", dataStore->mux_protocol},
                    {"padding", dataStore->mux_padding},
                    {"max_streams", dataStore->mux_concurrency},
                };
                outbound["multiplex"] = muxObj;
                muxApplied = true;
            }

            // apply custom outbound settings
            MergeJson(outbound, QString2QJsonObject(ent->bean->custom_outbound));

            // Bypass Lookup for the first profile
            auto serverAddress = ent->bean->serverAddress;

            auto customBean = dynamic_cast<NekoGui_fmt::CustomBean *>(ent->bean.get());
            if (customBean != nullptr && customBean->core == "internal") {
                auto server = QString2QJsonObject(customBean->config_simple)["server"].toString();
                if (!server.isEmpty()) serverAddress = server;
            }

            if (!IsIpAddress(serverAddress)) {
                status->domainListDNSDirect += "full:" + serverAddress;
            }

            status->outbounds += outbound;
            pastTag = tagOut;
            pastExternalStat = thisExternalStat;
            index++;
        }

        return chainTagOut;
    }

    // SingBox

    void BuildConfigSingBox(const std::shared_ptr<BuildConfigStatus> &status) {
        // Log
        status->result->coreConfig["log"] = QJsonObject{{"level", dataStore->log_level}};

        // Inbounds

        // mixed-in
        if (IsValidPort(dataStore->inbound_socks_port) && !status->forTest) {
            QJsonObject inboundObj;
            inboundObj["tag"] = "mixed-in";
            inboundObj["type"] = "mixed";
            inboundObj["listen"] = dataStore->inbound_address;
            inboundObj["listen_port"] = dataStore->inbound_socks_port;
            if (dataStore->routing->sniffing_mode != SniffingMode::DISABLE) {
                inboundObj["sniff"] = true;
                inboundObj["sniff_override_destination"] = dataStore->routing->sniffing_mode == SniffingMode::FOR_DESTINATION;
            }
            if (dataStore->inbound_auth->NeedAuth()) {
                inboundObj["users"] = QJsonArray{
                    QJsonObject{
                        {"username", dataStore->inbound_auth->username},
                        {"password", dataStore->inbound_auth->password},
                    },
                };
            }
            inboundObj["domain_strategy"] = dataStore->routing->domain_strategy;
            status->inbounds += inboundObj;
        }

        // tun-in
        if (UseInternalTun() && dataStore->spmode_vpn && !status->forTest) {
            QJsonObject inboundObj;
            inboundObj["tag"] = "tun-in";
            inboundObj["type"] = "tun";
            // Поле не ставится вовсе, если имени нет: пустая строка в конфиге и
            // отсутствие поля — разные вещи, и sing-tun на пустую ругается.
            if (const auto tunName = genTunName(); !tunName.isEmpty()) {
                inboundObj["interface_name"] = tunName;
            }
            inboundObj["auto_route"] = true;
            inboundObj["endpoint_independent_nat"] = true;
            inboundObj["mtu"] = dataStore->vpn_mtu;
            inboundObj["stack"] = Preset::SingBox::VpnImplementation.value(dataStore->vpn_implementation);
            inboundObj["strict_route"] = dataStore->vpn_strict_route;
            inboundObj["inet4_address"] = "172.19.0.1/28";
            if (dataStore->vpn_ipv6) inboundObj["inet6_address"] = "fdfe:dcba:9876::1/126";
            // Keep LAN/private traffic out of the tunnel entirely (route-table level), so
            // cross-subnet probes (e.g. Windows Delivery Optimization :7680) don't get
            // captured and time out. TUN's own 172.19.0.1/28 is set via inet4_address above,
            // so excluding 172.16/12 is safe.
            //
            // К ним же добавлены 100.64.0.0/10 и адреса IPv6 того же смысла.
            // 100.64/10 — это CGNAT, и по нему живёт Tailscale: без исключения
            // домашняя сеть человека уезжает в наш туннель ровно в тот момент,
            // когда он включает наш клиент, и «перестал видеть свой NAS» он
            // свяжет с чем угодно, только не с этим. fd00::/8 и fe80::/10 —
            // то же самое для IPv6, которого в прежнем списке не было вовсе.
            //
            // fd00::/8, А НЕ fc00::/7, И ЭТО ИСПРАВЛЕНИЕ СОБСТВЕННОЙ ОШИБКИ.
            // Стояло fc00::/7 — «все локальные адреса шестой версии». Но наш
            // ЖЕ поддельный диапазон живёт внутри него: inet6_range = fc00::/18
            // (ниже в этом файле и в res/vpn/sing-box-vpn.json). То есть каждый
            // выданный нами поддельный адрес объявлялся «домашней сетью» и
            // выводился из туннеля — а знать о нём, кроме туннеля, некому. Имя,
            // разрешённое в такой адрес, переставало открываться совсем.
            //
            // fc00::/7 делится ровно пополам: fc00::/8 не роздан никем и там
            // сидит наша подделка, fd00::/8 — та половина, которую и раздают
            // домашние роутеры. Исключая только вторую, мы защищаем домашнюю
            // сеть и не отбираем у себя собственный диапазон.
            auto excludes = QJsonArray{
                "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "169.254.0.0/16",
                "100.64.0.0/10", "fd00::/8", "fe80::/10"};
            // Дописанное человеком идёт СВЕРХ встроенного, а не вместо: список
            // выше защищает домашнюю сеть, и позволить его случайно затереть
            // одной строчкой в настройках значило бы отдать эту защиту опечатке.
            for (const auto &extra: SplitLinesSkipSharp(dataStore->vpn_route_exclude_extra)) {
                if (!extra.trimmed().isEmpty()) excludes += extra.trimmed();
            }
            inboundObj["route_exclude_address"] = excludes;
            if (dataStore->routing->sniffing_mode != SniffingMode::DISABLE) {
                inboundObj["sniff"] = true;
                inboundObj["sniff_override_destination"] = dataStore->routing->sniffing_mode == SniffingMode::FOR_DESTINATION;
            }
            inboundObj["domain_strategy"] = dataStore->routing->domain_strategy;
            status->inbounds += inboundObj;
        }

        // Outbounds
        auto tagProxy = BuildChain(0, status);
        if (!status->result->error.isEmpty()) return;

        // direct & bypass & block
        status->outbounds += QJsonObject{
            {"type", "direct"},
            {"tag", "direct"},
        };
        status->outbounds += QJsonObject{
            {"type", "direct"},
            {"tag", "bypass"},
        };
        status->outbounds += QJsonObject{
            {"type", "block"},
            {"tag", "block"},
        };
        if (!status->forTest) {
            status->outbounds += QJsonObject{
                {"type", "dns"},
                {"tag", "dns-out"},
            };
        }

        // custom inbound
        if (!status->forTest) QJSONARRAY_ADD(status->inbounds, QString2QJsonObject(dataStore->custom_inbound)["inbounds"].toArray())

        status->result->coreConfig.insert("inbounds", status->inbounds);
        status->result->coreConfig.insert("outbounds", status->outbounds);

        // user rule
        if (!status->forTest) {
            DOMAIN_USER_RULE
            IP_USER_RULE
        }

        // sing-box common rule object
        auto make_rule = [&](const QStringList &list, bool isIP = false) {
            QJsonObject rule;
            //
            QJsonArray ip_cidr;
            QJsonArray geoip;
            //
            QJsonArray domain_keyword;
            QJsonArray domain_subdomain;
            QJsonArray domain_regexp;
            QJsonArray domain_full;
            QJsonArray geosite;
            for (auto item: list) {
                if (isIP) {
                    if (item.startsWith("geoip:")) {
                        geoip += item.replace("geoip:", "");
                    } else {
                        ip_cidr += item;
                    }
                } else {
                    // https://www.v2fly.org/config/dns.html#dnsobject
                    if (item.startsWith("geosite:")) {
                        geosite += item.replace("geosite:", "");
                    } else if (item.startsWith("full:")) {
                        domain_full += item.replace("full:", "").toLower();
                    } else if (item.startsWith("domain:")) {
                        domain_subdomain += item.replace("domain:", "").toLower();
                    } else if (item.startsWith("regexp:")) {
                        domain_regexp += item.replace("regexp:", "").toLower();
                    } else if (item.startsWith("keyword:")) {
                        domain_keyword += item.replace("keyword:", "").toLower();
                    } else {
                        domain_subdomain += item.toLower();
                    }
                }
            }
            if (isIP) {
                if (ip_cidr.isEmpty() && geoip.isEmpty()) return rule;
                rule["ip_cidr"] = ip_cidr;
                rule["geoip"] = geoip;
            } else {
                if (domain_keyword.isEmpty() && domain_subdomain.isEmpty() && domain_regexp.isEmpty() && domain_full.isEmpty() && geosite.isEmpty()) {
                    return rule;
                }
                rule["domain"] = domain_full;
                rule["domain_suffix"] = domain_subdomain; // v2ray Subdomain => sing-box suffix
                rule["domain_keyword"] = domain_keyword;
                rule["domain_regex"] = domain_regexp;
                rule["geosite"] = geosite;
            }
            return rule;
        };

        // final add DNS
        QJsonObject dns;
        QJsonArray dnsServers;
        QJsonArray dnsRules;

        // Remote
        if (!status->forTest)
            dnsServers += QJsonObject{
                {"tag", "dns-remote"},
                {"address_resolver", "dns-local"},
                {"strategy", dataStore->routing->remote_dns_strategy},
                {"address", dataStore->routing->remote_dns},
                {"detour", tagProxy},
            };

        // Direct
        QJsonObject directObj{
            {"tag", "dns-direct"},
            {"address_resolver", "dns-local"},
            {"strategy", dataStore->routing->direct_dns_strategy},
            {"address", dataStore->routing->direct_dns},
            {"detour", "direct"},
        };
        if (dataStore->routing->dns_final_out == "bypass") {
            dnsServers.prepend(directObj);
        } else {
            dnsServers.append(directObj);
        }
        dnsRules.append(QJsonObject{
            {"outbound", "any"},
            {"server", "dns-direct"},
        });

        // block
        if (!status->forTest)
            dnsServers += QJsonObject{
                {"tag", "dns-block"},
                {"address", "rcode://success"},
            };

        // Fakedns
        if (dataStore->fake_dns && UseInternalTun() && dataStore->spmode_vpn && !status->forTest) {
            dnsServers += QJsonObject{
                {"tag", "dns-fake"},
                {"address", "fakeip"},
            };
            dns["fakeip"] = QJsonObject{
                {"enabled", true},
                {"inet4_range", "198.18.0.0/15"},
                {"inet6_range", "fc00::/18"},
            };
        }

        // Underlying 100% Working DNS ?
        dnsServers += QJsonObject{
            {"tag", "dns-local"},
            {"address", BOX_UNDERLYING_DNS},
            {"detour", "direct"},
        };

        // sing-box dns rule object
        auto add_rule_dns = [&](const QStringList &list, const QString &server) {
            auto rule = make_rule(list, false);
            if (rule.isEmpty()) return;
            rule["server"] = server;
            dnsRules += rule;
        };
        add_rule_dns(status->domainListDNSRemote, "dns-remote");
        add_rule_dns(status->domainListDNSDirect, "dns-direct");

        // built-in rules
        if (!status->forTest) {
            dnsRules += QJsonObject{
                {"query_type", QJsonArray{32, 33}},
                {"server", "dns-block"},
            };
            dnsRules += QJsonObject{
                {"domain_suffix", ".lan"},
                {"server", "dns-block"},
            };
        }

        // fakedns rule
        if (dataStore->fake_dns && UseInternalTun() && dataStore->spmode_vpn && !status->forTest) {
            dnsRules += QJsonObject{
                {"inbound", "tun-in"},
                {"server", "dns-fake"},
            };
        }

        dns["servers"] = dnsServers;
        dns["rules"] = dnsRules;
        dns["independent_cache"] = true;

        if (dataStore->routing->use_dns_object) {
            dns = QString2QJsonObject(dataStore->routing->dns_object);
        }
        status->result->coreConfig.insert("dns", dns);

        // Routing

        // ПЕРЕХВАТ DNS — ПЕРВЫМ ПРАВИЛОМ, ВЫШЕ ВСЕГО, ЧТО ЗАДАЁТ ЧЕЛОВЕК.
        //
        // Разрешение имён идёт на адрес внутри самого туннеля. Стоит человеку
        // вписать в «мимо туннеля» подсеть, которая его накрывает (172.16.0.0/12
        // вписывают, чтобы не заводить в туннель рабочую сеть), — и запросы имён
        // уходят в никуда: сперва в чёрную дыру на предпочитаемом интерфейсе, а
        // затем, если повезёт, мимо туннеля и на глазах у провайдера. Снаружи это
        // «интернет пропал или еле шевелится», и связать это со своей же строкой
        // в настройках человек не сможет никогда.
        //
        // Поэтому перехват живёт в отдельном массиве, который приклеивается
        // раньше всех остальных: обогнать его нельзя ни списком, ни пресетом.
        if (!status->forTest) {
            status->routingRulesDns += QJsonObject{
                {"protocol", "dns"},
                {"outbound", "dns-out"},
            };
        }

        // sing-box routing rule object
        auto add_rule_route = [&](const QStringList &list, bool isIP, const QString &out) {
            auto rule = make_rule(list, isIP);
            if (rule.isEmpty()) return;
            rule["outbound"] = out;
            status->routingRules += rule;
        };

        /*
         * ЯВНОЕ ИСКЛЮЧЕНИЕ БЬЁТ ШИРОКУЮ КАТЕГОРИЮ.
         *
         * Блок стоял первым, и это значило, что исключения из него сделать
         * нельзя ВООБЩЕ. Живой случай: в «Блок» вписан geosite:category-ads-all,
         * а рекламный кабинет VK живёт на ads.vk.ru — он в той же категории, и
         * рабочий инструмент пропадал вместе с рекламой. Дописать его в
         * «Напрямую» не помогало: до этого правила дело не доходило.
         *
         * Просто переставить списки местами нельзя, и это не осторожность.
         * В «Напрямую» у людей стоит domain:ru — а ads.vk.ru тоже .ru, и такая
         * перестановка молча сняла бы блокировку рекламы со всей российской
         * зоны. Широкое правило не должно отменять широкое.
         *
         * Поэтому вперёд выносится только ТОЧНОЕ совпадение — то, что человек
         * написал через full:. Категорию он не составлял, а этот домен вписал
         * сам и осознанно; из двух правил выигрывает то, которое он назвал
         * поимённо.
         *
         * Ничего не написавший через full: не заметит правки вовсе: списки ниже
         * остались на прежних местах и в прежнем порядке.
         */
        auto only_exact = [](const QStringList &list) {
            QStringList out;
            for (const auto &item: list) {
                if (item.startsWith("full:")) out += item;
            }
            return out;
        };
        add_rule_route(only_exact(status->domainListDirect), false, "bypass");
        add_rule_route(only_exact(status->domainListRemote), false, tagProxy);

        // final add user rule
        add_rule_route(status->domainListBlock, false, "block");
        add_rule_route(status->domainListRemote, false, tagProxy);
        add_rule_route(status->domainListDirect, false, "bypass");
        add_rule_route(status->ipListBlock, true, "block");
        add_rule_route(status->ipListRemote, true, tagProxy);
        add_rule_route(status->ipListDirect, true, "bypass");

        // built-in rules
        status->routingRules += QJsonObject{
            {"network", "udp"},
            {"port", QJsonArray{135, 137, 138, 139, 5353}},
            {"outbound", "block"},
        };
        status->routingRules += QJsonObject{
            {"ip_cidr", QJsonArray{"224.0.0.0/3", "ff00::/8"}},
            {"outbound", "block"},
        };
        status->routingRules += QJsonObject{
            {"source_ip_cidr", QJsonArray{"224.0.0.0/3", "ff00::/8"}},
            {"outbound", "block"},
        };

        // tun user rule
        if (UseInternalTun() && dataStore->spmode_vpn && !status->forTest) {
            auto match_out = dataStore->vpn_rule_white ? "proxy" : "bypass";

            // КУДА кладём — зависит от смысла списка, и это не мелочь.
            //
            // «Мимо туннеля» (обычный режим) — человек назвал программу поимённо,
            // и его выбор обязан решать РАНЬШЕ общих запретов из пресета. Иначе
            // выходит сегодняшний случай: игра вписана, а весь её udp/443 гасится
            // блокировкой, до которой очередь доходит первой, и обход не делает
            // ничего, оставаясь на вид настроенным.
            //
            // «Только через туннель» (белый список) — наоборот, оставляем позади:
            // там match_out = proxy, и вынос вперёд протащил бы в туннель QUIC,
            // который пресет глушит намеренно, потому что тоннелированный QUIC
            // молча встаёт и страницы не открываются.
            auto &userRules = dataStore->vpn_rule_white ? status->routingRules
                                                        : status->routingRulesFirst;

            QString process_name_rule = dataStore->vpn_rule_process.trimmed();
            if (!process_name_rule.isEmpty()) {
                auto arr = SplitLinesSkipSharp(process_name_rule);
                QJsonObject rule{{"outbound", match_out},
                                 {"process_name", QList2QJsonArray(arr)}};
                userRules += rule;
            }

            QString cidr_rule = dataStore->vpn_rule_cidr.trimmed();
            if (!cidr_rule.isEmpty()) {
                auto arr = SplitLinesSkipSharp(cidr_rule);
                QJsonObject rule{{"outbound", match_out},
                                 {"ip_cidr", QList2QJsonArray(arr)}};
                userRules += rule;
            }

            auto autoBypassExternalProcessPaths = getAutoBypassExternalProcessPaths(status->result);
            if (!autoBypassExternalProcessPaths.isEmpty()) {
                QJsonObject rule{{"outbound", "bypass"},
                                 {"process_name", QList2QJsonArray(autoBypassExternalProcessPaths)}};
                status->routingRules += rule;
            }
        }

        // geopath
        auto geoip = FindCoreAsset("geoip.db");
        auto geosite = FindCoreAsset("geosite.db");
        // ЧЕЛОВЕКУ — ЧЕЛОВЕЧЕСКОЕ. Здесь стояло «geoip.db not found», и это
        // ровно то, что он видел вместо подключения: имя файла, о котором он
        // ничего не знает, без единого слова о том, что делать. Причина всегда
        // одна — сборка пришла неполной, — и лечится она переустановкой.
        if (geoip.isEmpty() || geosite.isEmpty()) {
            status->result->error =
                QObject::tr("Не хватает файлов с базой сетевых адресов — сборка пришла неполной. "
                            "Переустановите приложение, скачав его заново.");
        }

        // final add routing rule
        // Порядок сборки: сначала поимённый выбор человека, затем цепочка пресета,
        // затем его же общие правила, затем всё остальное.
        auto routingRules = status->routingRulesDns;
        QJSONARRAY_ADD(routingRules, status->routingRulesFirst)
        QJSONARRAY_ADD(routingRules, QString2QJsonObject(dataStore->routing->custom)["rules"].toArray())
        if (status->forTest) routingRules = {};
        if (!status->forTest) QJSONARRAY_ADD(routingRules, QString2QJsonObject(dataStore->custom_route_global)["rules"].toArray())
        QJSONARRAY_ADD(routingRules, status->routingRules)
        auto routeObj = QJsonObject{
            {"rules", routingRules},
            {"auto_detect_interface", dataStore->spmode_vpn}, // TODO force enable?
            {
                "geoip",
                QJsonObject{
                    {"path", geoip},
                },
            },
            {
                "geosite",
                QJsonObject{
                    {"path", geosite},
                },
            }};
        if (!status->forTest) routeObj["final"] = dataStore->routing->def_outbound;
        if (status->forExport) {
            routeObj.remove("geoip");
            routeObj.remove("geosite");
            routeObj.remove("auto_detect_interface");
        }
        status->result->coreConfig.insert("route", routeObj);

        // experimental
        QJsonObject experimentalObj;

        if (!status->forTest) {
            // The connection list (and the route summary built from it) is read from
            // this API — without it the Connections tab is permanently empty, which is
            // how it shipped. Enable it even when the user has not asked for the
            // dashboard: bind to loopback on a port derived from the core's own, and
            // generate a secret so nothing else on the machine can query it.
            const bool userConfigured = dataStore->core_box_clash_api > 0;
            const int port = userConfigured ? dataStore->core_box_clash_api : dataStore->core_port + 1;
            QString secret = dataStore->core_box_clash_api_secret;
            if (secret.isEmpty()) secret = dataStore->core_token;
            QJsonObject clash_api = {
                {"external_controller", "127.0.0.1:" + Int2String(port)},
                {"secret", secret},
            };
            // The bundled web dashboard is only served when the user turned the API on
            // deliberately; the internal one exists to answer /connections, nothing more.
            if (userConfigured) clash_api["external_ui"] = "dashboard";
            experimentalObj["clash_api"] = clash_api;
        }

        if (!experimentalObj.isEmpty()) status->result->coreConfig.insert("experimental", experimentalObj);
    }

    QString WriteVPNSingBoxConfig() {
        // tun user rule
        auto match_out = dataStore->vpn_rule_white ? "neko-socks" : "direct";
        auto no_match_out = dataStore->vpn_rule_white ? "direct" : "neko-socks";

        QString process_name_rule = dataStore->vpn_rule_process.trimmed();
        if (!process_name_rule.isEmpty()) {
            auto arr = SplitLinesSkipSharp(process_name_rule);
            QJsonObject rule{{"outbound", match_out},
                             {"process_name", QList2QJsonArray(arr)}};
            process_name_rule = "," + QJsonObject2QString(rule, false);
        }

        QString cidr_rule = dataStore->vpn_rule_cidr.trimmed();
        if (!cidr_rule.isEmpty()) {
            auto arr = SplitLinesSkipSharp(cidr_rule);
            QJsonObject rule{{"outbound", match_out},
                             {"ip_cidr", QList2QJsonArray(arr)}};
            cidr_rule = "," + QJsonObject2QString(rule, false);
        }

        // Исключения маршрутов, дописанные человеком. Идут СВЕРХ встроенного
        // списка в шаблоне — затирать защиту домашней сети одной строчкой в
        // настройках нельзя. Запятая ведущая: список в шаблоне уже непустой.
        QString route_exclude_extra;
        for (const auto &extra: SplitLinesSkipSharp(dataStore->vpn_route_exclude_extra)) {
            const auto one = extra.trimmed();
            if (one.isEmpty()) continue;
            route_exclude_extra += ",\"" + one + "\"";
        }

        // TODO bypass ext core process path?

        // auth
        QString socks_user_pass;
        if (dataStore->inbound_auth->NeedAuth()) {
            socks_user_pass = R"( "username": "%1", "password": "%2", )";
            socks_user_pass = socks_user_pass.arg(dataStore->inbound_auth->username, dataStore->inbound_auth->password);
        }
        // gen config
        auto configFn = ":/neko/vpn/sing-box-vpn.json";
        if (QFile::exists("vpn/sing-box-vpn.json")) configFn = "vpn/sing-box-vpn.json";
        auto config = ReadFileText(configFn)
                          .replace("//%ROUTE_EXCLUDE_EXTRA%", route_exclude_extra)
                          .replace("//%IPV6_ADDRESS%", dataStore->vpn_ipv6 ? R"("inet6_address": "fdfe:dcba:9876::1/126",)" : "")
                          .replace("//%SOCKS_USER_PASS%", socks_user_pass)
                          .replace("//%PROCESS_NAME_RULE%", process_name_rule)
                          .replace("//%CIDR_RULE%", cidr_rule)
                          .replace("%MTU%", Int2String(dataStore->vpn_mtu))
                          .replace("%STACK%", Preset::SingBox::VpnImplementation.value(dataStore->vpn_implementation))
                          // Имя интерфейса — строкой целиком, а не значением:
                          // на macOS его нет, и «"interface_name": ""» sing-tun
                          // не примет. Отсутствующее поле и пустое поле — разные
                          // вещи, и здесь нужна именно первая.
                          .replace("//%TUN_NAME%", genTunName().isEmpty()
                                                       ? ""
                                                       : R"("interface_name": ")" + genTunName() + R"(",)")
                          .replace("%STRICT_ROUTE%", dataStore->vpn_strict_route ? "true" : "false")
                          .replace("%FINAL_OUT%", no_match_out)
                          .replace("%DNS_ADDRESS%", BOX_UNDERLYING_DNS)
                          .replace("%FAKE_DNS_INBOUND%", dataStore->fake_dns ? "tun-in" : "empty")
                          .replace("%PORT%", Int2String(dataStore->inbound_socks_port));
        // write config
        QFile file;
        file.setFileName(QFileInfo(configFn).fileName());
        file.open(QIODevice::ReadWrite | QIODevice::Truncate);
        file.write(config.toUtf8());
        file.close();
        return QFileInfo(file).absoluteFilePath();
    }

    QString WriteVPNLinuxScript(const QString &configPath) {
#ifdef Q_OS_WIN
        return {};
#endif
        // gen script
        auto scriptFn = ":/neko/vpn/vpn-run-root.sh";
        if (QFile::exists("vpn/vpn-run-root.sh")) scriptFn = "vpn/vpn-run-root.sh";
        auto script = ReadFileText(scriptFn)
                          .replace("./greenrhythm_core", QApplication::applicationDirPath() + "/greenrhythm_core")
                          .replace("$CONFIG_PATH", configPath);
        // write script
        QFile file2;
        file2.setFileName(QFileInfo(scriptFn).fileName());
        file2.open(QIODevice::ReadWrite | QIODevice::Truncate);
        file2.write(script.toUtf8());
        file2.close();
        return QFileInfo(file2).absoluteFilePath();
    }

} // namespace NekoGui
