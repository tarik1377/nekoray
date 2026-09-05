#include "NekoGui.hpp"
#include "fmt/Preset.hpp"

#include <QFile>
#include <QDir>
#include <QApplication>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#ifdef Q_OS_WIN
#include "sys/windows/guihelper.h"
#else
#ifdef Q_OS_LINUX
#include <sys/linux/LinuxCap.h>
#endif
#include <unistd.h>
#endif

namespace NekoGui_ConfigItem {

    // 添加关联
    void JsonStore::_add(configItem *item) {
        _map.insert(item->name, std::shared_ptr<configItem>(item));
    }

    QString JsonStore::_name(void *p) {
        for (const auto &_item: _map) {
            if (_item->ptr == p) return _item->name;
        }
        return {};
    }

    std::shared_ptr<configItem> JsonStore::_get(const QString &name) {
        // 直接 [] 会设置一个 nullptr ，所以先判断是否存在
        if (_map.contains(name)) {
            return _map[name];
        }
        return nullptr;
    }

    void JsonStore::_setValue(const QString &name, void *p) {
        auto item = _get(name);
        if (item == nullptr) return;

        switch (item->type) {
            case itemType::string:
                *(QString *) item->ptr = *(QString *) p;
                break;
            case itemType::boolean:
                *(bool *) item->ptr = *(bool *) p;
                break;
            case itemType::integer:
                *(int *) item->ptr = *(int *) p;
                break;
            case itemType::integer64:
                *(long long *) item->ptr = *(long long *) p;
                break;
            // others...
            case stringList:
            case integerList:
            case jsonStore:
                break;
        }
    }

    QJsonObject JsonStore::ToJson(const QStringList &without) {
        QJsonObject object;
        for (const auto &_item: _map) {
            auto item = _item.get();
            if (without.contains(item->name)) continue;
            switch (item->type) {
                case itemType::string:
                    // Allow Empty
                    if (!((QString *) item->ptr)->isEmpty()) {
                        object.insert(item->name, *(QString *) item->ptr);
                    }
                    break;
                case itemType::integer:
                    object.insert(item->name, *(int *) item->ptr);
                    break;
                case itemType::integer64:
                    object.insert(item->name, *(long long *) item->ptr);
                    break;
                case itemType::boolean:
                    object.insert(item->name, *(bool *) item->ptr);
                    break;
                case itemType::stringList:
                    object.insert(item->name, QList2QJsonArray<QString>(*(QList<QString> *) item->ptr));
                    break;
                case itemType::integerList:
                    object.insert(item->name, QList2QJsonArray<int>(*(QList<int> *) item->ptr));
                    break;
                case itemType::jsonStore:
                    // _add 时应关联对应 JsonStore 的指针
                    object.insert(item->name, ((JsonStore *) item->ptr)->ToJson());
                    break;
            }
        }
        return object;
    }

    QByteArray JsonStore::ToJsonBytes() {
        QJsonDocument document;
        document.setObject(ToJson());
        return document.toJson(save_control_compact ? QJsonDocument::Compact : QJsonDocument::Indented);
    }

    void JsonStore::FromJson(QJsonObject object) {
        for (const auto &key: object.keys()) {
            if (_map.count(key) == 0) {
                continue;
            }

            auto value = object[key];
            auto item = _map[key].get();

            if (item == nullptr)
                continue; // 故意忽略

            // 根据类型修改ptr的内容
            switch (item->type) {
                case itemType::string:
                    if (value.type() != QJsonValue::String) {
                        continue;
                    }
                    *(QString *) item->ptr = value.toString();
                    break;
                case itemType::integer:
                    if (value.type() != QJsonValue::Double) {
                        continue;
                    }
                    *(int *) item->ptr = value.toInt();
                    break;
                case itemType::integer64:
                    if (value.type() != QJsonValue::Double) {
                        continue;
                    }
                    *(long long *) item->ptr = value.toDouble();
                    break;
                case itemType::boolean:
                    if (value.type() != QJsonValue::Bool) {
                        continue;
                    }
                    *(bool *) item->ptr = value.toBool();
                    break;
                case itemType::stringList:
                    if (value.type() != QJsonValue::Array) {
                        continue;
                    }
                    *(QList<QString> *) item->ptr = QJsonArray2QListString(value.toArray());
                    break;
                case itemType::integerList:
                    if (value.type() != QJsonValue::Array) {
                        continue;
                    }
                    *(QList<int> *) item->ptr = QJsonArray2QListInt(value.toArray());
                    break;
                case itemType::jsonStore:
                    if (value.type() != QJsonValue::Object) {
                        continue;
                    }
                    ((JsonStore *) item->ptr)->FromJson(value.toObject());
                    break;
            }
        }

        if (callback_after_load != nullptr) callback_after_load();
    }

