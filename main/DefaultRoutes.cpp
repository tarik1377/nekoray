#include "DefaultRoutes.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>

namespace GreenRhythm::DefaultRoutes {
namespace {
bool readObject(const QString &path, QJsonObject *object, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid JSON object: ") + parseError.errorString();
        return false;
    }
    *object = doc.object();
    return true;
}

bool writeObject(const QString &path, const QJsonObject &object, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const auto bytes = QJsonDocument(object).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
} // namespace

QString removeAdsSelector(const QString &blockDomains) {
    auto lines = blockDomains.split('\n');
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (lines[i].trimmed() == QStringLiteral("geosite:category-ads-all"))
            lines.removeAt(i);
    }
    return lines.join('\n');
}

QJsonObject migrate(const QJsonObject &scheme) {
    auto updated = scheme;
    if (scheme.value("block_domain").isString()) {
        const auto before = scheme.value("block_domain").toString();
        const auto after = removeAdsSelector(before);
        if (before != after) updated.insert("block_domain", after);
    }
    return updated;
}

FileResult migrateFile(const QString &path, QString *loadedBlockDomains, QString *error) {
    QJsonObject before;
    if (!readObject(path, &before, error)) return FileResult::Failed;
    const auto after = migrate(before);
    if (before != after && !writeObject(path, after, error)) return FileResult::Failed;
    // Also synchronize unchanged disk state: an active preset may have inherited
    // its block list when the saved file omitted the property.
    if (loadedBlockDomains && before.value("block_domain").isString())
        *loadedBlockDomains = after.value("block_domain").toString();
    return before == after ? FileResult::Unchanged : FileResult::Updated;
}

bool markComplete(const QString &settingsPath, QString *error) {
    QJsonObject settings;
    if (!readObject(settingsPath, &settings, error)) return false;
    settings.insert("routing_ads_removed", true);
    return writeObject(settingsPath, settings, error);
}

QJsonObject repairDns(const QJsonObject &scheme) {
    const auto useObject = scheme.value("use_dns_object");
    const auto dnsRouting = scheme.value("dns_routing");
    const auto final = scheme.value("dns_final_out").toString();
    if ((!useObject.isUndefined() && (!useObject.isBool() || useObject.toBool()))
        || !dnsRouting.isBool() || !dnsRouting.toBool()
        || scheme.value("def_outbound").toString() != QStringLiteral("proxy")
        || scheme.value("direct_dns").toString() != QStringLiteral("77.88.8.8")
        || scheme.value("direct_dns_strategy").toString() != QStringLiteral("ipv4_only")
        || scheme.value("remote_dns").toString() != QStringLiteral("https://1.1.1.1/dns-query")
        || scheme.value("remote_dns_strategy").toString() != QStringLiteral("ipv4_only")
        || (final != QStringLiteral("bypass") && final != QStringLiteral("proxy"))) return scheme;

    QStringList directDomains, directIPs;
    for (const auto &line : scheme.value("direct_domain").toString().split('\n'))
        directDomains.append(line.trimmed());
    for (const auto &line : scheme.value("direct_ip").toString().split('\n'))
        directIPs.append(line.trimmed());
    if (!directDomains.contains(QStringLiteral("domain:ru"))
        || !directDomains.contains(QStringLiteral("domain:su"))
        || !directIPs.contains(QStringLiteral("geoip:ru"))) return scheme;

    auto repaired = scheme;
    repaired.insert("direct_dns", QStringLiteral("https://1.1.1.1/dns-query"));
    repaired.insert("dns_final_out", QStringLiteral("proxy"));
    return repaired;
}

FileResult repairDnsFile(const QString &path, QString *loadedDirectDns,
                         QString *loadedFinalOutbound, QString *error) {
    QJsonObject before;
    if (!readObject(path, &before, error)) return FileResult::Failed;
    // Never replace an active scheme with stale disk values. Memory is updated
    // only after the atomic write succeeds, so a failed commit can be retried.
    if ((loadedDirectDns && before.value("direct_dns").isString()
         && *loadedDirectDns != before.value("direct_dns").toString())
        || (loadedFinalOutbound && before.value("dns_final_out").isString()
            && *loadedFinalOutbound != before.value("dns_final_out").toString())) {
        if (error) *error = QStringLiteral("Loaded DNS settings differ from their file");
        return FileResult::Failed;
    }
    const auto after = repairDns(before);
    if (before != after && !writeObject(path, after, error)) return FileResult::Failed;
    if (loadedDirectDns && before.value("direct_dns").isString())
        *loadedDirectDns = after.value("direct_dns").toString();
    if (loadedFinalOutbound && before.value("dns_final_out").isString())
        *loadedFinalOutbound = after.value("dns_final_out").toString();
    return before == after ? FileResult::Unchanged : FileResult::Updated;
}

bool markDnsComplete(const QString &settingsPath, QString *error) {
    QJsonObject settings;
    if (!readObject(settingsPath, &settings, error)) return false;
    settings.insert("routing_dns_repaired", true);
    return writeObject(settingsPath, settings, error);
}
} // namespace GreenRhythm::DefaultRoutes
