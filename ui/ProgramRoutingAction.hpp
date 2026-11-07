#pragma once
#include "main/ProgramRouting.hpp"
#include "main/NekoGui.hpp"
#include "ui/mainwindow_interface.h"

namespace GreenRhythm {
inline ProgramRouting::State currentProgramRouting() {
    const auto *settings = NekoGui::dataStore;
    return {settings->vpn_rule_process, settings->vpn_rule_process_proxy, settings->game_app_rules, settings->vpn_rule_white,settings->roblox_split_processes,settings->roblox_auto_processes};
}
inline bool restoreProgramSiteRules(const QString &name, const ProgramRouting::State &expected, QString *error) {
    if (!(currentProgramRouting() == expected)) {
        if (error) *error = QObject::tr("Правила программ уже изменились. Откройте окно снова.");
        return false;
    }
    auto *settings = NekoGui::dataStore;
    ProgramRouting::State replacement;
    if (!ProgramRouting::saveSetting(settings->fn, expected, name, &replacement, error)) return false;
    settings->vpn_rule_process = replacement.bypass;
    settings->vpn_rule_process_proxy = replacement.proxy;
    settings->game_app_rules = replacement.applications;
    settings->roblox_split_processes = replacement.robloxSplit;
    settings->roblox_auto_processes = replacement.robloxAdaptive;
    MW_show_log(QObject::tr("«%1»: общие правила восстановлены. Сохранена резервная копия. Применится при следующем подключении; текущее соединение работает дальше.").arg(name));
    return true;
}
} // namespace GreenRhythm