    void JsonStore::FromJsonBytes(const QByteArray &data) {
        QJsonParseError error{};
        auto document = QJsonDocument::fromJson(data, &error);

        if (error.error != error.NoError) {
            qDebug() << "QJsonParseError" << error.errorString();
            return;
        }

        FromJson(document.object());
    }

    bool JsonStore::Save() {
        if (callback_before_save != nullptr) callback_before_save();
        if (save_control_no_save) return false;

        auto save_content = ToJsonBytes();
        auto changed = last_save_content != save_content;
        last_save_content = save_content;

        QFile file;
        file.setFileName(fn);
        file.open(QIODevice::ReadWrite | QIODevice::Truncate);
        file.write(save_content);
        file.close();

        return changed;
    }

    bool JsonStore::Load() {
        QFile file;
        file.setFileName(fn);

        if (!file.exists() && !load_control_must) {
            return false;
        }

        bool ok = file.open(QIODevice::ReadOnly);
        if (!ok) {
            MessageBoxWarning("error", "can not open config " + fn + "\n" + file.errorString());
        } else {
            last_save_content = file.readAll();
            FromJsonBytes(last_save_content);
        }

        file.close();
        return ok;
    }

} // namespace NekoGui_ConfigItem

namespace NekoGui {

    DataStore *dataStore = new DataStore();

    // datastore

    DataStore::DataStore() : JsonStore() {
        _add(new configItem("extraCore", dynamic_cast<JsonStore *>(extraCore), itemType::jsonStore));
        _add(new configItem("inbound_auth", dynamic_cast<JsonStore *>(inbound_auth), itemType::jsonStore));

        _add(new configItem("user_agent2", &user_agent, itemType::string));
        _add(new configItem("test_url", &test_latency_url, itemType::string));
        _add(new configItem("test_url_dl", &test_download_url, itemType::string));
        _add(new configItem("test_dl_timeout", &test_download_timeout, itemType::integer));
        _add(new configItem("current_group", &current_group, itemType::integer));
        _add(new configItem("inbound_address", &inbound_address, itemType::string));
        _add(new configItem("inbound_socks_port", &inbound_socks_port, itemType::integer));
        _add(new configItem("log_level", &log_level, itemType::string));
        _add(new configItem("mux_protocol", &mux_protocol, itemType::string));
        _add(new configItem("mux_concurrency", &mux_concurrency, itemType::integer));
        _add(new configItem("mux_padding", &mux_padding, itemType::boolean));
        _add(new configItem("mux_default_on", &mux_default_on, itemType::boolean));
        _add(new configItem("auto_failover", &auto_failover, itemType::boolean));
        _add(new configItem("connection_autopilot", &connection_autopilot, itemType::boolean));
        _add(new configItem("traffic_loop_interval", &traffic_loop_interval, itemType::integer));
        _add(new configItem("test_concurrent", &test_concurrent, itemType::integer));
        _add(new configItem("theme", &theme, itemType::string));
        _add(new configItem("custom_inbound", &custom_inbound, itemType::string));
        _add(new configItem("custom_route", &custom_route_global, itemType::string));
        _add(new configItem("sub_use_proxy", &sub_use_proxy, itemType::boolean));
        _add(new configItem("remember_id", &remember_id, itemType::integer));
        _add(new configItem("remember_enable", &remember_enable, itemType::boolean));
        _add(new configItem("language", &language, itemType::integer));
        _add(new configItem("spmode2", &remember_spmode, itemType::stringList));
        _add(new configItem("skip_cert", &skip_cert, itemType::boolean));
        _add(new configItem("hk_mw", &hotkey_mainwindow, itemType::string));
        _add(new configItem("hk_group", &hotkey_group, itemType::string));
        _add(new configItem("hk_route", &hotkey_route, itemType::string));
        _add(new configItem("hk_spmenu", &hotkey_system_proxy_menu, itemType::string));
        _add(new configItem("fakedns", &fake_dns, itemType::boolean));
        _add(new configItem("active_routing", &active_routing, itemType::string));
        _add(new configItem("mw_size", &mw_size, itemType::string));
        _add(new configItem("conn_stat", &connection_statistics, itemType::boolean));
        _add(new configItem("vpn_impl", &vpn_implementation, itemType::integer));
        _add(new configItem("vpn_mtu", &vpn_mtu, itemType::integer));
        _add(new configItem("vpn_ipv6", &vpn_ipv6, itemType::boolean));
        _add(new configItem("vpn_hide_console", &vpn_hide_console, itemType::boolean));
        _add(new configItem("vpn_strict_route", &vpn_strict_route, itemType::boolean));
        _add(new configItem("macos_mode_explained", &macos_mode_explained, itemType::boolean));
        _add(new configItem("vpn_route_exclude_extra", &vpn_route_exclude_extra, itemType::string));
        _add(new configItem("vpn_bypass_process", &vpn_rule_process, itemType::string));
        _add(new configItem("vpn_bypass_cidr", &vpn_rule_cidr, itemType::string));
        _add(new configItem("vpn_proxy_process", &vpn_rule_process_proxy, itemType::string));
        _add(new configItem("games_via_tunnel", &games_via_tunnel, itemType::boolean));
        _add(new configItem("dpi_fragment", &dpi_fragment, itemType::boolean));
        _add(new configItem("dpi_module_enabled", &dpi_module_enabled, itemType::boolean));
        _add(new configItem("dpi_module_strategy", &dpi_module_strategy, itemType::string));
        _add(new configItem("vpn_rule_white", &vpn_rule_white, itemType::boolean));
        _add(new configItem("check_include_pre", &check_include_pre, itemType::boolean));
        _add(new configItem("sp_format", &system_proxy_format, itemType::string));
        _add(new configItem("sub_clear", &sub_clear, itemType::boolean));
        _add(new configItem("sub_insecure", &sub_insecure, itemType::boolean));
        _add(new configItem("sub_auto_update", &sub_auto_update, itemType::integer));
        _add(new configItem("sub_auto_update_migrated", &sub_auto_update_migrated, itemType::boolean));
        _add(new configItem("conn_stat_migrated", &conn_stat_migrated, itemType::boolean));
        _add(new configItem("routing_quic_migrated", &routing_quic_migrated, itemType::boolean));
        _add(new configItem("routing_games_migrated", &routing_games_migrated, itemType::boolean));
        _add(new configItem("routing_launcher_migrated", &routing_launcher_migrated, itemType::boolean));
        _add(new configItem("log_ignore", &log_ignore, itemType::stringList));
        _add(new configItem("start_minimal", &start_minimal, itemType::boolean));
        _add(new configItem("max_log_line", &max_log_line, itemType::integer));
        _add(new configItem("splitter_state", &splitter_state, itemType::string));
        _add(new configItem("onboarding_completed", &onboarding_completed, itemType::boolean));
        _add(new configItem("utlsFingerprint", &utlsFingerprint, itemType::string));
        _add(new configItem("core_box_clash_api", &core_box_clash_api, itemType::integer));
        _add(new configItem("core_box_clash_api_secret", &core_box_clash_api_secret, itemType::string));
        _add(new configItem("core_box_underlying_dns", &core_box_underlying_dns, itemType::string));
        _add(new configItem("vpn_internal_tun", &vpn_internal_tun, itemType::boolean));
    }

