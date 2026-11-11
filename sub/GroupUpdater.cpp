#include "db/ProfileFilter.hpp"
#include "fmt/includes.h"
#include "fmt/Preset.hpp"
#include "main/HTTPRequestHelper.hpp"

#include "GroupUpdater.hpp"

#include <QApplication>
#include <QInputDialog>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

#ifndef NKR_NO_YAML

#include <yaml-cpp/yaml.h>

#endif

namespace NekoGui_sub {

    GroupUpdater *groupUpdater = new GroupUpdater;

    static QString NormalizeImport(const QString &input) {
        auto str = input.trimmed();
        if (str.startsWith(QChar(0xfeff))) str = str.mid(1).trimmed();
        return str;
    }

    void RawUpdater_FixEnt(const std::shared_ptr<NekoGui::ProxyEntity> &ent) {
        if (ent == nullptr) return;
        auto stream = NekoGui_fmt::GetStreamSettings(ent->bean.get());
        if (stream == nullptr) return;
        // 1. "security"
        if (stream->security == "none" || stream->security == "0" || stream->security == "false") {
            stream->security = "";
        } else if (stream->security == "1" || stream->security == "true") {
            stream->security = "tls";
        }
        // 2. TLS SNI: v2rayN config builder generate sni like this, so set sni here for their format.
        if (stream->security == "tls" && IsIpAddress(ent->bean->serverAddress) && (!stream->host.isEmpty()) && stream->sni.isEmpty()) {
            stream->sni = stream->host;
        }
    }

    void RawUpdater::AddParsed(const std::shared_ptr<NekoGui::ProxyEntity> &ent) {
        if (collect_only || NekoGui::profileManager->AddProfile(ent, gid_add_to)) updated_order += ent;
    }

    void RawUpdater::update(const QString &input) {
        auto str = NormalizeImport(input);
        if (str.isEmpty()) return;
        // Base64 encoded subscription
        if (auto str2 = DecodeB64IfValid(str); !str2.isEmpty()) {
            update(str2);
            return;
        }

        // Clash
        if (str.contains("proxies:")) {
            updateClash(str);
            return;
        }

        // Multi line
        if (str.count("\n") > 0) {
            auto list = str.split("\n");
            for (const auto &str2: list) {
                update(str2.trimmed());
            }
            return;
        }

        std::shared_ptr<NekoGui::ProxyEntity> ent;
        bool needFix = true;

        // Nekoray format
        if (str.startsWith("nekoray://")) {
            needFix = false;
            auto link = QUrl(str);
            if (!link.isValid()) return;
            ent = NekoGui::ProfileManager::NewProxyEntity(link.host());
            if (ent->bean->version == -114514) return;
            auto j = DecodeB64IfValid(link.fragment().toUtf8(), QByteArray::Base64UrlEncoding);
            if (j.isEmpty()) return;
            ent->bean->FromJsonBytes(j);
        }

        // SOCKS
        if (str.startsWith("socks5://") || str.startsWith("socks4://") ||
            str.startsWith("socks4a://") || str.startsWith("socks://")) {
            ent = NekoGui::ProfileManager::NewProxyEntity("socks");
            auto ok = ent->SocksHTTPBean()->TryParseLink(str);
            if (!ok) return;
        }

        // HTTP
        if (str.startsWith("http://") || str.startsWith("https://")) {
            ent = NekoGui::ProfileManager::NewProxyEntity("http");
            auto ok = ent->SocksHTTPBean()->TryParseLink(str);
            if (!ok) return;
        }

        // ShadowSocks
        if (str.startsWith("ss://")) {
            ent = NekoGui::ProfileManager::NewProxyEntity("shadowsocks");
            auto ok = ent->ShadowSocksBean()->TryParseLink(str);
            if (!ok) return;
        }

        // VMess
        if (str.startsWith("vmess://")) {
            ent = NekoGui::ProfileManager::NewProxyEntity("vmess");
            auto ok = ent->VMessBean()->TryParseLink(str);
            if (!ok) return;
        }

        // VLESS
        if (str.startsWith("vless://")) {
            ent = NekoGui::ProfileManager::NewProxyEntity("vless");
            auto ok = ent->TrojanVLESSBean()->TryParseLink(str);
            if (!ok) return;
        }

        // Trojan
        if (str.startsWith("trojan://")) {
            ent = NekoGui::ProfileManager::NewProxyEntity("trojan");
            auto ok = ent->TrojanVLESSBean()->TryParseLink(str);
            if (!ok) return;
        }

        // Naive
        if (str.startsWith("naive+")) {
            needFix = false;
            ent = NekoGui::ProfileManager::NewProxyEntity("naive");
            auto ok = ent->NaiveBean()->TryParseLink(str);
            if (!ok) return;
        }

        // Hysteria2
        if (str.startsWith("hysteria2://") || str.startsWith("hy2://")) {
            needFix = false;
            ent = NekoGui::ProfileManager::NewProxyEntity("hysteria2");
            auto ok = ent->QUICBean()->TryParseLink(str);
            if (!ok) return;
        }

        // TUIC
        if (str.startsWith("tuic://")) {
            needFix = false;
            ent = NekoGui::ProfileManager::NewProxyEntity("tuic");
            auto ok = ent->QUICBean()->TryParseLink(str);
            if (!ok) return;
        }

        if (ent == nullptr) return;

        // Fix
        if (needFix) RawUpdater_FixEnt(ent);

        // End
        AddParsed(ent);
    }

#ifndef NKR_NO_YAML

