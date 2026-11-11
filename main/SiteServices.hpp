#pragma once
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>

namespace GreenRhythm::SiteServices {
    inline bool url(const QString &input, QString *navigation, QString *origin, QString *error) {
        auto text = input.trimmed();
        if (!text.contains("://")) text.prepend("https://");
        QUrl value(text, QUrl::StrictMode);
        QHostAddress literal;
        auto host = literal.setAddress(value.host()) ? value.host().toLower() : QString::fromLatin1(QUrl::toAce(value.host())).toLower();
        if (host.endsWith('.')) host.chop(1);
        const bool scheme = value.scheme() == "https" || value.scheme() == "http";
        QHostAddress address;
        bool publicHost = !host.isEmpty();
        if (address.setAddress(host)) {
            publicHost = !address.isLoopback() && !address.isNull() && !address.isMulticast() && !address.isLinkLocal() && !address.isInSubnet(QHostAddress("10.0.0.0"), 8) && !address.isInSubnet(QHostAddress("172.16.0.0"), 12) && !address.isInSubnet(QHostAddress("192.168.0.0"), 16) && !address.isInSubnet(QHostAddress("100.64.0.0"), 10) && !address.isInSubnet(QHostAddress("fc00::"), 7);
        } else {
            static const QRegularExpression domain("^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$");
            publicHost = host.size() <= 253 && domain.match(host).hasMatch();
            for (const auto &suffix: {".local", ".lan", ".localhost", ".internal", ".home.arpa"})
                if (host.endsWith(suffix)) publicHost = false;
        }
        if (input.size() > 4096 || !value.isValid() || !scheme || !publicHost || !value.userInfo().isEmpty() || value.port() == 0) {
            if (error) *error = QStringLiteral("Введите публичный HTTP/HTTPS URL без логина и пароля. Внутренние ресурсы остаются в настройках рабочих VPN.");
            return false;
        }
        value.setHost(host);
        if ((value.scheme() == "https" && value.port() == 443) || (value.scheme() == "http" && value.port() == 80)) value.setPort(-1);
        if (value.path().isEmpty()) value.setPath("/");
        if (navigation) *navigation = value.toString(QUrl::FullyEncoded);
        value.setQuery(QString());
        value.setFragment(QString());
        value.setPath(QString());
        if (origin) *origin = value.toString(QUrl::FullyEncoded);
        return true;
    }
    inline QJsonObject empty() { return {{"schema", 1}, {"sites", QJsonArray{}}}; }
    inline bool parse(const QString &raw, QJsonObject *library, QString *error) {
        if (raw.isEmpty()) {
            *library = empty();
            return true;
        }
        const auto document = QJsonDocument::fromJson(raw.toUtf8());
        const auto root = document.object();
        if (raw.size() > 2 * 1024 * 1024 || !document.isObject() || root.value("schema") != 1 || !root.value("sites").isArray() || root.value("sites").toArray().size() > 32) {
            if (error) *error = QStringLiteral("Неизвестный формат сохранённых сайтов. Существующие данные не изменены.");
            return false;
        }
        QSet<QString> ids;
        static const QRegularExpression identity("^[a-zA-Z0-9_-]{1,64}$");
        for (const auto &value: root.value("sites").toArray()) {
            const auto site = value.toObject();
            const auto id = site.value("id").toString(), mode = site.value("mode").toString();
            QString origin;
            if (!identity.match(id).hasMatch() || ids.contains(id) || !site.value("enabled").isBool() || !site.value("endpoints").isArray() || site.value("endpoints").toArray().isEmpty() || site.value("endpoints").toArray().size() > 128 || site.value("title").toString().size() > 120 || (mode != "auto" && mode != "direct" && mode != "proxy") || !url(site.value("url").toString(), nullptr, &origin, error) || origin != site.value("url").toString()) {
                if (error) *error = QStringLiteral("Повреждена карточка сайта. Изменения не записаны.");
                return false;
            }
            ids.insert(id);
            QSet<QString> endpoints;
            for (const auto &endpoint: site.value("endpoints").toArray()) {
                QString normalized;
                if (!endpoint.isString() || !url(endpoint.toString(), nullptr, &normalized, error) || normalized != endpoint.toString() || endpoints.contains(normalized)) {
                    if (error) *error = QStringLiteral("Повреждён список зависимостей сайта.");
                    return false;
                }
                endpoints.insert(normalized);
            }
            if (!endpoints.contains(origin)) {
                if (error) *error = QStringLiteral("В карточке отсутствует основной адрес сайта.");
                return false;
            }
        }
        *library = root;
        return true;
    }
    inline QString encode(const QJsonObject &library) { return QString::fromUtf8(QJsonDocument(library).toJson(QJsonDocument::Compact)); }
    inline QJsonObject policy(const QJsonObject &library) {
        QJsonArray sites;
        for (const auto &value: library.value("sites").toArray()) {
            QJsonObject item;
            for (const auto &field: {"id", "title", "url", "mode", "enabled", "endpoints"}) item[field] = value.toObject().value(field);
            sites.append(item);
        }
        return {{"schema", 1}, {"sites", sites}};
    }
    inline void history(QJsonObject &site, const QString &kind, const QString &summary) {
        auto events = site.value("history").toArray();
        events.append(QJsonObject{{"at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                                  {"kind", kind},
                                  {"mode", site.value("mode")},
                                  {"summary", summary}});
        while (events.size() > 30) events.removeFirst();
        site["history"] = events;
    }
    inline bool changeMode(QJsonObject *library, const QString &id, const QString &mode, QString *error) {
        if (mode != "auto" && mode != "direct" && mode != "proxy") {
            if (error) *error = QStringLiteral("Неизвестный режим сайта.");
            return false;
        }
        auto sites = library->value("sites").toArray();
        for (int i = 0; i < sites.size(); ++i) {
            auto site = sites.at(i).toObject();
            if (site.value("id") != id) continue;
            if (site.value("mode") == mode && site.value("enabled").toBool()) return true;
            site["mode"] = mode;
            site["enabled"] = true;
            history(site, "mode", QStringLiteral("Выбран режим: %1").arg(mode == "auto" ? QStringLiteral("Авто") : mode == "direct" ? QStringLiteral("Напрямую")
                                                                                                                                    : QStringLiteral("Через VPN")));
            sites[i] = site;
            (*library)["sites"] = sites;
            return true;
        }
        if (error) *error = QStringLiteral("Карточка уже удалена.");
        return false;
    }
    inline bool recordScan(QJsonObject *library, const QJsonObject &report, QString *savedID, QString *error) {
        QString origin;
        if (!url(report.value("url").toString(), nullptr, &origin, error)) return false;
        auto sites = library->value("sites").toArray();
        int index = -1;
        for (int i = 0; i < sites.size(); ++i)
            if (sites.at(i).toObject().value("url") == origin) {
                index = i;
                break;
            }
        if (index < 0 && sites.size() >= 32) {
            if (error) *error = QStringLiteral("Сохранено 32 сайта. Удалите ненужную карточку.");
            return false;
        }
        QJsonObject site = index < 0 ? QJsonObject{{"id", QUuid::createUuid().toString(QUuid::Id128)}, {"url", origin}, {"mode", "auto"}, {"enabled", report.value("direct_ok").toBool() || report.value("proxy_ok").toBool()}} : sites.at(index).toObject();
        auto title = report.value("title").toString().simplified().left(120);
        if (title.isEmpty()) title = QUrl(origin).host();
        site["title"] = title;
        QSet<QString> names{origin};
        for (const auto &value: report.value("endpoints").toArray()) {
            QString endpoint;
            if (!url(value.toObject().value("origin").toString(), nullptr, &endpoint, error)) return false;
            names.insert(endpoint);
        }
        if (names.size() > 128) {
            if (error) *error = QStringLiteral("Слишком много зависимостей для одной карточки.");
            return false;
        }
        // Keep prior observations on partial/different public loads without ever
        // changing the old route scheme. Fresh observations have bounded priority.
        for (const auto &value: site.value("endpoints").toArray())
            if (names.size() < 128) names.insert(value.toString());
        auto ordered = names.values();
        ordered.sort();
        QJsonArray endpoints;
        for (const auto &name: ordered) endpoints.append(name);
        site["endpoints"] = endpoints;
        site["last_scan"] = report;
        history(site, "scan", QStringLiteral("Анализ: %1 адресов · напрямую %2 · VPN %3%4").arg(endpoints.size()).arg(report.value("direct_ok").toBool() ? QStringLiteral("доступен") : QStringLiteral("не подтверждён"), report.value("proxy_ok").toBool() ? QStringLiteral("доступен") : QStringLiteral("не подтверждён"), report.value("partial").toBool() ? QStringLiteral(" · неполная загрузка") : QString()));
        *savedID = site.value("id").toString();
        if (index < 0)
            sites.append(site);
        else
            sites[index] = site;
        (*library)["sites"] = sites;
        return true;
    }
    struct Effective {
        QString origin, mode;
    };
    inline QList<Effective> effective(const QJsonObject &library) {
        QMap<QString, Effective> selected;
        auto priority = [](const QString &mode) { return mode == "proxy" ? 3 : mode == "direct" ? 2
                                                                                                : 1; };
        for (const auto &value: library.value("sites").toArray()) {
            const auto site = value.toObject();
            if (!site.value("enabled").toBool()) continue;
            for (const auto &endpoint: site.value("endpoints").toArray()) {
                const auto origin = endpoint.toString();
                const QUrl parsed(origin);
                const auto key = parsed.host() + ':' + QString::number(parsed.port(parsed.scheme() == "https" ? 443 : 80));
                const auto mode = site.value("mode").toString();
                if (!selected.contains(key) || priority(mode) > priority(selected.value(key).mode)) selected[key] = {origin, mode};
            }
        }
        return selected.values();
    }
    inline QJsonObject endpointRule(const QString &origin) {
        const QUrl value(origin);
        QHostAddress address;
        QJsonObject rule{{"port", QJsonArray{value.port(value.scheme() == "https" ? 443 : 80)}}};
        if (address.setAddress(value.host()))
            rule["ip_cidr"] = QJsonArray{address.toString() + (address.protocol() == QAbstractSocket::IPv4Protocol ? "/32" : "/128")};
        else
            rule["domain"] = QJsonArray{value.host()};
        return rule;
    }
    inline QJsonObject literalIPRule(const QJsonObject &base, bool tcp = false) {
        QJsonArray names;
        for (const auto &value: base.value("ip_cidr").toArray()) names.append(value.toString().section('/', 0, 0));
        const QJsonObject emptyName{{"domain_regex", QJsonArray{".+"}}, {"invert", true}}, namedIPs{{"domain", names}};
        const QJsonObject guard{{"type", "logical"}, {"mode", "or"}, {"rules", QJsonArray{emptyName, namedIPs}}};
        QJsonArray children{base, guard};
        if (tcp) children.append(QJsonObject{{"network", "tcp"}});
        return {{"type", "logical"}, {"mode", "and"}, {"rules", children}};
    }
    inline QJsonArray headlessRules(const QJsonObject &library) {
        QMap<QString, QJsonObject> groups;
        for (const auto &entry: effective(library)) {
            auto rule = endpointRule(entry.origin);
            const QString field = rule.contains("ip_cidr") ? "ip_cidr" : "domain", key = QString::number(rule.value("port").toArray().first().toInt()) + field;
            if (!groups.contains(key))
                groups[key] = rule;
            else {
                auto names = groups[key].value(field).toArray();
                names.append(rule.value(field).toArray().first());
                groups[key][field] = names;
            }
        }
        QJsonArray rules;
        for (const auto &rule: groups) rules.append(rule.contains("ip_cidr") ? literalIPRule(rule) : rule);
        if (rules.isEmpty()) rules.append(QJsonObject{{"domain", QJsonArray{"site-services-disabled.invalid"}}, {"port", QJsonArray{65535}}});
        return rules;
    }
    inline QJsonArray exportRules(const QJsonObject &library, const QString &proxy) {
        QJsonArray rules;
        for (const auto &entry: effective(library)) {
            auto rule = endpointRule(entry.origin);
            if (rule.contains("ip_cidr"))
                rule = literalIPRule(rule, true);
            else
                rule["network"] = "tcp";
            rule["outbound"] = entry.mode == "direct" ? QString("bypass") : proxy;
            rules.append(rule);
        }
        return rules;
    }
    inline bool save(const QString &path, const QString &expected, const QJsonObject &library, QString *error) {
        QJsonObject checked;
        const auto replacement = encode(library);
        if (!parse(replacement, &checked, error)) return false;
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Не удалось прочитать настройки.");
            return false;
        }
        const auto before = input.readAll();
        input.close();
        const auto document = QJsonDocument::fromJson(before);
        if (!document.isObject()) {
            if (error) *error = QStringLiteral("Настройки повреждены; сохранение отменено.");
            return false;
        }
        auto settings = document.object();
        if ((settings.contains("site_services") && !settings.value("site_services").isString()) || settings.value("site_services").toString() != expected) {
            if (error) *error = QStringLiteral("Сайты уже изменились. Обновите страницу и повторите.");
            return false;
        }
        if (expected == replacement) return true;
        const auto directory = QFileInfo(path).absoluteDir().filePath(".site-services-backups");
        if (!QDir().mkpath(directory)) {
            if (error) *error = QStringLiteral("Не удалось создать резервную копию.");
            return false;
        }
        QTemporaryFile backup(directory + "/before-XXXXXX.json");
        if (!backup.open() || backup.write(before) != before.size() || !backup.flush()) {
            if (error) *error = QStringLiteral("Не удалось сохранить резервную копию.");
            return false;
        }
        backup.setAutoRemove(false);
        backup.close();
        settings["site_services"] = replacement;
        QSaveFile output(path);
        output.setDirectWriteFallback(false);
        const auto bytes = QJsonDocument(settings).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            if (error) *error = QStringLiteral("Сайты не сохранены; прежние настройки оставлены.");
            return false;
        }
        return true;
    }
} // namespace GreenRhythm::SiteServices
