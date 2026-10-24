#pragma once
#include "TunBypassDialog.hpp"
#include "main/TunBypass.hpp"
#include "main/NekoGui.hpp"
#include "ui/mainwindow_interface.h"
namespace GreenRhythm {
inline void openTunBypass(QWidget *parent) {
    auto *settings = NekoGui::dataStore;
    const auto expected = settings->vpn_route_exclude_extra;
    TunBypassDialog dialog(expected, parent);
    dialog.setCommitter([settings, expected](const QString &text, QString *error) {
        if (!TunBypass::saveSetting(settings->fn, expected, text, error)) return false;
        settings->vpn_route_exclude_extra = text;
        MW_show_log(QObject::tr("Исключения TUN сохранены для следующего подключения. Текущее соединение продолжает работать."));
        return true;
    });
    dialog.exec();
}
}