    void DataStore::UpdateStartedId(int id) {
        started_id = id;
        if (remember_enable) {
            remember_id = id;
            Save();
        } else if (remember_id >= 0) {
            remember_id = -1919;
            Save();
        }
    }

    QString DataStore::GetUserAgent(bool isDefault) const {
        if (user_agent.isEmpty()) {
            isDefault = true;
        }
        if (isDefault) {
            QString version = SubStrBefore(NKR_VERSION, "-");
            if (!version.contains(".")) version = "2.0";
            return "GreenRhythm/PC/" + version + " (Prefer ClashMeta Format)";
        }
        return user_agent;
    }

    // preset routing
    Routing::Routing(int preset) : JsonStore() {
        if (preset == 1) {
            def_outbound = "proxy";
            direct_dns = "77.88.8.8";
            direct_dns_strategy = "ipv4_only";
            remote_dns = "https://1.1.1.1/dns-query";
            remote_dns_strategy = "ipv4_only";
            dns_routing = true;
            // Direct/bypass traffic must resolve names locally. With "proxy" here every
            // unmatched DNS query is tunnelled abroad and back, adding seconds of latency
            // to the very domains we deliberately route direct (RU sites, games, launchers).
            dns_final_out = "bypass";
            sniffing_mode = 1;
            block_domain =
                "geosite:category-ads-all\n"
                "domain:appcenter.ms";
            block_ip = "";
            direct_domain =
                "domain:ru\n"
                "domain:su\n"
                "domain:рф\n"
                "domain:xn--p1ai\n"
                "domain:vk.com\n"
                "domain:vkontakte.ru\n"
                "domain:userapi.com\n"
                "domain:vk-cdn.net\n"
                "domain:vkuseraudio.net\n"
                "domain:vkuservideo.net\n"
                "domain:yandex.com\n"
                "domain:yandex.net\n"
                "domain:yastatic.net\n"
                "domain:yastat.net\n"
                "domain:yandex-team.ru\n"
                "domain:mail.ru\n"
                "domain:mradx.net\n"
                "domain:mycdn.me\n"
                "domain:imgsmail.ru\n"
                "domain:ok.ru\n"
                "domain:odnoklassniki.ru\n"
                "domain:okcdn.ru\n"
                "domain:2gis.com\n"
                "domain:2gis.ru\n"
                "domain:kaspersky.com\n"
                "domain:sberbank.com\n"
                "domain:tinkoff.ru\n"
                "domain:gazprom.com\n"
                "domain:wildberries.ru\n"
                "domain:ozon.ru\n"
                "domain:avito.ru\n"
                "domain:drom.ru\n"
                "domain:ivi.ru\n"
                "domain:kinopoisk.ru\n"
                "domain:ipinfo.io\n"
                "domain:msftconnecttest.com\n"
                "domain:msftncsi.com\n"
                "domain:windowsupdate.com\n"
                "domain:windows.com\n"
                "domain:microsoft.com\n"
                "domain:microsoftonline.com\n"
                "domain:office.com\n"
                "domain:office365.com\n"
                "domain:live.com\n"
                "domain:outlook.com\n"
                "domain:msn.com\n"
                "domain:bing.com\n"
                "domain:skype.com\n"
                // Gaming: these break when the account/session IP differs between the game
                // and its auth services, and Epic/Cloudflare challenges datacenter IPs.
                "domain:epicgames.com\n"
                "domain:epicgames.dev\n"
                "domain:epicgames.net\n"
                "domain:on.epicgames.com\n"
                "domain:ol.epicgames.com\n"
                "domain:unrealengine.com\n"
                "domain:easyanticheat.net\n"
                "domain:eac-cdn.com\n"
                "domain:xboxlive.com\n"
                "domain:xbox.com\n"
                "domain:seaofthieves.com\n"
                "domain:rare.co.uk\n"
                "domain:playfabapi.com\n"
                "domain:steampowered.com\n"
                "domain:steamcommunity.com\n"
                "domain:steamserver.net\n"
                "domain:steamcontent.com";
            direct_ip =
                "geoip:ru\n"
                "geoip:private";
            proxy_domain = "";
            proxy_ip = "";
            // Rule order matters — first match wins.
            //
            // 1. Block QUIC, by port first. Chrome/Opera send almost everything over
            //    HTTP/3 (UDP 443). Tunnelled QUIC is unreliable, and browsers do NOT
            //    fall back to TCP when it silently stalls — the user just sees pages
            //    that never load while the log fills with "outbound/vless[proxy]:
            //    outbound packet connection" to :443 and nothing comes back.
            //    "protocol":"quic" alone is not enough: it needs sniffing, and a user
            //    who turned sniffing off gets no match and no clue why. udp/443 matches
            //    regardless, so it goes first and the protocol rule stays as backup.
            //    Public resolvers are exempted BEFORE the block: Chrome's "Secure DNS"
            //    speaks DoH over QUIC to 8.8.8.8:443, so a blanket udp/443 block leaves
            //    it unable to resolve anything and the browser opens no TCP at all —
            //    which looks exactly like the bug we were trying to fix.
            // 2. ИГРЫ И ИХ ПОМОЩНИКИ ИДУТ ВЫХОДОМ «bypass». Слово здесь — только
            //    ярлык: в собранном конфиге и «bypass», и «direct» — это один и тот
            //    же {"type":"direct"} без единого отличающегося поля (см.
            //    db/ConfigBuilder.cpp, где оба объявляются). Раньше тут стояло,
            //    что «direct» переинжектит пакеты через TUN, — это было неверно;
            //    решает ПОРЯДОК правил, а не имя выхода. Ярлык при этом трогать
            //    нельзя: на строку "bypass" завязаны миграция схем, сторож порядка
            //    и счётчик прямого трафика.
            //    Что верно по существу: каждый помощник игры обязан выходить с того
            //    же адреса, что и она сама, иначе вход и античит отказывают (так
            //    ломался Sea of Thieves — «Lavenderbeard»).
            // 3. НО ЛАУНЧЕР EPIC — НЕ ИГРА, И ЕМУ НАПРЯМУЮ НЕЛЬЗЯ. Сеть Epic не
            //    пускает с прямого адреса: лаунчер отвечает «ошибка соединения» и
            //    предлагает автономный режим. Проверено по журналу — все его
            //    соединения уходили мимо туннеля, к CDN и API, которых нет в списке
            //    прямых доменов. Поэтому его здесь нет и быть не должно; Squad же
            //    покупается в Steam, и лаунчер Epic ему не нужен вовсе.
            //    Помощник EpicOnlineServicesUserHelper.exe убран по той же причине,
            //    а заодно потому, что за всё наблюдение не встретился ни разу: это
            //    было угаданное имя, а угаданные имена мы больше не возим.
            // 4. ШАБЛОН ЛОВИТ «-Shipping.exe», А НЕ «-Win64-Shipping.exe».
            //    Раньше здесь стояли две узкие строки — -Win64-Shipping и
            //    -WinGDK-Shipping, — и они ловили только сам исполняемый файл игры.
            //    У игры на Unreal рядом с ним стоит второй, СВОЙ лаунчер, и суффикс
            //    платформы в его имени отсутствует: у Wardogs это
            //    WardogsLauncher-Shipping.exe. Под старый шаблон он не подходил и
            //    уходил В ТУННЕЛЬ, тогда как сама игра шла мимо него.
            //
            //    Ровно этот раскол пункт 2 и запрещает. Лаунчер разговаривал с
            //    сервером античита с зарубежного адреса, игра предъявляла
            //    российский — и снаружи это выглядит как вечный экран загрузки, где
            //    жаловаться не на что: сеть цела, сервер жив, обход «настроен».
            //    Дописать имя игры руками в таком положении бесполезно вдвойне —
            //    имя и так уже ловилось шаблоном, а ломала не игра, а её лаунчер.
            //
            //    Суффикс -Shipping — это конфигурация сборки Unreal, а не имя
            //    платформы, и он есть у обоих. Одна строка вместо двух покрывает и
            //    прежние два случая, и лаунчеры игр, которых мы ещё не видели.
            custom = "{\"rules\":[{\"outbound\":\"bypass\",\"process_name\":[\"SquadGame-Win64-Shipping.exe\",\"SquadGame.exe\",\"Squad.exe\",\"EasyAntiCheat.exe\",\"EasyAntiCheat_EOS.exe\",\"EACLauncher.exe\",\"BEService.exe\",\"BEService_x64.exe\",\"SoTGame.exe\",\"SoTLauncher.exe\",\"UnrealCEFSubProcess.exe\",\"XboxPcAppFT.exe\",\"XboxPcApp.exe\",\"GameBarPresenceWriter.exe\",\"GamingServices.exe\",\"GamingServicesNet.exe\",\"XboxIdentityProvider.exe\",\"steam.exe\",\"steamwebhelper.exe\",\"steamservice.exe\",\"BsgLauncher.exe\",\"EscapeFromTarkov.exe\"]},{\"outbound\":\"bypass\",\"process_path_regex\":[\"(?i)-Shipping\\\\.exe$\"]},{\"network\":\"icmp\",\"outbound\":\"bypass\"},{\"network\":\"udp\",\"port\":443,\"ip_cidr\":[\"8.8.8.8/32\",\"8.8.4.4/32\",\"1.1.1.1/32\",\"1.0.0.1/32\",\"9.9.9.9/32\",\"77.88.8.8/32\",\"77.88.8.1/32\"],\"outbound\":\"direct\"},{\"network\":\"udp\",\"port\":443,\"outbound\":\"block\"},{\"protocol\":\"quic\",\"outbound\":\"block\"},{\"outbound\":\"proxy\",\"process_name\":[\"Discord.exe\",\"discord.exe\",\"Telegram.exe\",\"telegram.exe\",\"Codex.exe\",\"codex.exe\",\"Claude.exe\",\"claude.exe\",\"claude-code.exe\"]}]}";
        }
        if (!Preset::SingBox::DomainStrategy.contains(domain_strategy)) domain_strategy = "";
        if (!Preset::SingBox::DomainStrategy.contains(outbound_domain_strategy)) outbound_domain_strategy = "";
        _add(new configItem("direct_ip", &this->direct_ip, itemType::string));
        _add(new configItem("direct_domain", &this->direct_domain, itemType::string));
        _add(new configItem("proxy_ip", &this->proxy_ip, itemType::string));
        _add(new configItem("proxy_domain", &this->proxy_domain, itemType::string));
        _add(new configItem("block_ip", &this->block_ip, itemType::string));
        _add(new configItem("block_domain", &this->block_domain, itemType::string));
        _add(new configItem("def_outbound", &this->def_outbound, itemType::string));
        _add(new configItem("custom", &this->custom, itemType::string));
        //
        _add(new configItem("remote_dns", &this->remote_dns, itemType::string));
        _add(new configItem("remote_dns_strategy", &this->remote_dns_strategy, itemType::string));
        _add(new configItem("direct_dns", &this->direct_dns, itemType::string));
        _add(new configItem("direct_dns_strategy", &this->direct_dns_strategy, itemType::string));
        _add(new configItem("domain_strategy", &this->domain_strategy, itemType::string));
        _add(new configItem("outbound_domain_strategy", &this->outbound_domain_strategy, itemType::string));
        _add(new configItem("dns_routing", &this->dns_routing, itemType::boolean));
        _add(new configItem("sniffing_mode", &this->sniffing_mode, itemType::integer));
        _add(new configItem("use_dns_object", &this->use_dns_object, itemType::boolean));
        _add(new configItem("dns_object", &this->dns_object, itemType::string));
        _add(new configItem("dns_final_out", &this->dns_final_out, itemType::string));
    }

