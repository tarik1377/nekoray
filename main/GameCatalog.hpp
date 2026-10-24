#pragma once

#include <QStringList>
#include <QList>

namespace GreenRhythm::GameCatalog {
struct Entry { QString title; QStringList processes; };
inline QList<Entry> games() {
    return {
        {"Counter-Strike", {"cs2.exe", "csgo.exe"}},
        {"Dota 2", {"dota2.exe"}},
        {"Apex Legends", {"r5apex.exe"}},
        {"VALORANT", {"VALORANT.exe", "VALORANT-Win64-Shipping.exe"}},
        {"League of Legends", {"League of Legends.exe", "LeagueClient.exe", "LeagueClientUx.exe", "LeagueClientUxRenderer.exe"}},
        {"Fortnite", {"FortniteClient-Win64-Shipping.exe", "FortniteClient-Win64-Shipping_BE.exe", "FortniteClient-Win64-Shipping_EAC.exe", "FortniteLauncher.exe"}},
        {"Squad", {"SquadGame-Win64-Shipping.exe", "SquadGame.exe", "Squad.exe"}},
        {"Sea of Thieves", {"SoTGame.exe", "SoTLauncher.exe"}},
        {"Escape from Tarkov", {"EscapeFromTarkov.exe", "BsgLauncher.exe"}}
    };
}
inline QStringList steam() { return {"steam.exe", "steamwebhelper.exe", "steamservice.exe", "SteamService.exe"}; }
inline QStringList processes() {
    auto result = steam();
    for (const auto &entry : games()) for (const auto &name : entry.processes)
        if (!result.contains(name, Qt::CaseInsensitive)) result.append(name);
    return result;
}
// Preserve the existing CS/Steam exception to the legacy "games via VPN"
// switch, extend it to Dota. Other games continue to honor that explicit switch.
inline QStringList alwaysDirect() {
    auto result = steam();
    result << "cs2.exe" << "csgo.exe" << "dota2.exe";
    return result;
}
} // namespace GreenRhythm::GameCatalog
