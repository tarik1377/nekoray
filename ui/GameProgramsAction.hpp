#pragma once

#include "GameProgramsDialog.hpp"
#include "main/GameAppRules.hpp"
#include "main/NekoGui.hpp"
#include "ui/mainwindow_interface.h"

namespace GreenRhythm {
inline void openGamePrograms(QWidget *parent) {
    auto *settings = NekoGui::dataStore;
    const auto expected = settings->game_app_rules;
    GameProgramsDialog dialog(expected, settings->spmode_vpn && NekoGui::UseInternalTun(), parent);
    dialog.setCommitter([settings, expected](const QString &text, QString *error) {
        if (!GameAppRules::saveSetting(settings->fn, expected, text, error)) return false;
        settings->game_app_rules = text;
        MW_show_log(QObject::tr("Выбор игр и программ сохранён. Применится при следующем подключении; текущее соединение работает дальше."));
        return true;
    });
    dialog.exec();
}
} // namespace GreenRhythm