    QString Node2QString(const YAML::Node &n, const QString &def = "") {
        try {
            return n.as<std::string>().c_str();
        } catch (const YAML::Exception &ex) {
            qDebug() << ex.what();
            return def;
        }
    }

    QStringList Node2QStringList(const YAML::Node &n) {
        try {
            if (n.IsSequence()) {
                QStringList list;
                for (auto item: n) {
                    list << item.as<std::string>().c_str();
                }
                return list;
            } else {
                return {};
            }
        } catch (const YAML::Exception &ex) {
            qDebug() << ex.what();
            return {};
        }
    }

    int Node2Int(const YAML::Node &n, const int &def = 0) {
        try {
            return n.as<int>();
        } catch (const YAML::Exception &ex) {
            qDebug() << ex.what();
            return def;
        }
    }

    bool Node2Bool(const YAML::Node &n, const bool &def = false) {
        try {
            return n.as<bool>();
        } catch (const YAML::Exception &ex) {
            try {
                return n.as<int>();
            } catch (const YAML::Exception &ex2) {
                qDebug() << ex2.what();
            }
            qDebug() << ex.what();
            return def;
        }
    }

    // NodeChild returns the first defined children or Null Node
    YAML::Node NodeChild(const YAML::Node &n, const std::list<std::string> &keys) {
        for (const auto &key: keys) {
            auto child = n[key];
            if (child.IsDefined()) return child;
        }
        return {};
    }

#endif

