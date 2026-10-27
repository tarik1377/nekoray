#pragma once

#include "GameProgramsDialog.hpp"
#include "main/GameAppRules.hpp"
#include "main/NekoGui.hpp"
#include "ui/mainwindow_interface.h"
#include <QPointer>

namespace GreenRhythm {
inline void openGamePrograms(QWidget *parent, std::function<void(const QString &)> inspector = {}) {
    if (auto *existing = parent->findChild<QDialog *>("grGameProgramsDialog")) {
        existing->show(); existing->raise(); existing->activateWindow(); return;
    }
    auto *settings = NekoGui::dataStore;
    const auto expected = settings->game_app_rules;
    auto *dialog = new GameProgramsDialog(expected, settings->spmode_vpn && NekoGui::UseInternalTun(), parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (!inspector) {
        const QPointer<QWidget> owner(parent->window());
        inspector = [owner](const QString &name) {
            if (owner && GetMainWindow()) GetMainWindow()->inspect_program(name, owner);
        };
    }
    dialog->setInspector(std::move(inspector));
    dialog->setCommitter([settings, expected](const QString &text, QString *error) {
        if (!GameAppRules::saveSetting(settings->fn, expected, text, error)) return false;
        settings->game_app_rules = text;
        MW_show_log(QObject::tr("Выбор игр и программ сохранён. Применится при следующем подключении; текущее соединение работает дальше."));
        return true;
    });
    dialog->show();
}
} // namespace GreenRhythm
