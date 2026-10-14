#pragma once

#include <QJsonObject>
#include <QStringList>

namespace GreenRhythm::LocalDns {
bool parseDomains(const QString &text, QStringList *domains, QString *error = nullptr);
bool parseSettings(const QString &domainText, const QString &serverText, QStringList *domains,
                   QString *server, QString *error = nullptr);
// Append the resolver without changing the existing default server. Its scoped
// rule precedes outbound/geosite rules so work names reach the VPN's DNS.
void addToDns(QJsonObject &dns, const QStringList &domains, const QString &server);
QJsonObject resolveRule(const QStringList &domains, const QString &strategy = {},
                        const QStringList &fixedHosts = {});
bool parseHosts(const QString &text, QJsonObject *hosts, QString *error = nullptr);
void addHostsToDns(QJsonObject &dns, const QJsonObject &hosts);
QJsonObject hostsResolveRule(const QJsonObject &hosts, const QString &strategy = {});
}