    // https://github.com/Dreamacro/clash/wiki/configuration
    void RawUpdater::updateClash(const QString &str) {
#ifndef NKR_NO_YAML
        try {
            auto proxies = YAML::Load(str.toStdString())["proxies"];
            for (auto proxy: proxies) {
                auto type = Node2QString(proxy["type"]).toLower();
                auto type_clash = type;

                if (type == "ss" || type == "ssr") type = "shadowsocks";
                if (type == "socks5") type = "socks";

                auto ent = NekoGui::ProfileManager::NewProxyEntity(type);
                if (ent->bean->version == -114514) continue;
                bool needFix = false;

                // common
                ent->bean->name = Node2QString(proxy["name"]);
                ent->bean->serverAddress = Node2QString(proxy["server"]);
                ent->bean->serverPort = Node2Int(proxy["port"]);

                if (type_clash == "ss") {
                    auto bean = ent->ShadowSocksBean();
                    bean->method = Node2QString(proxy["cipher"]).replace("dummy", "none");
                    bean->password = Node2QString(proxy["password"]);
                    auto plugin_n = proxy["plugin"];
                    auto pluginOpts_n = proxy["plugin-opts"];

                    // UDP over TCP
                    if (Node2Bool(proxy["udp-over-tcp"])) {
                        bean->uot = Node2Int(proxy["udp-over-tcp-version"]);
                        if (bean->uot == 0) bean->uot = 2;
                    }

                    if (plugin_n.IsDefined() && pluginOpts_n.IsDefined()) {
                        QStringList ssPlugin;
                        auto plugin = Node2QString(plugin_n);
                        if (plugin == "obfs") {
                            ssPlugin << "obfs-local";
                            ssPlugin << "obfs=" + Node2QString(pluginOpts_n["mode"]);
                            ssPlugin << "obfs-host=" + Node2QString(pluginOpts_n["host"]);
                        } else if (plugin == "v2ray-plugin") {
                            auto mode = Node2QString(pluginOpts_n["mode"]);
                            auto host = Node2QString(pluginOpts_n["host"]);
                            auto path = Node2QString(pluginOpts_n["path"]);
                            ssPlugin << "v2ray-plugin";
                            if (!mode.isEmpty() && mode != "websocket") ssPlugin << "mode=" + mode;
                            if (Node2Bool(pluginOpts_n["tls"])) ssPlugin << "tls";
                            if (!host.isEmpty()) ssPlugin << "host=" + host;
                            if (!path.isEmpty()) ssPlugin << "path=" + path;
                            // clash only: skip-cert-verify
                            // clash only: headers
                            // clash: mux=?
                        }
                        bean->plugin = ssPlugin.join(";");
                    }

                    // sing-mux
                    auto smux = NodeChild(proxy, {"smux"});
                    if (Node2Bool(smux["enabled"])) bean->stream->multiplex_status = 1;
                } else if (type == "socks" || type == "http") {
                    auto bean = ent->SocksHTTPBean();
                    bean->username = Node2QString(proxy["username"]);
                    bean->password = Node2QString(proxy["password"]);
                    if (Node2Bool(proxy["tls"])) bean->stream->security = "tls";
                    if (Node2Bool(proxy["skip-cert-verify"])) bean->stream->allow_insecure = true;
                } else if (type == "trojan" || type == "vless") {
                    needFix = true;
                    auto bean = ent->TrojanVLESSBean();
                    if (type == "vless") {
                        bean->flow = Node2QString(proxy["flow"]);
                        bean->password = Node2QString(proxy["uuid"]);
                        // meta packet encoding
                        if (Node2Bool(proxy["packet-addr"])) {
                            bean->stream->packet_encoding = "packetaddr";
                        } else {
                            // For VLESS, default to use xudp
                            bean->stream->packet_encoding = "xudp";
                        }
                    } else {
                        bean->password = Node2QString(proxy["password"]);
                    }
                    bean->stream->security = "tls";
                    bean->stream->network = Node2QString(proxy["network"], "tcp");
                    bean->stream->sni = FIRST_OR_SECOND(Node2QString(proxy["sni"]), Node2QString(proxy["servername"]));
                    bean->stream->alpn = Node2QStringList(proxy["alpn"]).join(",");
                    bean->stream->allow_insecure = Node2Bool(proxy["skip-cert-verify"]);
                    bean->stream->utlsFingerprint = Node2QString(proxy["client-fingerprint"]);
                    if (bean->stream->utlsFingerprint.isEmpty()) {
                        bean->stream->utlsFingerprint = NekoGui::dataStore->utlsFingerprint;
                    }

                    // sing-mux
                    auto smux = NodeChild(proxy, {"smux"});
                    if (Node2Bool(smux["enabled"])) bean->stream->multiplex_status = 1;

                    // opts
                    auto ws = NodeChild(proxy, {"ws-opts", "ws-opt"});
                    if (ws.IsMap()) {
                        auto headers = ws["headers"];
                        for (auto header: headers) {
                            if (Node2QString(header.first).toLower() == "host") {
                                bean->stream->host = Node2QString(header.second);
                            }
                        }
                        bean->stream->path = Node2QString(ws["path"]);
                        bean->stream->ws_early_data_length = Node2Int(ws["max-early-data"]);
                        bean->stream->ws_early_data_name = Node2QString(ws["early-data-header-name"]);
                    }

                    auto grpc = NodeChild(proxy, {"grpc-opts", "grpc-opt"});
                    if (grpc.IsMap()) {
                        bean->stream->path = Node2QString(grpc["grpc-service-name"]);
                    }

                    auto reality = NodeChild(proxy, {"reality-opts"});
                    if (reality.IsMap()) {
                        bean->stream->reality_pbk = Node2QString(reality["public-key"]);
                        bean->stream->reality_sid = Node2QString(reality["short-id"]);
                        bean->stream->reality_pqv = Node2QString(reality["mldsa65-verify"]);
                    }
                } else if (type == "vmess") {
                    needFix = true;
                    auto bean = ent->VMessBean();
                    bean->uuid = Node2QString(proxy["uuid"]);
                    bean->aid = Node2Int(proxy["alterId"]);
                    bean->security = Node2QString(proxy["cipher"], bean->security);
                    bean->stream->network = Node2QString(proxy["network"], "tcp").replace("h2", "http");
                    bean->stream->sni = FIRST_OR_SECOND(Node2QString(proxy["sni"]), Node2QString(proxy["servername"]));
                    bean->stream->alpn = Node2QStringList(proxy["alpn"]).join(",");
                    if (Node2Bool(proxy["tls"])) bean->stream->security = "tls";
                    if (Node2Bool(proxy["skip-cert-verify"])) bean->stream->allow_insecure = true;
                    bean->stream->utlsFingerprint = Node2QString(proxy["client-fingerprint"]);
                    bean->stream->utlsFingerprint = Node2QString(proxy["client-fingerprint"]);
                    if (bean->stream->utlsFingerprint.isEmpty()) {
                        bean->stream->utlsFingerprint = NekoGui::dataStore->utlsFingerprint;
                    }

                    // sing-mux
                    auto smux = NodeChild(proxy, {"smux"});
                    if (Node2Bool(smux["enabled"])) bean->stream->multiplex_status = 1;

                    // meta packet encoding
                    if (Node2Bool(proxy["xudp"])) bean->stream->packet_encoding = "xudp";
                    if (Node2Bool(proxy["packet-addr"])) bean->stream->packet_encoding = "packetaddr";

                    // opts
                    auto ws = NodeChild(proxy, {"ws-opts", "ws-opt"});
                    if (ws.IsMap()) {
                        auto headers = ws["headers"];
                        for (auto header: headers) {
                            if (Node2QString(header.first).toLower() == "host") {
                                bean->stream->host = Node2QString(header.second);
                            }
                        }
                        bean->stream->path = Node2QString(ws["path"]);
                        bean->stream->ws_early_data_length = Node2Int(ws["max-early-data"]);
                        bean->stream->ws_early_data_name = Node2QString(ws["early-data-header-name"]);
                        // for Xray
                        if (Node2QString(ws["early-data-header-name"]) == "Sec-WebSocket-Protocol") {
                            bean->stream->path += "?ed=" + Node2QString(ws["max-early-data"]);
                        }
                    }

                    auto grpc = NodeChild(proxy, {"grpc-opts", "grpc-opt"});
                    if (grpc.IsMap()) {
                        bean->stream->path = Node2QString(grpc["grpc-service-name"]);
                    }

                    auto h2 = NodeChild(proxy, {"h2-opts", "h2-opt"});
                    if (h2.IsMap()) {
                        auto hosts = h2["host"];
                        for (auto host: hosts) {
                            bean->stream->host = Node2QString(host);
                            break;
                        }
                        bean->stream->path = Node2QString(h2["path"]);
                    }

                    auto tcp_http = NodeChild(proxy, {"http-opts", "http-opt"});
                    if (tcp_http.IsMap()) {
                        bean->stream->network = "tcp";
                        bean->stream->header_type = "http";
                        auto headers = tcp_http["headers"];
                        for (auto header: headers) {
                            if (Node2QString(header.first).toLower() == "host") {
                                bean->stream->host = Node2QString(header.second[0]);
                            }
                            break;
                        }
                        auto paths = tcp_http["path"];
                        for (auto path: paths) {
                            bean->stream->path = Node2QString(path);
                            break;
                        }
                    }
                } else if (type == "hysteria2") {
                    auto bean = ent->QUICBean();

                    bean->hopPort = Node2QString(proxy["ports"]);

                    bean->allowInsecure = Node2Bool(proxy["skip-cert-verify"]);
                    bean->caText = Node2QString(proxy["ca-str"]);
                    bean->sni = Node2QString(proxy["sni"]);

                    bean->obfsPassword = Node2QString(proxy["obfs-password"]);
                    bean->password = Node2QString(proxy["password"]);

                    bean->uploadMbps = Node2QString(proxy["up"]).split(" ")[0].toInt();
                    bean->downloadMbps = Node2QString(proxy["down"]).split(" ")[0].toInt();
                } else if (type == "tuic") {
                    auto bean = ent->QUICBean();

                    bean->uuid = Node2QString(proxy["uuid"]);
                    bean->password = Node2QString(proxy["password"]);

                    if (Node2Int(proxy["heartbeat-interval"]) != 0) {
                        bean->heartbeat = Int2String(Node2Int(proxy["heartbeat-interval"])) + "ms";
                    }

                    bean->udpRelayMode = Node2QString(proxy["udp-relay-mode"], bean->udpRelayMode);
                    bean->congestionControl = Node2QString(proxy["congestion-controller"], bean->congestionControl);

                    bean->disableSni = Node2Bool(proxy["disable-sni"]);
                    bean->zeroRttHandshake = Node2Bool(proxy["reduce-rtt"]);
                    bean->allowInsecure = Node2Bool(proxy["skip-cert-verify"]);
                    bean->alpn = Node2QStringList(proxy["alpn"]).join(",");
                    bean->caText = Node2QString(proxy["ca-str"]);
                    bean->sni = Node2QString(proxy["sni"]);

                    if (Node2Bool(proxy["udp-over-stream"])) bean->uos = true;

                    if (!Node2QString(proxy["ip"]).isEmpty()) {
                        if (bean->sni.isEmpty()) bean->sni = bean->serverAddress;
                        bean->serverAddress = Node2QString(proxy["ip"]);
                    }
                } else {
                    continue;
                }

                if (needFix) RawUpdater_FixEnt(ent);
                AddParsed(ent);
            }
        } catch (const YAML::Exception &ex) {
            runOnUiThread([=] {
                MessageBoxWarning("YAML Exception", ex.what());
            });
        }
#endif
    }

