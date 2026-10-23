#include "main/GameVoiceRouting.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>

namespace Policy = GreenRhythm::GameVoiceRouting;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) ++failures;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
}
QString selected(const QJsonArray &rules, const QString &name) {
    for (const auto &value : rules) {
        const auto rule = value.toObject();
        for (const auto &process : Policy::values(rule.value("process_name")))
            if (process.toString().compare(name, Qt::CaseInsensitive) == 0)
                return rule.value("outbound").toString();
    }
    return {};
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QJsonObject legacyGame{{"process_name", QJsonArray{"steam.exe", "steamwebhelper.exe", "steamservice.exe", "Squad.exe"}}, {"outbound", "bypass"}};
    auto game = legacyGame;
    auto names = game.value("process_name").toArray();
    names.append("cs2.exe"); names.append("csgo.exe"); game["process_name"] = names;
    const auto voice = GreenRhythm::RuRoutingPolicy::standardProcessProxyRule();
    const QJsonObject shipping{{"process_path_regex", QJsonArray{"(?i)-Shipping\\.exe$"}}, {"outbound", "bypass"}};
    const QJsonObject quic{{"network", "udp"}, {"port", 443}, {"outbound", "block"}};
    const QJsonArray factory{game, shipping, quic, voice};
    const QJsonArray legacy{legacyGame, shipping, quic, voice};
    const auto original = QJsonDocument(legacy).toJson();
    QFile fixture(QString::fromUtf8(GR_GAME_VOICE_FIXTURE));
    check(fixture.open(QIODevice::ReadOnly), "shared core UDP/TCP routing fixture opens");
    const auto fixtureRules = QJsonDocument::fromJson(fixture.readAll()).array();
    check(fixtureRules.size() >= 2 && Policy::defaults(legacy, factory, {}) == QJsonArray{fixtureRules.at(0), fixtureRules.at(1)},
          "C++ application policy is identical to the rules exercised by real core UDP/TCP tests");
    QFile sniffFixture(QString::fromUtf8(GR_GAME_VOICE_SNIFF_FIXTURE));
    check(sniffFixture.open(QIODevice::ReadOnly), "shared UDP sniff fixture opens");
    const auto sniffRules = QJsonDocument::fromJson(sniffFixture.readAll()).array();
    check(Policy::tunSniffRules(Policy::defaults(legacy, factory, {}), true) == sniffRules,
          "actual core exercises the same DNS-only UDP and ordinary TCP sniff rules as the builder");
    const auto fullSniff = QJsonArray{QJsonObject{{"inbound", QJsonArray{"tun-in"}}, {"action", "sniff"}}};
    check(Policy::tunSniffRules(Policy::defaults(legacy, factory, {}), false) == fullSniff,
          "scoped work-DNS and kept route policies retain full sniffing");
    check(Policy::tunSniffRules(QJsonArray{QJsonObject{{"protocol", "quic"}, {"outbound", "block"}}, game}, true) == fullSniff,
          "an earlier protocol rule prevents skipping discovery");
    for (const auto &scheme : {factory, legacy}) {
        const auto rules = Policy::defaults(scheme, factory, {});
        for (const auto &name : Policy::directProcesses())
            check(selected(rules, name) == "bypass", "factory and existing presets send CS2 and Steam directly");
        for (const auto &name : Policy::voiceProcesses())
            check(selected(rules, name) == "proxy", "stable/PTB/Canary Discord use the VPN by default");
        check(selected(rules, "Telegram.exe").isEmpty() && selected(rules, "Squad.exe").isEmpty(),
              "unrelated apps and game rules keep their existing policy");
    }
    check(QJsonDocument(legacy).toJson() == original, "assembling defaults never rewrites stored route JSON");
    for (const auto &choice : {QStringLiteral("keep"), QStringLiteral("restored")})
        check(Policy::defaults(legacy, factory, {}, choice).isEmpty(), "keep/restored choices preserve the selected route policy");
    check(Policy::defaults(QJsonArray{}, factory, {}).isEmpty(), "custom schemes without factory rules are not guessed into a preset");

    const QJsonObject explicitCS{{"process_name", "CS2.EXE"}, {"outbound", "proxy"}, {"network", "udp"}};
    check(Policy::tunSniffRules(QJsonArray{explicitCS}, true) == fullSniff,
          "qualified process rules do not acquire an unqualified UDP fast path");
    const QJsonObject explicitVoice{{"process_path", "C:\\Apps\\Discord.exe"}, {"outbound", "block"}};
    auto custom = legacy; custom.append(explicitCS); custom.append(explicitVoice);
    auto rules = Policy::defaults(custom, factory, {});
    check(selected(rules, "cs2.exe").isEmpty() && selected(rules, "Discord.exe").isEmpty(),
          "qualified process rules and exact process paths retain explicit user choices");
    check(selected(rules, "steam.exe") == "bypass", "one app override does not disable another app default");
    const QJsonObject logical{{"type", "logical"}, {"mode", "and"}, {"rules", QJsonArray{explicitCS}}, {"outbound", "block"}};
    rules = Policy::defaults(legacy, factory, QJsonArray{logical});
    check(selected(rules, "cs2.exe").isEmpty(), "nested global process choices retain priority");
    const QJsonObject pattern{{"process_path_regex", "C:/custom/.*"}, {"outbound", "proxy"}};
    check(Policy::defaults(legacy, factory, QJsonArray{pattern}).isEmpty(), "unknown process path patterns are conservatively preserved");
    const QJsonObject customBlock{{"ip_cidr", "192.0.2.0/24"}, {"outbound", "block"}};
    check(Policy::defaults(legacy, factory, QJsonArray{customBlock}).isEmpty(), "automatic defaults cannot bypass a custom address block");
    auto duplicate = legacy; duplicate.append(legacyGame); duplicate.append(voice); duplicate.append(shipping);
    check(Policy::defaults(duplicate, factory, {}).isEmpty(), "duplicate factory-looking rules are not mistaken for inherited defaults");
    const auto flipped = Policy::gameTunnelRule(game);
    check(selected(QJsonArray{flipped}, "Squad.exe") == "proxy", "games switch still tunnels other factory games");
    for (const auto &name : Policy::directProcesses())
        check(selected(QJsonArray{flipped}, name).isEmpty(), "games switch cannot put CS2 or Steam back in the VPN");
    check(Policy::gameTunnelRule(QJsonObject{{"process_name", "steam.exe"}, {"outbound", "bypass"}}).isEmpty(),
          "an all-launcher rule is not inverted into an empty catch-all");
    check(Policy::gameTunnelRule(QJsonObject{{"process_name", "Discord.exe"}, {"outbound", "bypass"}}).isEmpty(),
          "games switch does not invert an explicit voice bypass");
    check(Policy::gameTunnelRule(quic).isEmpty(), "games switch preserves UDP/443 restrictions for unrelated traffic");
    for (const auto &choice : {QStringLiteral("keep"), QStringLiteral("restored")})
        check(selected(QJsonArray{Policy::gameTunnelRule(game, choice)}, "steam.exe") == "proxy", "kept/restored policies retain their original games-switch semantics");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
