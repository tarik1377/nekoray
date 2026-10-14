#include "main/RuRoutingPolicy.hpp"

#include <QCoreApplication>
#include <cstdio>

namespace Policy = GreenRhythm::RuRoutingPolicy;
namespace {
int checks = 0, failed = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) ++failed;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto standard = Policy::standardProcessProxyRule();
    const QJsonObject explicitProxy{{"process_name", QJsonArray{"Telegram.exe"}}, {"outbound", "proxy"}};
    const QJsonObject explicitBlock{{"ip_cidr", QJsonArray{"192.0.2.0/24"}}, {"outbound", "block"}};
    const QJsonObject game{{"process_path_regex", QJsonArray{"-Shipping\\.exe$"}}, {"outbound", "bypass"}};
    const QJsonArray custom{game, standard, explicitProxy, explicitBlock};
    const auto split = Policy::splitStandardFallback(custom, "geoip:ru\ngeoip:private", "proxy");
    check(split.primary == QJsonArray{game, explicitProxy, explicitBlock}, "only the exact shipped fallback leaves the custom chain");
    check(split.fallback == QJsonArray{standard}, "factory process group becomes a fallback after address rules");
    check(custom == QJsonArray{game, standard, explicitProxy, explicitBlock}, "stored route JSON remains untouched");
    for (const auto &choice : {QStringLiteral("keep"), QStringLiteral("restored")}) {
        const auto kept = Policy::splitStandardFallback(custom, "geoip:ru", "proxy", choice);
        check(kept.primary == custom && kept.fallback.isEmpty(), "keep and restored choices preserve the entire custom order");
    }
    for (const auto &ips : {QStringLiteral("geoip:private"), QStringLiteral("# geoip:ru"), QStringLiteral("geoip:russia")}) {
        const auto other = Policy::splitStandardFallback(custom, ips, "proxy");
        check(other.primary == custom && other.fallback.isEmpty(), "unrelated schemes never acquire a RU precedence policy");
    }
    const auto directFinal = Policy::splitStandardFallback(custom, "geoip:ru", "bypass");
    check(directFinal.primary == custom && directFinal.fallback.isEmpty(), "user final outbound is preserved");
    const auto whitespace = Policy::splitStandardFallback(custom, "# keep\n  geoip:ru  \n", "proxy", "apply");
    check(whitespace.fallback == QJsonArray{standard}, "whitespace and comments do not disable an active RU selector");
    auto edited = standard;
    edited.insert("network", "tcp");
    const auto qualified = Policy::splitStandardFallback(QJsonArray{edited}, "geoip:ru", "proxy");
    check(qualified.primary == QJsonArray{edited} && qualified.fallback.isEmpty(), "qualified custom process rules retain precedence");
    edited = standard;
    edited.insert("future-option", QJsonObject{{"opaque", true}});
    const auto future = Policy::splitStandardFallback(QJsonArray{edited}, "geoip:ru", "proxy");
    check(future.primary == QJsonArray{edited} && future.fallback.isEmpty(), "unknown rule fields are preserved with their position");
    edited = standard;
    auto names = edited.value("process_name").toArray();
    names.append("MyApp.exe");
    edited.insert("process_name", names);
    const auto named = Policy::splitStandardFallback(QJsonArray{edited}, "geoip:ru", "proxy");
    check(named.primary == QJsonArray{edited} && named.fallback.isEmpty(), "an edited application group remains an explicit custom rule");
    const QJsonArray duplicates{standard, explicitBlock, standard};
    const auto duplicate = Policy::splitStandardFallback(duplicates, "geoip:ru", "proxy");
    check(duplicate.primary == duplicates && duplicate.fallback.isEmpty(), "duplicate factory-looking groups are not guessed away");
    const QJsonArray opaque{42, QJsonValue::Null, explicitProxy};
    const auto invalid = Policy::splitStandardFallback(opaque, "geoip:ru", "proxy");
    check(invalid.primary == opaque && invalid.fallback.isEmpty(), "unknown custom values are not discarded or interpreted");
    std::printf("%d checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