    static bool isHttpImport(const QString &line) {
        return line.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
    }

    void GroupUpdater::refreshSubscription(const QString &url, int groupId,
                                           const std::function<void()> &finish, bool reportResult) {
        const auto source = NormalizeImport(url);
        const auto group = NekoGui::profileManager->GetGroup(groupId);
        if (reportResult) emit serverOperationStatusChanged(QObject::tr("Обновляю подписку…"), false);
        runOnNewThread([=] {
            const auto result = updateGroup(source, group, true, false, true);
            emit asyncUpdateCallback(groupId);
            if (reportResult) {
                if (result.groupId < 0) emit serverOperationStatusChanged(result.problem, true);
                else emit serverOperationStatusChanged(QObject::tr("Подписка обновлена. Новых серверов: %1.")
                                                           .arg(result.added), false);
            }
            if (finish) finish();
        });
    }

    void GroupUpdater::RefreshSubscription(const QString &url, int groupId, const std::function<void()> &finish) {
        refreshSubscription(url, groupId, finish, true);
    }

    void GroupUpdater::AsyncUpdate(const QString &str, int _sub_gid, const std::function<void()> &finish) {
        if (_sub_gid >= 0) {
            refreshSubscription(str, _sub_gid, finish, false);
            return;
        }
        const auto content = NormalizeImport(str);
        if (content.isEmpty()) {
            emit serverOperationStatusChanged(QObject::tr("Вставьте ссылку подписки или сервера."), true);
            if (finish) finish();
            return;
        }

        // Only split explicit link lists. YAML and encoded subscriptions must
        // reach the original profile parser intact.
        const auto lines = content.split('\n', Qt::SkipEmptyParts);
        static const QRegularExpression linkScheme(QStringLiteral("^[a-z][a-z0-9+.-]*://"),
                                                    QRegularExpression::CaseInsensitiveOption);
        QStringList urls, profileLines;
        bool invalidLine = false;
        for (const auto &line : lines) {
            const auto text = line.trimmed();
            if (text.isEmpty()) continue;
            if (!linkScheme.match(text).hasMatch()) invalidLine = true;
            if (isHttpImport(text)) urls.append(text);
            else profileLines.append(text);
        }
        bool asURL = false;
        bool createNewGroup = false;
        if (invalidLine && !urls.isEmpty() && linkScheme.match(content).hasMatch()) {
            emit serverOperationStatusChanged(QObject::tr("Проверьте список ссылок. Укажите по одной полной ссылке на строку."), true);
            if (finish) finish();
            return;
        }
        if (invalidLine) urls.clear();
        urls.removeDuplicates();
        if (!urls.isEmpty()) {
            const QStringList items{
                QObject::tr("Импортировать как подписки"),
                QObject::tr("Импортировать в новые группы"),
                QObject::tr("Добавить как HTTP-прокси"),
            };
            bool ok = false;
            const auto choice = QInputDialog::getItem(QApplication::activeWindow(),
                QObject::tr("Добавление ссылок"),
                QObject::tr("Как добавить HTTP(S)-ссылки (%1)?").arg(urls.size()),
                items, 0, false, &ok);
            if (!ok) return;
            asURL = choice != items.at(2);
            createNewGroup = choice == items.at(1);
        }

        const auto group = NekoGui::profileManager->CurrentGroup();
        emit serverOperationStatusChanged(QObject::tr("Импортирую ссылки…"), false);
        runOnNewThread([=] {
            int added = 0, importedSubscriptions = 0, lastGroup = -1;
            QStringList problems;
            const auto collect = [&](const ImportResult &result, const QString &problemPrefix = QString()) {
                added += result.added;
                if (!result.problem.isEmpty()) problems.append(problemPrefix + result.problem);
                if (result.groupId >= 0) emit asyncUpdateCallback(result.groupId);
            };
            if (asURL) {
                for (int i = 0; i < urls.size(); ++i) {
                    const auto result = updateGroup(urls.at(i), group, true, createNewGroup, false);
                    collect(result, urls.size() > 1 ? QObject::tr("Подписка %1. ").arg(i + 1) : QString());
                    if (result.groupId >= 0) {
                        ++importedSubscriptions;
                        lastGroup = result.groupId;
                    }
                }
            }
            const auto profiles = asURL ? profileLines.join('\n') : content;
            if (!profiles.isEmpty()) collect(updateGroup(profiles, group, false, false, false));
            QStringList summary;
            if (importedSubscriptions > 0)
                summary.append(QObject::tr("Подписок импортировано: %1.").arg(importedSubscriptions));
            if (added > 0) summary.append(QObject::tr("Новых серверов: %1.").arg(added));
            constexpr int visibleProblems = 3;
            summary.append(problems.mid(0, visibleProblems));
            if (problems.size() > visibleProblems)
                summary.append(QObject::tr("Других ошибок: %1. Подробности в журнале.").arg(problems.size() - visibleProblems));
            if (asURL) emit subscriptionImportFinished(lastGroup);
            emit serverOperationStatusChanged(summary.join(' '), !problems.isEmpty());
            if (finish) finish();
        });
    }