    QString Routing::DisplayRouting() const {
        return QStringLiteral("[Proxy] %1\n[Proxy] %2\n[Direct] %3\n[Direct] %4\n[Block] %5\n[Block] %6\n[Default Outbound] %7\n[DNS] %8")
            .arg(SplitLinesSkipSharp(proxy_domain).join(","), 10)
            .arg(SplitLinesSkipSharp(proxy_ip).join(","), 10)
            .arg(SplitLinesSkipSharp(direct_domain).join(","), 10)
            .arg(SplitLinesSkipSharp(direct_ip).join(","), 10)
            .arg(SplitLinesSkipSharp(block_domain).join(","), 10)
            .arg(SplitLinesSkipSharp(block_ip).join(","), 10)
            .arg(def_outbound)
            .arg(use_dns_object ? "DNS Object" : "Simple DNS");
    }

    QStringList Routing::List() {
        QDir dr(ROUTES_PREFIX);
        return dr.entryList(QDir::Files);
    }

    // A rule counts as the QUIC guard if it is the sniffed-protocol rule or any udp/443
    // rule — the port may be a single number or a list, and a user may well have written
    // their own variant. Used both to detect "already guarded" and to lift the rules out
    // of the shipped preset, so the two can never disagree.
    static bool isQuicRule(const QJsonObject &o) {
        if (o["protocol"].toString() == "quic") return true;
        if (o["network"].toString() != "udp") return false;
        const auto p = o["port"];
        if (p.isDouble()) return p.toInt() == 443;
        if (p.isArray()) {
            for (const auto &v: p.toArray()) {
                if (v.toInt() == 443) return true;
            }
        }
        return false;
    }

