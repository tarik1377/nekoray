#include "LocalDns.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>

namespace GreenRhythm::LocalDns {
namespace {
const QString tag = QStringLiteral("gr-local-dns");
QJsonArray values(const QStringList &domains) {
    QJsonArray result;
    for (const auto &domain : domains) result.append(domain);
    return result;
}
}

bool parseDomains(const QString &text, QStringList *domains, QString *error) {
    QStringList result;
    const QRegularExpression label(QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    for (const auto &raw : text.split('\n')) {
        auto domain = raw.trimmed().toLower();
        if (domain.isEmpty() || domain.startsWith('#')) continue;
        if (domain.startsWith("*.")) domain.remove(0, 2);
        if (domain.startsWith('.')) domain.remove(0, 1);
        if (domain.endsWith('.')) domain.chop(1);
        domain = QString::fromLatin1(QUrl::toAce(domain));
        bool valid = !domain.isEmpty() && domain.size() <= 253 && QHostAddress(domain).isNull();
        for (const auto &part : domain.split('.')) valid = valid && label.match(part).hasMatch();
        if (!valid) {
            if (error) *error = QStringLiteral("Некорректный домен локального DNS: %1").arg(raw.trimmed());
            return false;
        }
        if (!result.contains(domain)) result.append(domain);
    }
    if (domains) *domains = result;
    return true;
}

bool parseSettings(const QString &domainText, const QString &serverText, QStringList *domains,
                   QString *server, QString *error) {
    QStringList parsedDomains;
    if (!parseDomains(domainText, &parsedDomains, error)) return false;
    QString parsedServer;
    if (!parsedDomains.isEmpty()) {
        const QHostAddress address(serverText.trimmed());
        if (address.isNull() || address.isMulticast()
            || address == QHostAddress(QHostAddress::AnyIPv4)
            || address == QHostAddress(QHostAddress::AnyIPv6)) {
            if (error) *error = QStringLiteral("Укажи IP-адрес DNS-сервера локальной сети или рабочего VPN.");
            return false;
        }
        parsedServer = address.toString();
    }
    if (domains) *domains = parsedDomains;
    if (server) *server = parsedServer;
    return true;
}

void addToDns(QJsonObject &dns, const QStringList &domains, const QString &server) {
    if (domains.isEmpty()) return;
    auto servers = dns.value("servers").toArray();
    servers.append(QJsonObject{{"tag", tag}, {"address", server}, {"detour", "direct"}});
    auto rules = dns.value("rules").toArray();
    rules.prepend(QJsonObject{{"domain_suffix", values(domains)}, {"server", tag}});
    dns.insert("servers", servers);
    dns.insert("rules", rules);
}

QJsonObject resolveRule(const QStringList &domains, const QString &strategy) {
    if (domains.isEmpty()) return {};
    QJsonObject rule{{"domain_suffix", values(domains)}, {"action", "resolve"}, {"server", tag}};
    if (!strategy.isEmpty()) rule.insert("strategy", strategy);
    return rule;
}
}