    void GroupUpdater::Update(const QString &source, int gid, bool asURL) {
        auto *manager = NekoGui::profileManager;
        updateGroup(NormalizeImport(source), gid < 0 ? manager->CurrentGroup() : manager->GetGroup(gid),
                    asURL || gid >= 0, false, gid >= 0);
    }

    static QString subscriptionProblem(const NekoHTTPResponse &response) {
        switch (response.status) {
        case 401:
        case 403:
            return QObject::tr("Доступ к подписке отклонён (HTTP %1). Проверьте ссылку и срок действия.").arg(response.status);
        case 404:
        case 410:
            return QObject::tr("Подписка не найдена (HTTP %1). Проверьте ссылку.").arg(response.status);
        default:
            if (response.status >= 400)
                return QObject::tr("Сервер подписки вернул HTTP %1.").arg(response.status);
        }
        switch (response.networkError) {
        case QNetworkReply::HostNotFoundError:
            return QObject::tr("Не удалось найти адрес сервера подписки (DNS).");
        case QNetworkReply::SslHandshakeFailedError:
            return QObject::tr("Не удалось установить защищённое соединение с сервером подписки (TLS).");
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError:
            return QObject::tr("Сервер подписки не ответил вовремя.");
        case QNetworkReply::NoError:
            if (NekoGui::dataStore->sub_use_proxy && NekoGui::dataStore->started_id < 0)
                return QObject::tr("Загрузка подписок настроена через прокси. Подключите профиль или измените способ загрузки подписок в настройках.");
            break;
        default:
            break;
        }
        return QObject::tr("Не удалось связаться с сервером подписки.");
    }