    // Existing installs keep their own routes_box file, and no template change can reach
    // it — which is why the QUIC block, the fix for the most common "подключается, но не
    // грузится", never arrived for anyone who had already run the app once.
    //
    // Deliberately conservative, because this edits a file the customer may have curated:
    //  - the QUIC rules are PREPENDED (order matters, first match wins) and only when the
    //    scheme has no udp/443 or protocol:quic rule of its own;
    //  - dns_final_out moves only from the exact old default "proxy", so anyone who chose
    //    something else keeps it;
    //  - a custom rule set we cannot parse is left completely alone — a hand-written
    //    config is worth more than this migration;
    //  - nothing is ever removed, and the domain/IP lists are not touched at all.
    bool Routing::MigrateOne(Routing *r) {
        if (r == nullptr) return false;
        bool changed = false;

        if (r->dns_final_out == "proxy") {
            r->dns_final_out = "bypass";
            changed = true;
        }

        const QString original = r->custom.trimmed();
        auto obj = QString2QJsonObject(original);
        const bool parsed = obj.contains("rules") && obj["rules"].isArray();
        if (!parsed && !original.isEmpty()) return changed; // unreadable: leave it be

        auto rules = obj["rules"].toArray();
        for (const auto &v: rules) {
            if (isQuicRule(v.toObject())) return changed; // already has its own guard
        }

        // Copy the rules out of the shipped preset rather than repeating them here, so a
        // future change to the preset cannot leave this migration inserting stale rules.
        Routing preset(1);
        QJsonArray quic;
        for (const auto &v: QString2QJsonObject(preset.custom)["rules"].toArray()) {
            if (isQuicRule(v.toObject())) quic.append(v);
        }
        if (quic.isEmpty()) return changed;

        for (int i = quic.size() - 1; i >= 0; i--) rules.prepend(quic[i]);
        obj["rules"] = rules;
        r->custom = QJsonObject2QString(obj, true);
        return true;
    }

