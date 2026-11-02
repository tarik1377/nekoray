#pragma once
#include "RobloxRoutingDialog.hpp"
#include "main/NekoGui.hpp"
#include "ui/mainwindow_interface.h"

namespace GreenRhythm {
inline void openRobloxRouting(QWidget *parent, std::function<void(const QString &)> inspector = {}) {
    if(auto *existing=parent->findChild<QDialog *>("grRobloxRouting")) { existing->show(); existing->raise(); return; }
    auto *settings=NekoGui::dataStore;
    const ProgramRouting::State expected{settings->vpn_rule_process,settings->vpn_rule_process_proxy,settings->game_app_rules,settings->vpn_rule_white};
    auto *dialog=new RobloxRoutingDialog(expected,settings->spmode_vpn && NekoGui::UseInternalTun(),parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setInspector(std::move(inspector));
    dialog->setCommitter([settings,expected](const ProgramRouting::State &candidate,QString *error) {
        if(!ProgramRouting::saveState(settings->fn,expected,candidate,error)) return false;
        settings->vpn_rule_process=candidate.bypass; settings->vpn_rule_process_proxy=candidate.proxy; settings->game_app_rules=candidate.applications;
        MW_show_log(QObject::tr("Выбор Roblox сохранён для следующего подключения. Текущее соединение работает дальше."));
        return true;
    });
    dialog->show();
}
}