    GroupUpdater::ImportResult GroupUpdater::updateGroup(const QString &source,
                                  std::shared_ptr<NekoGui::Group> group,
                                  bool subscription, bool newGroup, bool refresh) {
        using Entity = NekoGui::ProxyEntity;
        auto *manager = NekoGui::profileManager;
        QString originalUrl;
        const auto missingGroup = QObject::tr("Группа недоступна. Выберите другую группу и повторите импорт.");
        const auto changedSubscription = QObject::tr("Подписка изменилась во время загрузки. Повторите импорт.");
        {
            QMutexLocker locker(&manager->mutex);
            if (!newGroup && (!group || group->archive || manager->GetGroup(group->id) != group))
                return {-1, 0, missingGroup};
            if (group) {
                originalUrl = group->url;
            }
        }
        if (refresh && !originalUrl.isEmpty() && originalUrl != source) return {-1, 0, changedSubscription};
        static std::atomic<quint64> nextRequest{0};
        const quint64 request = subscription ? ++nextRequest : 0;
        const auto reportFailure = [&] {
            QMutexLocker locker(&manager->mutex);
            if (subscription && !newGroup && group && manager->GetGroup(group->id) == group
                && group->url == source && group->last_subscription_request <= request) {
                group->last_update_outcome = 2;
            }
        };

        QString content = source;
        QString userInfo;
        if (subscription) {
            const QUrl url(source, QUrl::StrictMode);
            if (!url.isValid() || url.host().isEmpty())
                return {-1, 0, QObject::tr("Некорректный адрес подписки. Проверьте ссылку.")};
            MW_show_log(QObject::tr("Запрашиваю подписку %1").arg(url.host()));
            const auto response = NetworkRequestHelper::HttpGet(url);
            if (!response.error.isEmpty()) {
                const auto error = QObject::tr("%1 — %2").arg(url.host(), subscriptionProblem(response));
                MW_show_log(error);
                reportFailure();
                return {-1, 0, error};
            }
            content = response.data;
            userInfo = NetworkRequestHelper::GetHeader(response.header, "Subscription-UserInfo");
        }

        RawUpdater parser;
        parser.collect_only = true;
        parser.update(content);
        if (parser.updated_order.isEmpty()) {
            reportFailure();
            const auto error = QObject::tr("Не найдено поддерживаемых серверов. Проверьте ссылку или формат ответа.");
            MW_show_log(error);
            return {-1, 0, error};
        }

        // Download and parsing run outside the store lock. A complete update
        // sees one current set of profiles and one subscription owner.
        QMutexLocker locker(&manager->mutex);
        if (!newGroup && (manager->GetGroup(group->id) != group || group->archive))
            return {-1, 0, missingGroup};
        if (!subscription) {
            int added = 0;
            for (const auto &ent : parser.updated_order)
                if (manager->AddProfile(ent, group->id)) ++added;
            const auto error = added == parser.updated_order.size() ? QString()
                : QObject::tr("Не удалось сохранить все серверы. Проверьте доступ к папке данных.");
            if (!error.isEmpty()) MW_show_log(error);
            MW_dialog_message("SubUpdater", QStringLiteral("finish:%1").arg(added));
            return {added > 0 ? group->id : -1, added, error};
        }

        if (refresh && group->url != originalUrl && group->url != source)
            return {-1, 0, changedSubscription};
        if (!newGroup && !refresh && group->url != source) {
            // Reuse the same subscription even when importing from an empty group.
            for (const auto &[id, candidate] : manager->groups) {
                if (!candidate->archive && candidate->url == source) {
                    group = candidate;
                    break;
                }
            }
            newGroup = !group->url.isEmpty() && group->url != source;
        }
        if (newGroup) {
            group = NekoGui::ProfileManager::NewGroup();
            group->name = QUrl(source).host();
            if (!manager->AddGroup(group)) {
                const auto error = QObject::tr("Не удалось сохранить группу подписки.");
                MW_show_log(error);
                return {-1, 0, error};
            }
        }
        if (group->last_subscription_request > request) return {-1, 0, changedSubscription};

        const auto previous = group->ProfilesWithOrder();
        QList<std::shared_ptr<Entity>> eligible;
        const bool sameSubscription = group->url == source;
        if (sameSubscription) {
            // Older clients did not record where a profile came from. Adopt
            // exact matches and keep profiles whose ownership is ambiguous.
            for (const auto &ent : previous)
                if (ent->origin == Entity::Subscription
                    && (!NekoGui::dataStore->sub_clear || ent->id == NekoGui::dataStore->started_id))
                    eligible.append(ent);
            for (const auto &ent : previous)
                if (ent->origin == Entity::Unclassified) eligible.append(ent);
        }

        const auto &incoming = parser.updated_order;
        QList<std::shared_ptr<Entity>> retained, matches;
        NekoGui::ProfileFilter::Common(eligible, incoming, retained, matches, false);
        QList<int> added;
        QList<int> order;
        const auto discardAdded = [&] {
            for (const int id : added) manager->DeleteProfile(id);
            if (newGroup) manager->DeleteGroup(group->id);
        };
        for (const auto &ent : incoming) {
            const auto match = matches.indexOf(ent);
            if (match >= 0) {
                order.append(retained.at(match)->id);
                continue;
            }
            ent->origin = Entity::Subscription;
            if (!manager->AddProfile(ent, group->id)) {
                discardAdded();
                const auto error = QObject::tr("Не удалось сохранить серверы подписки. Повторите импорт.");
                MW_show_log(error);
                return {-1, 0, error};
            }
            added.append(ent->id);
            order.append(ent->id);
        }

        const QSet<int> incomingIds(order.begin(), order.end());
        QList<int> obsolete;
        for (const auto &ent : previous) {
            if (incomingIds.contains(ent->id)) continue;
            if (sameSubscription && ent->origin == Entity::Subscription && ent->id != NekoGui::dataStore->started_id)
                obsolete.append(ent->id);
            else
                order.append(ent->id);
        }

        const auto previousUrl = group->url;
        const auto previousInfo = group->info;
        const auto previousStamp = group->sub_last_update;
        const auto previousOrder = group->order;
        group->url = source;
        group->info = userInfo;
        group->sub_last_update = QDateTime::currentSecsSinceEpoch();
        group->order = order;
        if (group->ToJsonBytes() != group->last_save_content && !group->Save()) {
            group->url = previousUrl;
            group->info = previousInfo;
            group->sub_last_update = previousStamp;
            group->order = previousOrder;
            discardAdded();
            const auto error = QObject::tr("Не удалось сохранить настройки подписки. Повторите импорт.");
            MW_show_log(error);
            return {-1, 0, error};
        }

        for (const auto &ent : retained) {
            if (ent->origin != Entity::Unclassified) continue;
            ent->origin = Entity::Subscription;
            if (!ent->Save()) ent->origin = Entity::Unclassified;
        }
        int removed = 0;
        for (const int id : obsolete) {
            manager->DeleteProfile(id);
            if (!manager->GetProfile(id)) ++removed;
        }
        group->last_subscription_request = request;
        group->last_update_outcome = 1;
        MW_show_log(QObject::tr("Подписка %1 обновлена (+%2 / −%3 профилей).")
                    .arg(group->name).arg(added.size()).arg(removed));
        MW_dialog_message("SubUpdater", "finish-dingyue");
        return {group->id, int(added.size()), {}};
    }

} // namespace NekoGui_sub