    // ПОРЯДОК РЕШАЕТ. sing-box берёт первое совпавшее правило, а список игр «мимо
    // туннеля» стоял ПОСЛЕ блокировки udp/443. Игре, которой этот порт нужен, обход
    // не помогал вовсе: пакеты гасились раньше, чем доходили до списка. Так пропал
    // список серверов в Squad — он берёт его через Epic Online Services, а те ходят
    // по udp/443; на глаз это выглядит как «клиент не видит серверов», и никакая
    // галка обхода этого не лечила.
    //
    // Осторожность та же, что и в MigrateOne: чужую цепочку, которую не удалось
    // разобрать, не трогаем; ничего не удаляем; имена только дописываем.
    bool Routing::MigrateGameBypass(Routing *r) {
        if (r == nullptr) return false;
        const QString original = r->custom.trimmed();
        if (original.isEmpty()) return false;
        auto obj = QString2QJsonObject(original);
        if (!obj.contains("rules") || !obj["rules"].isArray()) return false;

        auto rules = obj["rules"].toArray();
        int firstBlock = -1;
        for (int i = 0; i < rules.size(); i++) {
            const auto o = rules[i].toObject();
            if (isQuicRule(o) && o["outbound"].toString() == "block") {
                firstBlock = i;
                break;
            }
        }
        if (firstBlock < 0) return false; // блокировки нет — переставлять нечего

        // Имена и шаблоны берём из пресета, а не повторяем здесь: иначе следующая
        // правка пресета оставит миграцию раздавать вчерашний список.
        Routing preset(1);
        bool changed = false;
        for (const auto &v: QString2QJsonObject(preset.custom)["rules"].toArray()) {
            const auto want = v.toObject();
            if (want["outbound"].toString() != "bypass") continue;
            QString field;
            for (const auto &candidate: {QStringLiteral("process_name"),
                                        QStringLiteral("process_path_regex"),
                                        QStringLiteral("network")}) {
                if (want.contains(candidate)) {
                    field = candidate;
                    break;
                }
            }
            if (field.isEmpty()) continue;

            // Значения дописываем только у правил по процессу. Правило по СЕТИ
            // (оно нужно для ICMP: у него нет порта, процесс не определяется, и
            // поймать его может лишь сеть) дописывать в чужое нельзя — «icmp»,
            // подсунутый в чужое правило про udp, поменял бы его смысл. Такое
            // правило либо уже есть, либо кладётся отдельным.
            const bool mergeable = field != QStringLiteral("network");

            // Своё правило ищем по ТОМУ ЖЕ полю. Имя и шаблон обязаны жить в разных
            // правилах: внутри одного правила поля складываются по «И», и правило,
            // требующее совпадения сразу обоих, не сработает никогда.
            int at = -1;
            for (int i = 0; i < rules.size(); i++) {
                const auto o = rules[i].toObject();
                if (o["outbound"].toString() != "bypass" || !o.contains(field)) continue;
                if (!mergeable) {
                    // Своим считаем только правило с ТЕМ ЖЕ значением, иначе
                    // «нашлось» бы первое попавшееся правило про сеть.
                    bool same = false;
                    const auto mineValues = o[field].isArray() ? o[field].toArray()
                                                               : QJsonArray{o[field]};
                    for (const auto &n: mineValues) {
                        if (n.toString() == want[field].toString()) same = true;
                    }
                    if (!same) continue;
                }
                at = i;
                break;
            }

            if (at < 0) {
                rules.insert(firstBlock, want);
                firstBlock++;
                changed = true;
                continue;
            }

            if (!mergeable) {
                // Нашлось и совпало по значению — переставить, если оно ниже блока.
                if (at > firstBlock) {
                    const auto mine = rules[at].toObject();
                    rules.removeAt(at);
                    rules.insert(firstBlock, mine);
                    firstBlock++;
                    changed = true;
                }
                continue;
            }

            auto mine = rules[at].toObject();
            auto values = mine[field].toArray();
            QSet<QString> have;
            for (const auto &n: values) have.insert(n.toString());
            for (const auto &n: want[field].toArray()) {
                if (have.contains(n.toString())) continue;
                values.append(n);
                changed = true;
            }
            mine[field] = values;
            if (at > firstBlock) {
                rules.removeAt(at);
                rules.insert(firstBlock, mine);
                firstBlock++;
                changed = true;
            } else {
                rules.replace(at, mine);
            }
        }

        if (!changed) return false;
        obj["rules"] = rules;
        r->custom = QJsonObject2QString(obj, true);
        return true;
    }

