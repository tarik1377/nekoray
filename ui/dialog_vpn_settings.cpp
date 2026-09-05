#include "dialog_vpn_settings.h"
#include "ui/DialogPolish.hpp"
#include "ui_dialog_vpn_settings.h"

#include "main/GuiUtils.hpp"
#include "main/NekoGui.hpp"
#include "ui/mainwindow_interface.h"

#include "sys/ForeignTunnels.hpp"

#include <QMessageBox>

DialogVPNSettings::DialogVPNSettings(QWidget *parent) : QDialog(parent), ui(new Ui::DialogVPNSettings) {
    ui->setupUi(this);
    GreenRhythm::polishDialog(this);
    ADD_ASTERISK(this);

    ui->fake_dns->setChecked(NekoGui::dataStore->fake_dns);
    ui->vpn_implementation->setCurrentIndex(NekoGui::dataStore->vpn_implementation);
    ui->vpn_mtu->setCurrentText(Int2String(NekoGui::dataStore->vpn_mtu));
    ui->vpn_ipv6->setChecked(NekoGui::dataStore->vpn_ipv6);
    ui->hide_console->setChecked(NekoGui::dataStore->vpn_hide_console);
#ifndef Q_OS_WIN
    ui->hide_console->setVisible(false);
#endif
    ui->strict_route->setChecked(NekoGui::dataStore->vpn_strict_route);
    ui->single_core->setChecked(NekoGui::dataStore->vpn_internal_tun);
#ifdef Q_OS_MACOS
    /*
     * НЕДОСТУПНОЕ НА ПЛАТФОРМЕ ПРЯЧЕТСЯ, А НЕ ГАСИТСЯ СЕРЫМ.
     *
     * Серая галка обещает, что где-то есть условие, при котором она включится,
     * и человек будет его искать. Обеих настроек на маке нет вовсе:
     *
     *  — «одно ядро» (туннель внутри процесса) там невозможен: ядро запускается
     *    потомком интерфейса, интерфейс не root, повысить права уже запущенному
     *    процессу нельзя. См. NekoGui::PlatformSupportsInternalTun.
     *
     *  — strict_route в реализации sing-tun под darwin не существует вовсе
     *    (tun_darwin.go). Галка не отключала бы ничего — она создавала бы
     *    ложное впечатление, что защита включена.
     */
    ui->single_core->setVisible(false);
    ui->strict_route->setVisible(false);
#endif
    //
    D_LOAD_STRING_PLAIN(vpn_rule_cidr)
    D_LOAD_STRING_PLAIN(vpn_rule_process)
    D_LOAD_STRING_PLAIN(vpn_route_exclude_extra)

    /*
     * Кнопка ПОКАЗЫВАЕТ найденное, а не применяет его.
     *
     * Подставлять автоматически нельзя, и это не осторожность ради
     * осторожности: полнотуннельный чужой VPN владеет половиной адресного
     * пространства (0.0.0.0/1 и 128.0.0.0/1), и «нашёл — исключил» выключило бы
     * обход для половины интернета одним нажатием. Домашний же VPN с
     * разделённым туннелем даёт короткий список, который человек узнаёт с
     * первого взгляда.
     *
     * Поэтому найденное дописывается в конец поля, и человек видит, что именно
     * добавилось, до того как нажмёт «Сохранить».
     */
    connect(ui->fill_from_detected, &QPushButton::clicked, this, [this] {
        const auto found = NekoGui_sys::DetectForeignTunnels();
        if (found.isEmpty()) {
            QMessageBox::information(this, software_name,
                                     tr("Сторонних туннелей не найдено."));
            return;
        }

        QStringList add;
        QStringList wholeInternet;
        for (const auto &t: found) {
            if (t.ownsHalfTheInternet) {
                wholeInternet << t.name;
                continue; // его сети не предлагаем — см. выше
            }
            for (const auto &p: t.prefixes) {
                if (!p.trimmed().isEmpty()) add << p.trimmed();
            }
        }
        add.removeDuplicates();

        if (!wholeInternet.isEmpty()) {
            QMessageBox::warning(
                this, software_name,
                tr("Через %1 идёт весь трафик. Два полных туннеля на одной машине "
                   "несовместимы: его сети не подставлены, и включать наш туннель "
                   "вместе с ним не стоит.")
                    .arg(wholeInternet.join(", ")));
        }
        if (add.isEmpty()) return;

        auto text = ui->vpn_route_exclude_extra->toPlainText();
        if (!text.isEmpty() && !text.endsWith('\n')) text += "\n";
        ui->vpn_route_exclude_extra->setPlainText(text + add.join("\n") + "\n");
    });
    //
    connect(ui->whitelist_mode, &QCheckBox::stateChanged, this, [=](int state) {
        if (state == Qt::Checked) {
            ui->gb_cidr->setTitle(tr("Proxy CIDR"));
            ui->gb_process_name->setTitle(tr("Proxy Process Name"));
        } else {
            ui->gb_cidr->setTitle(tr("Bypass CIDR"));
            ui->gb_process_name->setTitle(tr("Bypass Process Name"));
        }
    });
    ui->whitelist_mode->setChecked(NekoGui::dataStore->vpn_rule_white);
}

DialogVPNSettings::~DialogVPNSettings() {
    delete ui;
}

void DialogVPNSettings::accept() {
    //
    auto mtu = ui->vpn_mtu->currentText().toInt();
    if (mtu > 10000 || mtu < 1000) mtu = 9000;
    NekoGui::dataStore->vpn_implementation = ui->vpn_implementation->currentIndex();
    NekoGui::dataStore->fake_dns = ui->fake_dns->isChecked();
    NekoGui::dataStore->vpn_mtu = mtu;
    NekoGui::dataStore->vpn_ipv6 = ui->vpn_ipv6->isChecked();
    NekoGui::dataStore->vpn_hide_console = ui->hide_console->isChecked();
    NekoGui::dataStore->vpn_strict_route = ui->strict_route->isChecked();
    NekoGui::dataStore->vpn_rule_white = ui->whitelist_mode->isChecked();
    bool isInternalChanged = NekoGui::dataStore->vpn_internal_tun != ui->single_core->isChecked();
    NekoGui::dataStore->vpn_internal_tun = ui->single_core->isChecked();
    //
    D_SAVE_STRING_PLAIN(vpn_rule_cidr)
    D_SAVE_STRING_PLAIN(vpn_rule_process)
    D_SAVE_STRING_PLAIN(vpn_route_exclude_extra)
    //
    QStringList msg{"UpdateDataStore"};
    if (isInternalChanged) {
        msg << "NeedRestart";
    } else {
        msg << "VPNChanged";
    }
    MW_dialog_message("", msg.join(","));
    QDialog::accept();
}

void DialogVPNSettings::on_troubleshooting_clicked() {
    auto r = QMessageBox::information(this, tr("Troubleshooting"),
                                      tr("If you have trouble starting VPN, you can force reset greenrhythm_core process here.\n\n"
                                         "If still not working, see documentation for more information.\n"
                                         "https://verdantvibe.ru"),
                                      tr("Reset"), tr("Cancel"), "",
                                      1, 1);
    if (r == 0) {
        GetMainWindow()->StopVPNProcess(true);
    }
}