bool UI_update_all_groups_Updating = false;

#define should_skip_group(g) (g == nullptr || g->url.isEmpty() || g->archive || (onlyAllowed && g->skip_auto_update))

void serialUpdateSubscription(const QList<int> &groupsTabOrder, int _order, bool onlyAllowed) {
    if (_order >= groupsTabOrder.size()) {
        UI_update_all_groups_Updating = false;
        return;
    }

    // calculate this group
    auto group = NekoGui::profileManager->GetGroup(groupsTabOrder[_order]);
    if (group == nullptr || should_skip_group(group)) {
        serialUpdateSubscription(groupsTabOrder, _order + 1, onlyAllowed);
        return;
    }

    int nextOrder = _order + 1;
    while (nextOrder < groupsTabOrder.size()) {
        auto nextGid = groupsTabOrder[nextOrder];
        auto nextGroup = NekoGui::profileManager->GetGroup(nextGid);
        if (!should_skip_group(nextGroup)) {
            break;
        }
        nextOrder += 1;
    }

    // Async update current group
    UI_update_all_groups_Updating = true;
    NekoGui_sub::groupUpdater->AsyncUpdate(group->url, group->id, [=] {
        serialUpdateSubscription(groupsTabOrder, nextOrder, onlyAllowed);
    });
}

void UI_update_all_groups(bool onlyAllowed) {
    if (UI_update_all_groups_Updating) {
        MW_show_log("The last subscription update has not exited.");
        return;
    }

    auto groupsTabOrder = NekoGui::profileManager->groupsTabOrder;
    serialUpdateSubscription(groupsTabOrder, 0, onlyAllowed);
}