    // Обход всех схем — один на обе миграции: их две, а способ дойти до каждой
    // схемы один и тот же, и расходиться этим двум обходам незачем.
    static int migrateEach(bool (*fix)(Routing *)) {
        int changed = 0;
        const auto active = dataStore->active_routing;
        for (const auto &name: Routing::List()) {
            // The active scheme is already loaded in memory; patching the file underneath
            // it would be overwritten by the next Save().
            if (name == active && dataStore->routing != nullptr) {
                if (fix(dataStore->routing.get())) {
                    dataStore->routing->Save();
                    changed++;
                }
                continue;
            }
            Routing other;
            other.load_control_must = true;
            other.fn = ROUTES_PREFIX + name;
            if (!other.Load()) continue;
            if (fix(&other)) {
                other.Save();
                changed++;
            }
        }
        return changed;
    }

    int Routing::MigrateAll() {
        return migrateEach(&Routing::MigrateOne);
    }

    int Routing::MigrateGamesAll() {
        return migrateEach(&Routing::MigrateGameBypass);
    }

    bool Routing::SetToActive(const QString &name) {
        NekoGui::dataStore->routing = std::make_unique<Routing>();
        NekoGui::dataStore->routing->load_control_must = true;
        NekoGui::dataStore->routing->fn = ROUTES_PREFIX + name;
        auto ok = NekoGui::dataStore->routing->Load();
        if (ok) {
            NekoGui::dataStore->active_routing = name;
            NekoGui::dataStore->Save();
        }
        return ok;
    }

