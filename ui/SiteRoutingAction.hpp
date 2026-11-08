#pragma once
#include "ui/SiteRoutingDialog.hpp"
#include "main/SiteRouting.hpp"
#include "main/NekoGui.hpp"
#include "ui/mainwindow_interface.h"
namespace GreenRhythm {
inline void openSiteRouting(QWidget *parent) {
    if (auto *existing = parent->findChild<QDialog *>("grSiteRouting")) { existing->show(); existing->raise(); return; }
    auto *settings = NekoGui::dataStore;
    const auto expected = settings->site_routing;
    auto *dialog = new SiteRoutingDialog(expected, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setCommitter([settings, expected](const QString &replacement, QString *error) {
        if (settings->site_routing != expected) { if(error) *error = QObject::tr("Выбор сайтов уже изменён. Откройте окно снова."); return false; }
        if (!SiteRouting::save(settings->fn, expected, replacement, error)) return false;
        settings->site_routing = replacement;
        MW_show_log(QObject::tr("Выбор сайтов сохранён с резервной копией. Применится при следующем подключении; текущее соединение работает дальше."));
        return true;
    });
    dialog->show();
}
}
