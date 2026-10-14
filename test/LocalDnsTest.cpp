#include "main/LocalDns.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <cstdio>

static int checks = 0, failures = 0;
static void check(bool ok, const char *name) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QStringList domains;
    QString error;
    check(GreenRhythm::LocalDns::parseDomains(" # keep comment\r\n .CORP.example. \r\n*.corp.example\nfiles.corp.example", &domains, &error)
          && domains == QStringList{"corp.example", "files.corp.example"}, "domain suffixes normalized and deduplicated");
    check(GreenRhythm::LocalDns::parseDomains(QString::fromUtf8("пример.рф"), &domains, &error)
          && domains == QStringList{"xn--e1afmkfd.xn--p1ai"}, "IDN suffix supported");
    for (const auto &invalid : {"*", ".", "https://corp.example", "corp.example/path", "10.1.2.3", "a..example", "-corp.example", "corp-.example", "corp example"}) {
        domains = {"retained.example"};
        check(!GreenRhythm::LocalDns::parseDomains(invalid, &domains, &error)
              && !error.isEmpty() && domains == QStringList{"retained.example"}, "bad domain rejected without partial results");
    }
    check(GreenRhythm::LocalDns::parseDomains(" # only comment\n", &domains, &error) && domains.isEmpty(), "empty setting is valid");
    QString server;
    check(GreenRhythm::LocalDns::parseSettings("corp.example", " 10.0.0.53 ", &domains, &server, &error)
          && server == "10.0.0.53" && domains == QStringList{"corp.example"}, "explicit VPN DNS address normalized");
    check(GreenRhythm::LocalDns::parseSettings("corp.example", "fd00::53", &domains, &server, &error)
          && server == "fd00::53", "IPv6 DNS address supported");
    for (const auto &invalid : {"", "local", "https://dns.example/dns-query", "dns.example", "0.0.0.0", "::", "224.0.0.1"}) {
        domains = {"retained.example"}; server = "10.0.0.53";
        check(!GreenRhythm::LocalDns::parseSettings("corp.example", invalid, &domains, &server, &error)
              && domains == QStringList{"retained.example"} && server == "10.0.0.53", "bad DNS server rejected without partial results");
    }
    check(GreenRhythm::LocalDns::parseSettings("", "", &domains, &server, &error)
          && domains.isEmpty() && server.isEmpty(), "old settings require no new DNS server");
    const QJsonObject original{
        {"servers", QJsonArray{QJsonObject{{"tag", "dns-remote"}, {"address", "https://1.1.1.1/dns-query"}},
                               QJsonObject{{"tag", "dns-direct"}, {"address", "77.88.8.8"}}}},
        {"rules", QJsonArray{QJsonObject{{"outbound", "any"}, {"server", "dns-direct"}}}},
        {"independent_cache", true}, {"final", "dns-remote"},
        {"future_option", QJsonObject{{"retain", true}}}
    };
    auto dns = original;
    GreenRhythm::LocalDns::addToDns(dns, {}, {});
    check(dns == original, "empty setting does not alter DNS");
    GreenRhythm::LocalDns::addToDns(dns, {"corp.example"}, "10.0.0.53");
    auto servers = dns.value("servers").toArray();
    check(servers.first() == original.value("servers").toArray().first(), "implicit default server retained");
    check(servers.last().toObject().value("address") == "10.0.0.53"
          && servers.last().toObject().value("detour") == "direct", "corporate names use selected VPN DNS directly");
    auto rules = dns.value("rules").toArray();
    check(rules.first().toObject().value("domain_suffix").toArray() == QJsonArray{"corp.example"}
          && rules.last() == original.value("rules").toArray().last(), "scoped DNS rule precedes generic outbound rule");
    servers.removeLast(); rules.removeFirst();
    dns["servers"] = servers; dns["rules"] = rules;
    check(dns == original, "all existing DNS settings preserved");
    auto resolve = GreenRhythm::LocalDns::resolveRule({"corp.example"}, "prefer_ipv6");
    check(resolve.value("action") == "resolve" && resolve.value("server") == "gr-local-dns"
          && resolve.value("strategy") == "prefer_ipv6", "local resolution exposes destination IP while honoring address-family preference");
    check(!GreenRhythm::LocalDns::resolveRule({"corp.example"}).contains("strategy"), "no unwanted IPv4-only constraint");
    check(GreenRhythm::LocalDns::resolveRule({}).isEmpty(), "empty setting adds no route action");
    QJsonObject hosts;
    check(GreenRhythm::LocalDns::parseHosts("# comment\n10.0.0.10 GIT.corp.example. alias.corp.example\nfd00::10 git.corp.example\n10.0.0.10 git.corp.example", &hosts, &error)
          && hosts.value("git.corp.example").toArray() == QJsonArray{"10.0.0.10", "fd00::10"}, "internal host names and addresses normalize and deduplicate");
    for (const auto &invalid : {"0.0.0.0 git.example", "224.0.0.1 git.example", "10.0.0.10", "10.0.0.10 *.corp.example", "10.0.0.10 https://git.example"}) {
        const auto before = hosts;
        check(!GreenRhythm::LocalDns::parseHosts(invalid, &hosts, &error) && hosts == before,
              "invalid internal host setting leaves the previous map intact");
    }
    dns = original;
    GreenRhythm::LocalDns::addHostsToDns(dns, {});
    check(dns == original, "empty internal host map changes no defaults");
    GreenRhythm::LocalDns::addHostsToDns(dns, hosts);
    check(dns.value("rules").toArray().first().toObject().value("server") == "gr-local-hosts"
          && dns.value("servers").toArray().last().toObject().value("predefined").toObject() == hosts,
          "internal addresses take precedence over public DNS without changing the final server");
    check(GreenRhythm::LocalDns::hostsResolveRule(hosts).value("domain").toArray().size() == 2,
          "internal addresses resolve before destination IP routing");
    check(GreenRhythm::LocalDns::hostsResolveRule({}).isEmpty(), "empty host map adds no resolve action");
    std::printf("Local VPN DNS: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