    // NO default extra core

    ExtraCore::ExtraCore() : JsonStore() {
        _add(new configItem("core_map", &this->core_map, itemType::string));
    }

    QString ExtraCore::Get(const QString &id) const {
        auto obj = QString2QJsonObject(core_map);
        for (const auto &c: obj.keys()) {
            if (c == id) {
                auto path = obj[id].toString();
                // Only honour a stored path if it still exists — otherwise a stale
                // entry (old install / migration) would shadow the bundled core and
                // break "works out of the box". Fall through to the bundled binary.
                if (!path.isEmpty() && QFile::exists(path)) return path;
                break;
            }
        }
        // Fall back to a core binary shipped next to the app (e.g. a bundled xray.exe),
        // so profiles that need an external core work out of the box without the user
        // having to set the path in Settings → Extra cores. Only used when it exists,
        // so behaviour is unchanged when nothing is bundled.
        auto bundled = QApplication::applicationDirPath() + "/" + id;
#ifdef Q_OS_WIN
        bundled += ".exe";
#endif
        if (QFile::exists(bundled)) return bundled;
        return "";
    }

    void ExtraCore::Set(const QString &id, const QString &path) {
        auto obj = QString2QJsonObject(core_map);
        obj[id] = path;
        core_map = QJsonObject2QString(obj, true);
    }

    void ExtraCore::Delete(const QString &id) {
        auto obj = QString2QJsonObject(core_map);
        obj.remove(id);
        core_map = QJsonObject2QString(obj, true);
    }

    InboundAuthorization::InboundAuthorization() : JsonStore() {
        _add(new configItem("user", &this->username, itemType::string));
        _add(new configItem("pass", &this->password, itemType::string));
    }

    bool InboundAuthorization::NeedAuth() const {
        return !username.trimmed().isEmpty() && !password.trimmed().isEmpty();
    }

    // System Utils

    QString FindCoreAsset(const QString &name) {
        QStringList search{};
        search << QApplication::applicationDirPath();
        search << "/usr/share/sing-geoip";
        search << "/usr/share/sing-geosite";
        search << "/usr/share/sing-box";
        search << "/usr/lib/greenrhythm";
        search << "/usr/share/greenrhythm";
        for (const auto &dir: search) {
            if (dir.isEmpty()) continue;
            QFileInfo asset(dir + "/" + name);
            if (asset.exists()) {
                return asset.absoluteFilePath();
            }
        }
        return {};
    }

    QString FindNekoBoxCoreRealPath() {
        auto fn = QApplication::applicationDirPath() + "/greenrhythm_core";
        auto fi = QFileInfo(fn);
        if (fi.isSymLink()) return fi.symLinkTarget();
        return fn;
    }

    short isAdminCache = -1;

    // IsAdmin 主要判断：有无权限启动 Tun
    bool IsAdmin() {
        if (isAdminCache >= 0) return isAdminCache;

        bool admin = false;
#ifdef Q_OS_WIN
        admin = Windows_IsInAdmin();
#else
#ifdef Q_OS_LINUX
        admin |= Linux_GetCapString(FindNekoBoxCoreRealPath()).contains("cap_net_admin");
#endif
        admin |= geteuid() == 0;
#endif

        isAdminCache = admin;
        return admin;
    };

    bool PlatformSupportsInternalTun() {
#ifdef Q_OS_MACOS
        return false;
#else
        return true;
#endif
    }

    bool UseInternalTun() {
        return dataStore->vpn_internal_tun && PlatformSupportsInternalTun();
    }

} // namespace NekoGui
