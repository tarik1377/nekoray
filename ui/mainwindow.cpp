#include "./ui_mainwindow.h"
#include "mainwindow.h"

#include "fmt/Preset.hpp"
#include "db/ProfileFilter.hpp"
#include "db/ConfigBuilder.hpp"
#include "sub/GroupUpdater.hpp"
#include "sys/ExternalProcess.hpp"
#include "sys/AutoRun.hpp"
#include "main/BrandingConstants.hpp"

#include "ui/ThemeManager.hpp"
#include "ui/Icon.hpp"
#include "ui/edit/dialog_edit_profile.h"
#include "ui/dialog_basic_settings.h"
#include "ui/dialog_manage_groups.h"
#include "ui/dialog_manage_routes.h"
#include "ui/dialog_vpn_settings.h"
#include "ui/dialog_hotkey.h"
#include "ui/dialog_relay_activate.h"

#include "3rdparty/fix_old_qt.h"
#include "3rdparty/qrcodegen.hpp"
#include "3rdparty/VT100Parser.hpp"
#include "3rdparty/qv2ray/v2/components/proxy/QvProxyConfigurator.hpp"

#ifndef NKR_NO_ZXING
#include "3rdparty/ZxingQtReader.hpp"
#endif

#ifdef Q_OS_WIN
#include "3rdparty/WinCommander.hpp"
#else
#ifdef Q_OS_LINUX
#include "sys/linux/LinuxCap.h"
#endif
#include <unistd.h>
#endif

#include <QClipboard>
#include <QLabel>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QColor>
#include <QBrush>
#include <QScrollBar>
#include <QScreen>
#include <QDesktopServices>
#include <QInputDialog>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QRegularExpression>
#include <QDateTime>
#include <QTcpSocket>
#include <QSslSocket>
#include <QHostInfo>
#include <QProgressDialog>
#include <QSysInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QFileDialog>
#include <QMenu>
#include <QHostAddress>

#ifdef Q_OS_WIN
static void RegisterGreenRhythmScheme();
#endif

void UI_InitMainWindow() {
    mainwindow = new MainWindow;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    mainwindow = this;
    MW_dialog_message = [=](const QString &a, const QString &b) {
        runOnUiThread([=] { dialog_message_impl(a, b); });
    };

    // Load Manager
    NekoGui::profileManager->LoadManager();

    // Setup misc UI
    themeManager->ApplyTheme(NekoGui::dataStore->theme);
    ui->setupUi(this);
    //
    connect(ui->menu_start, &QAction::triggered, this, [=]() { neko_start(); });
    connect(ui->menu_stop, &QAction::triggered, this, [=]() { neko_stop(); });
    connect(ui->tabWidget->tabBar(), &QTabBar::tabMoved, this, [=](int from, int to) {
        // use tabData to track tab & gid
        NekoGui::profileManager->groupsTabOrder.clear();
        for (int i = 0; i < ui->tabWidget->tabBar()->count(); i++) {
            NekoGui::profileManager->groupsTabOrder += ui->tabWidget->tabBar()->tabData(i).toInt();
        }
        NekoGui::profileManager->SaveManager();
    });
    ui->label_running->installEventFilter(this);
    ui->label_inbound->installEventFilter(this);
    ui->splitter->installEventFilter(this);
    //
    RegisterHotkey(false);
    //
    auto last_size = NekoGui::dataStore->mw_size.split("x");
    if (last_size.length() == 2) {
        auto w = last_size[0].toInt();
        auto h = last_size[1].toInt();
        if (w > 0 && h > 0) {
            resize(w, h);
        }
    }

    if (QDir("dashboard").count() == 0) {
        QDir().mkdir("dashboard");
        QFile::copy(":/neko/dashboard-notice.html", "dashboard/index.html");
    }

    // top bar
    ui->toolButton_program->setMenu(ui->menu_program);
    ui->toolButton_preferences->setMenu(ui->menu_preferences);
    ui->toolButton_server->setMenu(ui->menu_server);
    ui->menubar->setVisible(false);
    connect(ui->toolButton_document, &QToolButton::clicked, this, [=] { QDesktopServices::openUrl(QUrl(GreenRhythm::kSiteUrl)); });
    // «Подписка» is the brand hub: dropdown with Buy / Telegram / About. This also
    // makes the «Зелёный Ритм» menu (and the About dialog) reachable — otherwise it
    // lived only on the hidden menubar.
    ui->toolButton_ads->setMenu(ui->menu_greenrhythm);
    connect(ui->toolButton_update, &QToolButton::clicked, this, [=] { runOnNewThread([=] { CheckUpdate(); }); });
    connect(ui->toolButton_update_sub, &QToolButton::clicked, this, [=] { on_menu_update_subscription_triggered(); });
    connect(ui->toolButton_url_test, &QToolButton::clicked, this, [=] { speedtest_current_group(1, true); });
    connect(ui->toolButton_dashboard, &QToolButton::clicked, this, [=] {
        auto port = NekoGui::dataStore->core_box_clash_api;
        if (port <= 0) {
            MessageBoxWarning(software_name, tr("Enable the Clash API first in Preferences -> Core Options, then restart the core."));
            return;
        }
        auto secret = QString::fromUtf8(QUrl::toPercentEncoding(NekoGui::dataStore->core_box_clash_api_secret));
        // Open the locally-served dashboard (sing-box external_ui) — same loopback origin
        // as the API, so it avoids the browser's Private Network Access / CORS blocks that
        // break the hosted dashboard, and works offline.
        auto url = QStringLiteral("http://127.0.0.1:%1/ui/#/setup?hostname=127.0.0.1&port=%1&secret=%2").arg(port).arg(secret);
        QDesktopServices::openUrl(QUrl(url));
    });

    // GreenRhythm service entry points (opt-in default help/purchase points)
    connect(ui->menu_gr_connect, &QAction::triggered, this, [=] { smart_connect_greenrhythm(); });
    connect(ui->menu_gr_qr, &QAction::triggered, this, [=] { show_subscription_qr(); });
    connect(ui->menu_gr_diag, &QAction::triggered, this, [=] { run_diagnostics(); });
    connect(ui->menu_gr_fixnet, &QAction::triggered, this, [=] { repair_windows_network(); });
    ui->menu_gr_autopilot->setChecked(NekoGui::dataStore->connection_autopilot);
    connect(ui->menu_gr_autopilot, &QAction::toggled, this, [=](bool checked) {
        NekoGui::dataStore->connection_autopilot = checked;
        NekoGui::dataStore->Save();
    });
    // Autopilot watchdog: first probe soon after startup, then self-scheduled.
    autopilot_timer = new QTimer(this);
    autopilot_timer->setSingleShot(true);
    connect(autopilot_timer, &QTimer::timeout, this, [=] { autopilot_tick(); });
    autopilot_timer->start(20 * 1000);
    connect(ui->menu_gr_relay, &QAction::triggered, this, [=] {
        DialogRelayActivate d(this);
        d.exec();
    });
    connect(ui->menu_gr_buy, &QAction::triggered, this, [=] { QDesktopServices::openUrl(QUrl(GreenRhythm::kBuyUrl)); });
    connect(ui->menu_gr_telegram, &QAction::triggered, this, [=] { QDesktopServices::openUrl(QUrl(GreenRhythm::kTelegramUrl)); });
    connect(ui->menu_gr_about, &QAction::triggered, this, [=] { show_about_greenrhythm(); });

    // Setup log UI
    ui->splitter->restoreState(DecodeB64IfValid(NekoGui::dataStore->splitter_state));
    qvLogDocument->setUndoRedoEnabled(false);
    ui->masterLogBrowser->setUndoRedoEnabled(false);
    ui->masterLogBrowser->setDocument(qvLogDocument);
    ui->masterLogBrowser->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    {
        auto font = ui->masterLogBrowser->font();
        font.setPointSize(9);
        ui->masterLogBrowser->setFont(font);
        qvLogDocument->setDefaultFont(font);
    }
    connect(ui->masterLogBrowser->verticalScrollBar(), &QSlider::valueChanged, this, [=](int value) {
        if (ui->masterLogBrowser->verticalScrollBar()->maximum() == value)
            qvLogAutoScoll = true;
        else
            qvLogAutoScoll = false;
    });
    connect(ui->masterLogBrowser, &QTextBrowser::textChanged, this, [=]() {
        if (!qvLogAutoScoll)
            return;
        auto bar = ui->masterLogBrowser->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
    MW_show_log = [=](const QString &log) {
        runOnUiThread([=] { show_log_impl(log); });
    };
    MW_show_log_ext = [=](const QString &tag, const QString &log) {
        runOnUiThread([=] { show_log_impl("[" + tag + "] " + log); });
    };
    MW_show_log_ext_vt100 = [=](const QString &log) {
        runOnUiThread([=] { show_log_impl(cleanVT100String(log)); });
    };

    // table UI
    ui->proxyListTable->callback_save_order = [=] {
        auto group = NekoGui::profileManager->CurrentGroup();
        group->order = ui->proxyListTable->order;
        group->Save();
    };
    ui->proxyListTable->refresh_data = [=](int id) { refresh_proxy_list_impl_refresh_data(id); };
    if (auto button = ui->proxyListTable->findChild<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly)) {
        // Corner Button
        connect(button, &QAbstractButton::clicked, this, [=] { refresh_proxy_list_impl(-1, {GroupSortMethod::ById}); });
    }
    connect(ui->proxyListTable->horizontalHeader(), &QHeaderView::sectionClicked, this, [=](int logicalIndex) {
        GroupSortAction action;
        // 不正确的descending实现
        if (proxy_last_order == logicalIndex) {
            action.descending = true;
            proxy_last_order = -1;
        } else {
            proxy_last_order = logicalIndex;
        }
        action.save_sort = true;
        // 表头
        if (logicalIndex == 0) {
            action.method = GroupSortMethod::ByType;
        } else if (logicalIndex == 1) {
            action.method = GroupSortMethod::ByAddress;
        } else if (logicalIndex == 2) {
            action.method = GroupSortMethod::ByName;
        } else if (logicalIndex == 3) {
            action.method = GroupSortMethod::ByLatency;
        } else {
            return;
        }
        refresh_proxy_list_impl(-1, action);
    });
    connect(ui->proxyListTable->horizontalHeader(), &QHeaderView::sectionResized, this, [=](int logicalIndex, int oldSize, int newSize) {
        auto group = NekoGui::profileManager->CurrentGroup();
        if (NekoGui::dataStore->refreshing_group || group == nullptr || !group->manually_column_width) return;
        // save manually column width
        group->column_width.clear();
        for (int i = 0; i < ui->proxyListTable->horizontalHeader()->count(); i++) {
            group->column_width.push_back(ui->proxyListTable->horizontalHeader()->sectionSize(i));
        }
        group->column_width[logicalIndex] = newSize;
        group->Save();
    });
    ui->tableWidget_conn->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->tableWidget_conn->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->tableWidget_conn->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->tableWidget_conn->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    // Right-click a live connection → одним кликом сделать правило маршрутизации.
    ui->tableWidget_conn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableWidget_conn, &QWidget::customContextMenuRequested, this, [=](const QPoint &p) { show_conn_context_menu(p); });
    // Live route "map" strip above the connections table: at-a-glance split of what
    // goes 🌍 proxy / 🇷🇺 direct / ⛔ block, aggregating the fast-scrolling log.
    conn_route_summary = new QLabel(ui->tab_2);
    conn_route_summary->setTextFormat(Qt::RichText);
    conn_route_summary->setMargin(6);
    conn_route_summary->setText(tr("Нет активных соединений"));
    if (auto *lay = qobject_cast<QVBoxLayout *>(ui->tab_2->layout())) lay->insertWidget(0, conn_route_summary);
    // Card-like rows: taller for breathing room, no gridlines (surface comes from
    // the theme's alternating rows + rounded selection). Columns/sort/drag intact.
    ui->proxyListTable->verticalHeader()->setDefaultSectionSize(40);
    ui->proxyListTable->setShowGrid(false);

    build_onboarding_panel();
#ifdef Q_OS_WIN
    RegisterGreenRhythmScheme(); // greenrhythm:// one-click import (HKCU, no admin)
#endif

    // search box
    ui->search->setVisible(false);
    connect(shortcut_ctrl_f, &QShortcut::activated, this, [=] {
        ui->search->setVisible(true);
        ui->search->setFocus();
    });
    connect(shortcut_esc, &QShortcut::activated, this, [=] {
        if (ui->search->isVisible()) {
            ui->search->setText("");
            ui->search->textChanged("");
            ui->search->setVisible(false);
        }
        if (select_mode) {
            emit profile_selected(-1);
            select_mode = false;
            refresh_status();
        }
    });
    connect(ui->search, &QLineEdit::textChanged, this, [=](const QString &text) {
        if (text.isEmpty()) {
            for (int i = 0; i < ui->proxyListTable->rowCount(); i++) {
                ui->proxyListTable->setRowHidden(i, false);
            }
        } else {
            QList<QTableWidgetItem *> findItem = ui->proxyListTable->findItems(text, Qt::MatchContains);
            for (int i = 0; i < ui->proxyListTable->rowCount(); i++) {
                ui->proxyListTable->setRowHidden(i, true);
            }
            for (auto item: findItem) {
                if (item != nullptr) ui->proxyListTable->setRowHidden(item->row(), false);
            }
        }
    });

    // refresh
    this->refresh_groups();

    // Setup Tray
    tray = new QSystemTrayIcon(this); // 初始化托盘对象tray
    tray->setIcon(Icon::GetTrayIcon(Icon::NONE));
    tray->setContextMenu(ui->menu_program); // 创建托盘菜单
    // Surface one-click «Быстрое подключение» + the «Зелёный Ритм» hub at the top of
    // the tray menu (tray context menu == menu_program). Same QActions as the toolbar.
    if (!ui->menu_program->actions().isEmpty()) {
        auto *anchor = ui->menu_program->actions().first();
        ui->menu_program->insertAction(anchor, ui->menu_gr_connect);
        ui->menu_program->insertAction(anchor, ui->menu_greenrhythm->menuAction());
        ui->menu_program->insertSeparator(anchor);
    }
    tray->show();                           // 让托盘图标显示在系统托盘上
    connect(tray, &QSystemTrayIcon::activated, this, [=](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (this->isVisible()) {
                hide();
            } else {
                ActivateWindow(this);
            }
        }
    });

    // Misc menu
    connect(ui->menu_open_config_folder, &QAction::triggered, this, [=] { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::currentPath())); });
    ui->menu_program_preference->addActions(ui->menu_preferences->actions());
    connect(ui->menu_add_from_clipboard2, &QAction::triggered, ui->menu_add_from_clipboard, &QAction::trigger);
    connect(ui->actionRestart_Proxy, &QAction::triggered, this, [=] { if (NekoGui::dataStore->started_id>=0) neko_start(NekoGui::dataStore->started_id); });
    connect(ui->actionRestart_Program, &QAction::triggered, this, [=] { MW_dialog_message("", "RestartProgram"); });
    connect(ui->actionShow_window, &QAction::triggered, this, [=] { tray->activated(QSystemTrayIcon::ActivationReason::Trigger); });
    //
    connect(ui->menu_program, &QMenu::aboutToShow, this, [=]() {
        ui->actionRemember_last_proxy->setChecked(NekoGui::dataStore->remember_enable);
        ui->actionStart_with_system->setChecked(AutoRun_IsEnabled());
        ui->actionAllow_LAN->setChecked(QStringList{"::", "0.0.0.0"}.contains(NekoGui::dataStore->inbound_address));
        // active server
        for (const auto &old: ui->menuActive_Server->actions()) {
            ui->menuActive_Server->removeAction(old);
            old->deleteLater();
        }
        int active_server_item_count = 0;
        for (const auto &pf: NekoGui::profileManager->CurrentGroup()->ProfilesWithOrder()) {
            auto a = new QAction(pf->bean->DisplayTypeAndName(), this);
            a->setProperty("id", pf->id);
            a->setCheckable(true);
            if (NekoGui::dataStore->started_id == pf->id) a->setChecked(true);
            ui->menuActive_Server->addAction(a);
            if (++active_server_item_count == 100) break;
        }
        // active routing
        for (const auto &old: ui->menuActive_Routing->actions()) {
            ui->menuActive_Routing->removeAction(old);
            old->deleteLater();
        }
        for (const auto &name: NekoGui::Routing::List()) {
            auto a = new QAction(name, this);
            a->setCheckable(true);
            a->setChecked(name == NekoGui::dataStore->active_routing);
            ui->menuActive_Routing->addAction(a);
        }
    });
    connect(ui->menuActive_Server, &QMenu::triggered, this, [=](QAction *a) {
        bool ok;
        auto id = a->property("id").toInt(&ok);
        if (!ok) return;
        if (NekoGui::dataStore->started_id == id) {
            neko_stop();
        } else {
            neko_start(id);
        }
    });
    connect(ui->menuActive_Routing, &QMenu::triggered, this, [=](QAction *a) {
        auto fn = a->text();
        if (!fn.isEmpty()) {
            NekoGui::Routing r;
            r.load_control_must = true;
            r.fn = ROUTES_PREFIX + fn;
            if (r.Load()) {
                if (QMessageBox::question(GetMessageBoxParent(), software_name, tr("Load routing and apply: %1").arg(fn) + "\n" + r.DisplayRouting()) == QMessageBox::Yes) {
                    NekoGui::Routing::SetToActive(fn);
                    if (NekoGui::dataStore->started_id >= 0) {
                        neko_start(NekoGui::dataStore->started_id);
                    } else {
                        refresh_status();
                    }
                }
            }
        }
    });
    connect(ui->actionRemember_last_proxy, &QAction::triggered, this, [=](bool checked) {
        NekoGui::dataStore->remember_enable = checked;
        NekoGui::dataStore->Save();
    });
    connect(ui->actionStart_with_system, &QAction::triggered, this, [=](bool checked) {
        AutoRun_SetEnabled(checked);
    });
    connect(ui->actionAllow_LAN, &QAction::triggered, this, [=](bool checked) {
        NekoGui::dataStore->inbound_address = checked ? "::" : "127.0.0.1";
        MW_dialog_message("", "UpdateDataStore");
    });
    //
    connect(ui->checkBox_VPN, &QCheckBox::clicked, this, [=](bool checked) { neko_set_spmode_vpn(checked); });
    connect(ui->checkBox_SystemProxy, &QCheckBox::clicked, this, [=](bool checked) { neko_set_spmode_system_proxy(checked); });
    connect(ui->menu_spmode, &QMenu::aboutToShow, this, [=]() {
        ui->menu_spmode_disabled->setChecked(!(NekoGui::dataStore->spmode_system_proxy || NekoGui::dataStore->spmode_vpn));
        ui->menu_spmode_system_proxy->setChecked(NekoGui::dataStore->spmode_system_proxy);
        ui->menu_spmode_vpn->setChecked(NekoGui::dataStore->spmode_vpn);
    });
    connect(ui->menu_spmode_system_proxy, &QAction::triggered, this, [=](bool checked) { neko_set_spmode_system_proxy(checked); });
    connect(ui->menu_spmode_vpn, &QAction::triggered, this, [=](bool checked) { neko_set_spmode_vpn(checked); });
    connect(ui->menu_spmode_disabled, &QAction::triggered, this, [=]() {
        neko_set_spmode_system_proxy(false);
        neko_set_spmode_vpn(false);
    });
    connect(ui->menu_qr, &QAction::triggered, this, [=]() { display_qr_link(false); });
    connect(ui->menu_tcp_ping, &QAction::triggered, this, [=]() { speedtest_current_group(0, false); });
    connect(ui->menu_url_test, &QAction::triggered, this, [=]() { speedtest_current_group(1, false); });
    connect(ui->menu_full_test, &QAction::triggered, this, [=]() { speedtest_current_group(2, false); });
    connect(ui->menu_stop_testing, &QAction::triggered, this, [=]() { speedtest_current_group(114514, false); });
    //
    auto set_selected_or_group = [=](int mode) {
        // 0=group 1=select 2=unknown(menu is hide)
        ui->menu_server->setProperty("selected_or_group", mode);
    };
    auto move_tests_to_menu = [=](bool menuCurrent_Select) {
        return [=] {
            if (menuCurrent_Select) {
                ui->menuCurrent_Select->insertAction(ui->actionfake_4, ui->menu_tcp_ping);
                ui->menuCurrent_Select->insertAction(ui->actionfake_4, ui->menu_url_test);
                ui->menuCurrent_Select->insertAction(ui->actionfake_4, ui->menu_full_test);
                ui->menuCurrent_Select->insertAction(ui->actionfake_4, ui->menu_stop_testing);
                ui->menuCurrent_Select->insertAction(ui->actionfake_4, ui->menu_clear_test_result);
                ui->menuCurrent_Select->insertAction(ui->actionfake_4, ui->menu_resolve_domain);
            } else {
                ui->menuCurrent_Group->insertAction(ui->actionfake_5, ui->menu_tcp_ping);
                ui->menuCurrent_Group->insertAction(ui->actionfake_5, ui->menu_url_test);
                ui->menuCurrent_Group->insertAction(ui->actionfake_5, ui->menu_full_test);
                ui->menuCurrent_Group->insertAction(ui->actionfake_5, ui->menu_stop_testing);
                ui->menuCurrent_Group->insertAction(ui->actionfake_5, ui->menu_clear_test_result);
                ui->menuCurrent_Group->insertAction(ui->actionfake_5, ui->menu_resolve_domain);
            }
            set_selected_or_group(menuCurrent_Select ? 1 : 0);
        };
    };
    connect(ui->menuCurrent_Select, &QMenu::aboutToShow, this, move_tests_to_menu(true));
    connect(ui->menuCurrent_Group, &QMenu::aboutToShow, this, move_tests_to_menu(false));
    connect(ui->menu_server, &QMenu::aboutToHide, this, [=] {
        setTimeout([=] { set_selected_or_group(2); }, this, 200);
    });
    set_selected_or_group(2);
    //
    connect(ui->menu_share_item, &QMenu::aboutToShow, this, [=] {
        QString name;
        auto selected = get_now_selected_list();
        if (!selected.isEmpty()) {
            auto ent = selected.first();
            name = ent->bean->DisplayCoreType();
        }
        ui->menu_export_config->setVisible(name == software_core_name);
        ui->menu_export_config->setText(tr("Export %1 config").arg(name));
    });
    refresh_status();

    // Prepare core
    NekoGui::dataStore->core_token = GetRandomString(32);
    NekoGui::dataStore->core_port = MkPort();
    if (NekoGui::dataStore->core_port <= 0) NekoGui::dataStore->core_port = 19810;

    auto core_path = QApplication::applicationDirPath() + "/";
    core_path += "greenrhythm_core";

    QStringList args;
    args.push_back("greenrhythm");
    args.push_back("-port");
    args.push_back(Int2String(NekoGui::dataStore->core_port));
    if (NekoGui::dataStore->flag_debug) args.push_back("-debug");

    // Start core
    runOnUiThread(
        [=] {
            // A core from a previous session may still be running and holding the
            // inbound port, which makes Start fail with "address already in use".
            // Kill any leftover core before spawning ours (skip in multi-instance mode).
            if (!NekoGui::dataStore->flag_many) {
#ifdef Q_OS_WIN
                QProcess::execute("taskkill", {"/F", "/IM", "greenrhythm_core.exe"});
#else
                QProcess::execute("pkill", {"-9", "-f", "greenrhythm_core"});
#endif
            }
            core_process = new NekoGui_sys::CoreProcess(core_path, args);
            // Remember last started
            if (NekoGui::dataStore->remember_enable && NekoGui::dataStore->remember_id >= 0) {
                core_process->start_profile_when_core_is_up = NekoGui::dataStore->remember_id;
            }
            // Setup
            core_process->Start();
            setup_grpc();
        },
        DS_cores);

    // Remember system proxy
    if (NekoGui::dataStore->remember_enable || NekoGui::dataStore->flag_restart_tun_on) {
        if (NekoGui::dataStore->remember_spmode.contains("system_proxy")) {
            neko_set_spmode_system_proxy(true, false);
        }
        if (NekoGui::dataStore->remember_spmode.contains("vpn") || NekoGui::dataStore->flag_restart_tun_on) {
            neko_set_spmode_vpn(true, false);
        }
    }

    connect(qApp, &QGuiApplication::commitDataRequest, this, &MainWindow::on_commitDataRequest);

    auto t = new QTimer;
    connect(t, &QTimer::timeout, this, [=]() { refresh_status(); });
    t->start(2000);

    t = new QTimer;
    connect(t, &QTimer::timeout, this, [&] { NekoGui_sys::logCounter.fetchAndStoreRelaxed(0); });
    t->start(1000);

    // One-shot: legacy configs persisted the old disabled default (-30). Flip only that
    // exact value to the new enabled default so existing users start auto-refreshing
    // subscriptions (picking up server Reality key rotations) without overriding anyone
    // who chose their own interval/toggle. Runs once, then never touches it again.
    if (!NekoGui::dataStore->sub_auto_update_migrated) {
        if (NekoGui::dataStore->sub_auto_update == -30) NekoGui::dataStore->sub_auto_update = 120;
        NekoGui::dataStore->sub_auto_update_migrated = true;
        NekoGui::dataStore->Save();
    }

    // One-shot: turn the connection list on for everyone who already has a config. Unlike the
    // interval above there is no user choice to preserve — the settings checkbox was disabled,
    // so false is not a preference, it is just what the old shipped template wrote. Without
    // this the Connections tab and the route strip stay empty on every existing install, no
    // matter what the in-code default says.
    if (!NekoGui::dataStore->conn_stat_migrated) {
        NekoGui::dataStore->connection_statistics = true;
        NekoGui::dataStore->conn_stat_migrated = true;
        NekoGui::dataStore->Save();
    }

    // One-shot: bring existing routing schemes up to the shipped preset's QUIC guard and
    // local DNS for direct traffic. Additive — see Routing::MigrateOne for the limits.
    if (!NekoGui::dataStore->routing_quic_migrated) {
        const int migrated = NekoGui::Routing::MigrateAll();
        NekoGui::dataStore->routing_quic_migrated = true;
        NekoGui::dataStore->Save();
        if (migrated > 0)
            MW_show_log(tr("Маршрутизация обновлена: блокировка QUIC и локальный DNS (схем: %1).").arg(migrated));
    }

    TM_auto_update_subsctiption = new QTimer;
    TM_auto_update_subsctiption_Reset_Minute = [&](int m) {
        TM_auto_update_subsctiption->stop();
        if (m >= 30) TM_auto_update_subsctiption->start(m * 60 * 1000);
    };
    connect(TM_auto_update_subsctiption, &QTimer::timeout, this, [&] { UI_update_all_groups(true); });
    TM_auto_update_subsctiption_Reset_Minute(NekoGui::dataStore->sub_auto_update);

    if (!NekoGui::dataStore->flag_tray) show();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (tray->isVisible()) {
        hide();          // 隐藏窗口
        event->ignore(); // 忽略事件
    }
}

MainWindow::~MainWindow() {
    delete ui;
}

// Group tab manage

inline int tabIndex2GroupId(int index) {
    if (NekoGui::profileManager->groupsTabOrder.length() <= index) return -1;
    return NekoGui::profileManager->groupsTabOrder[index];
}

inline int groupId2TabIndex(int gid) {
    for (int key = 0; key < NekoGui::profileManager->groupsTabOrder.count(); key++) {
        if (NekoGui::profileManager->groupsTabOrder[key] == gid) return key;
    }
    return 0;
}

void MainWindow::on_tabWidget_currentChanged(int index) {
    if (NekoGui::dataStore->refreshing_group_list) return;
    if (tabIndex2GroupId(index) == NekoGui::dataStore->current_group) return;
    show_group(tabIndex2GroupId(index));
}

void MainWindow::show_group(int gid) {
    if (NekoGui::dataStore->refreshing_group) return;
    NekoGui::dataStore->refreshing_group = true;

    auto group = NekoGui::profileManager->GetGroup(gid);
    if (group == nullptr) {
        MessageBoxWarning(tr("Error"), QStringLiteral("No such group: %1").arg(gid));
        NekoGui::dataStore->refreshing_group = false;
        return;
    }

    if (NekoGui::dataStore->current_group != gid) {
        NekoGui::dataStore->current_group = gid;
        NekoGui::dataStore->Save();
    }
    ui->tabWidget->widget(groupId2TabIndex(gid))->layout()->addWidget(ui->proxyListTable);

    // 列宽是否可调
    if (group->manually_column_width) {
        for (int i = 0; i <= 4; i++) {
            ui->proxyListTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Interactive);
            auto size = group->column_width.value(i);
            if (size <= 0) size = ui->proxyListTable->horizontalHeader()->defaultSectionSize();
            ui->proxyListTable->horizontalHeader()->resizeSection(i, size);
        }
    } else {
        ui->proxyListTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        ui->proxyListTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        ui->proxyListTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        ui->proxyListTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        ui->proxyListTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    }

    // show proxies
    GroupSortAction gsa;
    gsa.scroll_to_started = true;
    refresh_proxy_list_impl(-1, gsa);

    NekoGui::dataStore->refreshing_group = false;
}

// callback

void MainWindow::dialog_message_impl(const QString &sender, const QString &info) {
    // greenrhythm:// deep link (startup args or forwarded by a second instance).
    // Handled first and returned: the raw link is untrusted and must not fall
    // through to the substring matching below.
    if (info.startsWith("SchemeImport#")) {
        import_scheme_url(info.mid(QStringLiteral("SchemeImport#").size()));
        return;
    }
    // info
    if (info.contains("UpdateIcon")) {
        icon_status = -1;
        refresh_status();
    }
    if (info.contains("UpdateDataStore")) {
        auto suggestRestartProxy = NekoGui::dataStore->Save();
        if (info.contains("RouteChanged")) {
            suggestRestartProxy = true;
        }
        if (info.contains("NeedRestart")) {
            suggestRestartProxy = false;
        }
        refresh_proxy_list();
        if (info.contains("VPNChanged") && NekoGui::dataStore->spmode_vpn) {
            MessageBoxWarning(tr("Tun Settings changed"), tr("Restart Tun to take effect."));
        }
        if (suggestRestartProxy && NekoGui::dataStore->started_id >= 0 &&
            QMessageBox::question(GetMessageBoxParent(), tr("Confirmation"), tr("Settings changed, restart proxy?")) == QMessageBox::StandardButton::Yes) {
            neko_start(NekoGui::dataStore->started_id);
        }
        refresh_status();
    }
    if (info.contains("NeedRestart")) {
        auto n = QMessageBox::warning(GetMessageBoxParent(), tr("Settings changed"), tr("Restart the program to take effect."), QMessageBox::Yes | QMessageBox::No);
        if (n == QMessageBox::Yes) {
            this->exit_reason = 2;
            on_menu_exit_triggered();
        }
    }
    //
    if (info == "RestartProgram") {
        this->exit_reason = 2;
        on_menu_exit_triggered();
    } else if (info == "Raise") {
        ActivateWindow(this);
    } else if (info == "ClearConnectionList") {
        refresh_connection_list({});
    }
    // sender
    if (sender == Dialog_DialogEditProfile) {
        auto msg = info.split(",");
        if (msg.contains("accept")) {
            refresh_proxy_list();
            if (msg.contains("restart")) {
                if (QMessageBox::question(GetMessageBoxParent(), tr("Confirmation"), tr("Settings changed, restart proxy?")) == QMessageBox::StandardButton::Yes) {
                    neko_start(NekoGui::dataStore->started_id);
                }
            }
        }
    } else if (sender == Dialog_DialogManageGroups) {
        if (info.startsWith("refresh")) {
            this->refresh_groups();
        }
    } else if (sender == "SubUpdater") {
        if (info.startsWith("finish")) {
            refresh_proxy_list();
            if (!info.contains("dingyue")) {
                show_log_impl(tr("Imported %1 profile(s)").arg(NekoGui::dataStore->imported_count));
            }
        } else if (info == "NewGroup") {
            refresh_groups();
        }
    } else if (sender == "ExternalProcess") {
        if (info == "Crashed") {
            neko_stop();
        } else if (info == "CoreCrashed") {
            neko_stop(true);
        } else if (info.startsWith("CoreStarted")) {
            neko_start(info.split(",")[1].toInt());
        }
    }
}

// top bar & tray menu

inline bool dialog_is_using = false;

#define USE_DIALOG(a)                               \
    if (dialog_is_using) return;                    \
    dialog_is_using = true;                         \
    auto dialog = new a(this);                      \
    connect(dialog, &QDialog::finished, this, [=] { \
        dialog->deleteLater();                      \
        dialog_is_using = false;                    \
    });                                             \
    dialog->show();

void MainWindow::on_menu_basic_settings_triggered() {
    USE_DIALOG(DialogBasicSettings)
}

void MainWindow::on_menu_manage_groups_triggered() {
    USE_DIALOG(DialogManageGroups)
}

void MainWindow::on_menu_routing_settings_triggered() {
    USE_DIALOG(DialogManageRoutes)
}

void MainWindow::on_menu_vpn_settings_triggered() {
    USE_DIALOG(DialogVPNSettings)
}

void MainWindow::on_menu_hotkey_settings_triggered() {
    USE_DIALOG(DialogHotkey)
}

void MainWindow::on_commitDataRequest() {
    qDebug() << "Start of data save";
    //
    if (!isMaximized()) {
        auto olds = NekoGui::dataStore->mw_size;
        auto news = QStringLiteral("%1x%2").arg(size().width()).arg(size().height());
        if (olds != news) {
            NekoGui::dataStore->mw_size = news;
        }
    }
    //
    NekoGui::dataStore->splitter_state = ui->splitter->saveState().toBase64();
    //
    auto last_id = NekoGui::dataStore->started_id;
    if (NekoGui::dataStore->remember_enable && last_id >= 0) {
        NekoGui::dataStore->remember_id = last_id;
    }
    //
    NekoGui::dataStore->Save();
    NekoGui::profileManager->SaveManager();
    qDebug() << "End of data save";
}

void MainWindow::on_menu_exit_triggered() {
    if (mu_exit.tryLock()) {
        NekoGui::dataStore->prepare_exit = true;
        //
        neko_set_spmode_system_proxy(false, false);
        neko_set_spmode_vpn(false, false);
        if (NekoGui::dataStore->spmode_vpn) {
            mu_exit.unlock(); // retry
            return;
        }
        RegisterHotkey(true);
        //
        on_commitDataRequest();
        //
        NekoGui::dataStore->save_control_no_save = true; // don't change datastore after this line
        neko_stop(false, true);
        //
        hide();
        runOnNewThread([=] {
            sem_stopped.acquire();
            stop_core_daemon();
            runOnUiThread([=] {
                on_menu_exit_triggered(); // continue exit progress
            });
        });
        return;
    }
    //
    MF_release_runguard();
    if (exit_reason == 1) {
        QDir::setCurrent(QApplication::applicationDirPath());
        QProcess::startDetached("./updater", QStringList{});
    } else if (exit_reason == 2 || exit_reason == 3) {
        QDir::setCurrent(QApplication::applicationDirPath());

        auto arguments = NekoGui::dataStore->argv;
        if (arguments.length() > 0) {
            arguments.removeFirst();
            arguments.removeAll("-tray");
            arguments.removeAll("-flag_restart_tun_on");
            arguments.removeAll("-flag_reorder");
        }
        auto isLauncher = qEnvironmentVariable("NKR_FROM_LAUNCHER") == "1";
        if (isLauncher) arguments.prepend("--");
        auto program = isLauncher ? "./launcher" : QApplication::applicationFilePath();

        if (exit_reason == 3) {
            // Tun restart as admin
            arguments << "-flag_restart_tun_on";
#ifdef Q_OS_WIN
            WinCommander::runProcessElevated(program, arguments, "", WinCommander::SW_NORMAL, false);
#else
            QProcess::startDetached(program, arguments);
#endif
        } else {
            QProcess::startDetached(program, arguments);
        }
    }
    tray->hide();
    QCoreApplication::quit();
}

#define neko_set_spmode_FAILED \
    refresh_status();          \
    return;

void MainWindow::neko_set_spmode_system_proxy(bool enable, bool save) {
    if (enable != NekoGui::dataStore->spmode_system_proxy) {
        if (enable) {
            auto socks_port = NekoGui::dataStore->inbound_socks_port;
            auto http_port = NekoGui::dataStore->inbound_socks_port;
            SetSystemProxy(http_port, socks_port);
        } else {
            ClearSystemProxy();
        }
    }

    if (save) {
        NekoGui::dataStore->remember_spmode.removeAll("system_proxy");
        if (enable && NekoGui::dataStore->remember_enable) {
            NekoGui::dataStore->remember_spmode.append("system_proxy");
        }
        NekoGui::dataStore->Save();
    }

    NekoGui::dataStore->spmode_system_proxy = enable;
    refresh_status();
}

void MainWindow::neko_set_spmode_vpn(bool enable, bool save) {
    if (enable != NekoGui::dataStore->spmode_vpn) {
        if (enable) {
            if (NekoGui::dataStore->vpn_internal_tun) {
                bool requestPermission = !NekoGui::IsAdmin();
                if (requestPermission) {
#ifdef Q_OS_LINUX
                    if (!Linux_HavePkexec()) {
                        MessageBoxWarning(software_name, "Please install \"pkexec\" first.");
                        neko_set_spmode_FAILED
                    }
                    auto ret = Linux_Pkexec_SetCapString(NekoGui::FindNekoBoxCoreRealPath(), "cap_net_admin=ep");
                    if (ret == 0) {
                        this->exit_reason = 3;
                        on_menu_exit_triggered();
                    } else {
                        MessageBoxWarning(software_name, "Setcap for Tun mode failed.\n\n1. You may canceled the dialog.\n2. You may be using an incompatible environment like AppImage.");
                        if (QProcessEnvironment::systemEnvironment().contains("APPIMAGE")) {
                            MW_show_log("If you are using AppImage, it's impossible to start a Tun. Please use other package instead.");
                        }
                    }
#endif
#ifdef Q_OS_WIN
                    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please run GreenRhythm as admin"), QMessageBox::Yes | QMessageBox::No);
                    if (n == QMessageBox::Yes) {
                        this->exit_reason = 3;
                        on_menu_exit_triggered();
                    }
#endif
                    neko_set_spmode_FAILED
                }
            } else {
                if (NekoGui::dataStore->need_keep_vpn_off) {
                    MessageBoxWarning(software_name, tr("Current server is incompatible with Tun. Please stop the server first, enable Tun Mode, and then restart."));
                    neko_set_spmode_FAILED
                }
                if (!StartVPNProcess()) {
                    neko_set_spmode_FAILED
                }
            }
        } else {
            if (NekoGui::dataStore->vpn_internal_tun) {
                // current core is sing-box
            } else {
                if (!StopVPNProcess()) {
                    neko_set_spmode_FAILED
                }
            }
        }
    }

    if (save) {
        NekoGui::dataStore->remember_spmode.removeAll("vpn");
        if (enable && NekoGui::dataStore->remember_enable) {
            NekoGui::dataStore->remember_spmode.append("vpn");
        }
        NekoGui::dataStore->Save();
    }

    NekoGui::dataStore->spmode_vpn = enable;
    refresh_status();

    if (NekoGui::dataStore->vpn_internal_tun && NekoGui::dataStore->started_id >= 0) neko_start(NekoGui::dataStore->started_id);
}

void MainWindow::refresh_status(const QString &traffic_update) {
    auto refresh_speed_label = [=] {
        if (traffic_update_cache == "") {
            ui->label_speed->setText(QObject::tr("Proxy: %1\nDirect: %2").arg("", ""));
        } else {
            ui->label_speed->setText(traffic_update_cache);
        }
    };

    // From TrafficLooper
    if (!traffic_update.isEmpty()) {
        traffic_update_cache = traffic_update;
        if (traffic_update == "STOP") {
            traffic_update_cache = "";
        } else {
            refresh_speed_label();
            return;
        }
    }

    refresh_speed_label();

    // Connection-status pill: green «Подключено · <server>» / grey «Не запущен».
    // Widget-level stylesheet coexists with ThemeManager's app sheet and re-applies
    // every refresh, so it self-heals across theme switches.
    {
        const bool up = (running != nullptr);
        const QString nm = up ? running->bean->DisplayName().left(26) : QString();
        // Three states, not two: grey «Не запущено», amber «Подключено, нет трафика» when
        // the tunnel is up but the autopilot probe cannot pass traffic, green otherwise.
        // The amber state is the answer support could never see before.
        const bool noTraffic = up && conn_health == Health_NoTraffic;
        QString pillText, pillColor;
        if (!up) {
            pillText = tr("Не запущено");
            pillColor = QStringLiteral("#5A5F66");
        } else if (noTraffic) {
            pillText = tr("Подключено, нет трафика");
            pillColor = QStringLiteral("#E3A008");
        } else {
            pillText = tr("Подключено") + QStringLiteral(" · ") + nm;
            pillColor = QStringLiteral("#3FB950");
        }
        ui->label_conn_pill->setText(pillText);
        ui->label_conn_pill->setToolTip(noTraffic
                                            ? tr("Соединение с сервером есть, но сайты не грузятся. "
                                                 "Частые причины: QUIC/DoH в браузере, DPI, посторонний "
                                                 "перехватчик. Попробуйте «Починить сеть Windows» или «Диагностику».")
                                            : QString());
        ui->label_conn_pill->setStyleSheet(QStringLiteral(
            "QLabel#label_conn_pill{border-radius:9px;padding:2px 10px;color:white;background:%1;}")
            .arg(pillColor));
        // Hug the text — otherwise the pill absorbs the row's slack and stretches.
        ui->label_conn_pill->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        // label_running duplicates the pill's "Not Running"; keep it only when it adds
        // info (running profile detail / select mode) — it is also the speedtest click target.
        ui->label_running->setVisible(up || select_mode);
    }
    refresh_subscription_status();

    // From UI
    QString group_name;
    if (running != nullptr) {
        auto group = NekoGui::profileManager->GetGroup(running->gid);
        if (group != nullptr) group_name = group->name;
    }

    if (last_test_time.addSecs(2) < QTime::currentTime()) {
        auto txt = running == nullptr ? tr("Not Running")
                                      : QStringLiteral("[%1] %2").arg(group_name, running->bean->DisplayName()).left(30);
        ui->label_running->setText(txt);
    }
    //
    auto display_socks = DisplayAddress(NekoGui::dataStore->inbound_address, NekoGui::dataStore->inbound_socks_port);
    auto inbound_txt = QStringLiteral("Mixed: %1").arg(display_socks);
    ui->label_inbound->setText(inbound_txt);
    //
    ui->checkBox_VPN->setChecked(NekoGui::dataStore->spmode_vpn);
    ui->checkBox_SystemProxy->setChecked(NekoGui::dataStore->spmode_system_proxy);
    if (select_mode) {
        ui->label_running->setText(tr("Select") + " *");
        ui->label_running->setToolTip(tr("Select mode, double-click or press Enter to select a profile, press ESC to exit."));
    } else {
        ui->label_running->setToolTip({});
    }

    auto make_title = [=](bool isTray) {
        QStringList tt;
        if (!isTray && NekoGui::IsAdmin()) tt << "[Admin]";
        if (select_mode) tt << "[" + tr("Select") + "]";
        if (!title_error.isEmpty()) tt << "[" + title_error + "]";
        if (NekoGui::dataStore->spmode_vpn && !NekoGui::dataStore->spmode_system_proxy) tt << "[Tun]";
        if (!NekoGui::dataStore->spmode_vpn && NekoGui::dataStore->spmode_system_proxy) tt << "[" + tr("System Proxy") + "]";
        if (NekoGui::dataStore->spmode_vpn && NekoGui::dataStore->spmode_system_proxy) tt << "[Tun+" + tr("System Proxy") + "]";
        tt << software_name;
        if (!isTray) tt << "(" + QString(NKR_VERSION) + ")";
        if (!NekoGui::dataStore->active_routing.isEmpty() && NekoGui::dataStore->active_routing != "Default") {
            tt << "[" + NekoGui::dataStore->active_routing + "]";
        }
        if (running != nullptr) tt << running->bean->DisplayTypeAndName() + "@" + group_name;
        return tt.join(isTray ? "\n" : " ");
    };

    auto icon_status_new = Icon::NONE;

    if (running != nullptr) {
        if (NekoGui::dataStore->spmode_vpn) {
            icon_status_new = Icon::VPN;
        } else if (NekoGui::dataStore->spmode_system_proxy) {
            icon_status_new = Icon::SYSTEM_PROXY;
        } else {
            icon_status_new = Icon::RUNNING;
        }
    }

    // refresh title & window icon
    setWindowTitle(make_title(false));
    if (icon_status_new != icon_status) QApplication::setWindowIcon(Icon::GetTrayIcon(Icon::NONE));

    // refresh tray
    if (tray != nullptr) {
        tray->setToolTip(make_title(true));
        if (icon_status_new != icon_status) tray->setIcon(Icon::GetTrayIcon(icon_status_new));
    }

    icon_status = icon_status_new;
}

// table显示

// refresh_groups -> show_group -> refresh_proxy_list
void MainWindow::refresh_groups() {
    NekoGui::dataStore->refreshing_group_list = true;

    // refresh group?
    for (int i = ui->tabWidget->count() - 1; i > 0; i--) {
        ui->tabWidget->removeTab(i);
    }

    int index = 0;
    for (const auto &gid: NekoGui::profileManager->groupsTabOrder) {
        auto group = NekoGui::profileManager->GetGroup(gid);
        if (index == 0) {
            ui->tabWidget->setTabText(0, group->name);
        } else {
            auto widget2 = new QWidget();
            auto layout2 = new QVBoxLayout();
            layout2->setContentsMargins(QMargins());
            layout2->setSpacing(0);
            widget2->setLayout(layout2);
            ui->tabWidget->addTab(widget2, group->name);
        }
        ui->tabWidget->tabBar()->setTabData(index, gid);
        index++;
    }

    // show after group changed
    if (NekoGui::profileManager->CurrentGroup() == nullptr) {
        NekoGui::dataStore->current_group = -1;
        ui->tabWidget->setCurrentIndex(groupId2TabIndex(0));
        show_group(NekoGui::profileManager->groupsTabOrder.count() > 0 ? NekoGui::profileManager->groupsTabOrder.first() : 0);
    } else {
        ui->tabWidget->setCurrentIndex(groupId2TabIndex(NekoGui::dataStore->current_group));
        show_group(NekoGui::dataStore->current_group);
    }

    NekoGui::dataStore->refreshing_group_list = false;
}

void MainWindow::refresh_proxy_list(const int &id) {
    refresh_proxy_list_impl(id, {});
}

void MainWindow::refresh_proxy_list_impl(const int &id, GroupSortAction groupSortAction) {
    // id < 0 重绘
    if (id < 0) {
        // 清空数据
        ui->proxyListTable->row2Id.clear();
        ui->proxyListTable->setRowCount(0);
        // 添加行 — lock while iterating the shared map (a background subscription
        // worker may be mutating it concurrently).
        int row = -1;
        {
            QMutexLocker locker(&NekoGui::profileManager->mutex);
            for (const auto &[id, profile]: NekoGui::profileManager->profiles) {
                if (NekoGui::dataStore->current_group != profile->gid) continue;
                row++;
                ui->proxyListTable->insertRow(row);
                ui->proxyListTable->row2Id += id;
            }
        }
    }

    // 显示排序
    if (id < 0) {
        switch (groupSortAction.method) {
            case GroupSortMethod::Raw: {
                auto group = NekoGui::profileManager->CurrentGroup();
                if (group == nullptr) return;
                ui->proxyListTable->order = group->order;
                break;
            }
            case GroupSortMethod::ById: {
                // Clear Order
                ui->proxyListTable->order.clear();
                ui->proxyListTable->callback_save_order();
                break;
            }
            case GroupSortMethod::ByAddress:
            case GroupSortMethod::ByName:
            case GroupSortMethod::ByLatency:
            case GroupSortMethod::ByType: {
                std::sort(ui->proxyListTable->order.begin(), ui->proxyListTable->order.end(),
                          [=](int a, int b) {
                              QString ms_a;
                              QString ms_b;
                              if (groupSortAction.method == GroupSortMethod::ByType) {
                                  ms_a = NekoGui::profileManager->GetProfile(a)->bean->DisplayType();
                                  ms_b = NekoGui::profileManager->GetProfile(b)->bean->DisplayType();
                              } else if (groupSortAction.method == GroupSortMethod::ByName) {
                                  ms_a = NekoGui::profileManager->GetProfile(a)->bean->name;
                                  ms_b = NekoGui::profileManager->GetProfile(b)->bean->name;
                              } else if (groupSortAction.method == GroupSortMethod::ByAddress) {
                                  ms_a = NekoGui::profileManager->GetProfile(a)->bean->DisplayAddress();
                                  ms_b = NekoGui::profileManager->GetProfile(b)->bean->DisplayAddress();
                              } else if (groupSortAction.method == GroupSortMethod::ByLatency) {
                                  ms_a = NekoGui::profileManager->GetProfile(a)->full_test_report;
                                  ms_b = NekoGui::profileManager->GetProfile(b)->full_test_report;
                              }
                              auto get_latency_for_sort = [](int id) {
                                  auto i = NekoGui::profileManager->GetProfile(id)->latency;
                                  if (i == 0) i = 100000;
                                  if (i < 0) i = 99999;
                                  return i;
                              };
                              if (groupSortAction.descending) {
                                  if (groupSortAction.method == GroupSortMethod::ByLatency) {
                                      if (ms_a.isEmpty() && ms_b.isEmpty()) {
                                          // compare latency if full_test_report is empty
                                          return get_latency_for_sort(a) > get_latency_for_sort(b);
                                      }
                                  }
                                  return ms_a > ms_b;
                              } else {
                                  if (groupSortAction.method == GroupSortMethod::ByLatency) {
                                      auto int_a = NekoGui::profileManager->GetProfile(a)->latency;
                                      auto int_b = NekoGui::profileManager->GetProfile(b)->latency;
                                      if (ms_a.isEmpty() && ms_b.isEmpty()) {
                                          // compare latency if full_test_report is empty
                                          return get_latency_for_sort(a) < get_latency_for_sort(b);
                                      }
                                  }
                                  return ms_a < ms_b;
                              }
                          });
                break;
            }
        }
        ui->proxyListTable->update_order(groupSortAction.save_sort);
    }

    // refresh data
    refresh_proxy_list_impl_refresh_data(id);
    refresh_onboarding();
}

// Onboarding / empty-state page. Lives in the same layout cell as the profile table:
// while there are no profiles the table is hidden and this page takes its slot, so it
// can never overlap or clip anything at any window size. Branding help point only —
// GreenRhythm stays a universal client (✕ hides it for the session).
void MainWindow::build_onboarding_panel() {
    auto *panel = new QFrame(this);
    panel->setObjectName("onboardingPanel");
    panel->hide();
    onboarding_panel = panel;

    // Translucent neutral card so the page follows any theme (dark or light);
    // the only hardcoded color is the brand-green primary button.
    panel->setStyleSheet(QStringLiteral(
        "#onboardingPanel{background:transparent;}"
        "#onboardCard{background-color:rgba(127,134,147,0.10);border:1px solid rgba(127,134,147,0.28);border-radius:12px;}"
        "QPushButton#onboardPrimary{background-color:#2ea043;color:#ffffff;border:none;border-radius:8px;padding:6px 22px;font-weight:600;}"
        "QPushButton#onboardPrimary:hover{background-color:#3fb950;}"
        "QPushButton#onboardPrimary:pressed{background-color:#2c974b;}"));

    auto *outer = new QVBoxLayout(panel);
    outer->setContentsMargins(24, 6, 24, 12);
    outer->setSpacing(0);

    auto *topRow = new QHBoxLayout();
    topRow->addStretch();
    auto *closeBtn = new QToolButton(panel);
    closeBtn->setText(QString::fromUtf8("\xE2\x9C\x95")); // ✕
    closeBtn->setAutoRaise(true);
    closeBtn->setToolTip(tr("Скрыть"));
    connect(closeBtn, &QToolButton::clicked, this, [=] {
        onboarding_dismissed = true;
        refresh_onboarding();
    });
    topRow->addWidget(closeBtn);
    outer->addLayout(topRow);

    outer->addStretch(2);

    auto *title = new QLabel(tr("Добро пожаловать в GreenRhythm") + QString::fromUtf8(" \xF0\x9F\x8C\xBF"), panel); // 🌿
    title->setAlignment(Qt::AlignHCenter);
    { QFont f = title->font(); f.setPointSizeF(f.pointSizeF() * 1.5); f.setBold(true); title->setFont(f); }
    outer->addWidget(title);
    outer->addSpacing(6);

    auto *subtitle = new QLabel(tr("Вставьте ссылку подписки — или получите доступ за пару минут"), panel);
    subtitle->setAlignment(Qt::AlignHCenter);
    subtitle->setEnabled(false); // dimmed via the theme's disabled palette
    outer->addWidget(subtitle);
    outer->addSpacing(16);

    // One centered card, width-capped so it reads like a dialog, not a stretched bar.
    auto *card = new QFrame(panel);
    card->setObjectName("onboardCard");
    card->setMaximumWidth(620);
    auto *cardL = new QVBoxLayout(card);
    cardL->setContentsMargins(18, 16, 18, 16);
    cardL->setSpacing(10);

    auto *rowA = new QHBoxLayout();
    rowA->setSpacing(8);
    auto *subEdit = new QLineEdit(card);
    subEdit->setPlaceholderText(tr("Ссылка подписки или профиля…"));
    subEdit->setMinimumHeight(32);
    rowA->addWidget(subEdit, 1);
    auto *importBtn = new QPushButton(tr("Импорт"), card);
    importBtn->setObjectName("onboardPrimary");
    importBtn->setMinimumHeight(32);
    importBtn->setCursor(Qt::PointingHandCursor);
    connect(importBtn, &QPushButton::clicked, this, [=] { import_link_offer_connect(subEdit->text()); });
    connect(subEdit, &QLineEdit::returnPressed, importBtn, &QPushButton::click);
    rowA->addWidget(importBtn);
    cardL->addLayout(rowA);

    auto *rowB = new QHBoxLayout();
    rowB->setSpacing(8);
    auto *pasteBtn = new QPushButton(tr("Вставить из буфера"), card);
    connect(pasteBtn, &QPushButton::clicked, this, [=] {
        const auto clip = QApplication::clipboard()->text().trimmed();
        if (clip.isEmpty()) return;
        import_link_offer_connect(clip);
    });
    rowB->addWidget(pasteBtn);
    rowB->addStretch();
    auto *links = new QLabel(card);
    links->setTextFormat(Qt::RichText);
    links->setOpenExternalLinks(true);
    links->setText(QStringLiteral("<a href=\"%1\" style=\"color:#3fb950;text-decoration:none;\">%2</a>"
                                  "&nbsp;&nbsp;·&nbsp;&nbsp;"
                                  "<a href=\"%3\" style=\"color:#3fb950;text-decoration:none;\">Telegram</a>")
                       .arg(GreenRhythm::kBuyUrl, tr("Нет подписки? Получить"), GreenRhythm::kTelegramUrl));
    rowB->addWidget(links);
    cardL->addLayout(rowB);

    auto *cardRow = new QHBoxLayout();
    cardRow->addStretch();
    cardRow->addWidget(card, 1);
    cardRow->addStretch();
    outer->addLayout(cardRow);

    outer->addStretch(3);
}

// Empty-state switcher: with zero profiles the table hides and the welcome page takes
// its layout slot; with any profile (or after ✕) the table is restored. The page follows
// the table across group tabs — both live in the current tab's layout.
void MainWindow::refresh_onboarding() {
    if (onboarding_panel == nullptr) return;
    const bool empty = NekoGui::profileManager->profiles.empty();
    if (!empty && !NekoGui::dataStore->onboarding_completed) {
        NekoGui::dataStore->onboarding_completed = true;
        NekoGui::dataStore->Save();
    }
    const bool show = empty && !onboarding_dismissed;
    if (show) {
        auto *host = ui->proxyListTable->parentWidget();
        if (host != nullptr && host->layout() != nullptr && onboarding_panel->parentWidget() != host) {
            host->layout()->addWidget(onboarding_panel);
        }
    }
    onboarding_panel->setVisible(show);
    ui->proxyListTable->setVisible(!show);
}

// ---------- greenrhythm:// one-click import ----------

#ifdef Q_OS_WIN
// HKCU\Software\Classes registration — per-user, no admin. Re-checked on every
// start so the handler self-heals when the exe is moved.
static void RegisterGreenRhythmScheme() {
    const auto exe = QDir::toNativeSeparators(QApplication::applicationFilePath());
    const auto cmd = QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1"));
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\greenrhythm"), QSettings::NativeFormat);
    if (reg.value(QStringLiteral("shell/open/command/.")).toString() == cmd) return;
    reg.setValue(QStringLiteral("."), QStringLiteral("URL:greenrhythm"));
    reg.setValue(QStringLiteral("URL Protocol"), QString());
    reg.setValue(QStringLiteral("shell/open/command/."), cmd);
}
#endif

// Contract: greenrhythm://import/<percent-encoded payload>, payload = https
// subscription link OR a single vless:// profile. The payload is UNTRUSTED:
// decode exactly once, cap at 8 KB, reject control characters and any other
// scheme (file://, javascript:, http://, ...). Never passed to a shell.
static QString ParseGreenRhythmImport(const QString &raw, QString *errOut) {
    const auto prefix = QStringLiteral("greenrhythm://import/");
    if (!raw.startsWith(prefix, Qt::CaseInsensitive)) {
        *errOut = QObject::tr("неизвестный формат ссылки");
        return {};
    }
    const auto payload = QUrl::fromPercentEncoding(raw.mid(prefix.size()).toUtf8()).trimmed();
    if (payload.toUtf8().size() > 8 * 1024) {
        *errOut = QObject::tr("слишком длинная ссылка");
        return {};
    }
    // Reject control chars (C0/C1) and Unicode bidi/zero-width format characters:
    // the whole safety model is "show the user the real source host", so RTLO and
    // homograph-hiding code points must not survive into the confirmation dialog.
    for (const auto &ch: payload) {
        const auto u = ch.unicode();
        const bool control = u < 0x20 || (u >= 0x7F && u <= 0x9F);
        const bool bidiOrZeroWidth = u == 0x200B || u == 0x200C || u == 0x200D || u == 0xFEFF ||
                                     (u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069);
        if (control || bidiOrZeroWidth) {
            *errOut = QObject::tr("недопустимые символы");
            return {};
        }
    }
    const bool schemeOk = payload.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
                          payload.startsWith(QStringLiteral("vless://"), Qt::CaseInsensitive);
    if (!schemeOk || !QUrl(payload).isValid()) {
        *errOut = QObject::tr("поддерживаются только https-ссылки подписок и vless-профили");
        return {};
    }
    return payload;
}

void MainWindow::import_scheme_url(const QString &raw) {
    ActivateWindow(this);
    QString err;
    const auto payload = ParseGreenRhythmImport(raw, &err);
    if (payload.isEmpty()) {
        MessageBoxWarning(tr("Импорт по ссылке"), tr("Ссылка не добавлена: %1.").arg(err));
        return;
    }
    // Reentrancy guard: QMessageBox::question below pumps a nested event loop, so a
    // rapid second deep link for the same payload could re-enter before the first
    // group is created and duplicate it. Ignore identical in-flight imports.
    if (scheme_import_inflight.contains(payload)) return;
    scheme_import_inflight.insert(payload);
    struct InflightGuard {
        QSet<QString> &set;
        QString key;
        ~InflightGuard() { set.remove(key); }
    } inflightGuard{scheme_import_inflight, payload};
    const QUrl url(payload);
    if (payload.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        // Subscription. Idempotent: the same link updates its existing group.
        std::shared_ptr<NekoGui::Group> group;
        {
            QMutexLocker locker(&NekoGui::profileManager->mutex);
            for (const auto &[gid, g]: NekoGui::profileManager->groups) {
                if (g != nullptr && g->url == payload) {
                    group = g;
                    break;
                }
            }
        }
        if (QMessageBox::question(GetMessageBoxParent(), tr("Зелёный Ритм — импорт"),
                                  tr("Добавить подписку с %1 и загрузить список серверов?").arg(url.host())) != QMessageBox::Yes) return;
        if (group == nullptr) {
            group = NekoGui::ProfileManager::NewGroup();
            group->name = GreenRhythm::kServiceName;
            group->url = payload;
            NekoGui::profileManager->AddGroup(group);
            refresh_groups();
        }
        const int gid = group->id;
        NekoGui_sub::groupUpdater->AsyncUpdate(payload, gid, [this, gid] {
            runOnUiThread([this, gid] {
                auto g = NekoGui::profileManager->GetGroup(gid);
                if (g == nullptr) return;
                const auto profiles = g->Profiles();
                refresh_groups();
                refresh_proxy_list();
                if (profiles.isEmpty()) {
                    MessageBoxWarning(tr("Зелёный Ритм"),
                                      tr("Подписка недоступна — возможно, срок истёк.\nПродлить: %1").arg(GreenRhythm::kRenewUrl));
                    return;
                }
                if (QMessageBox::question(GetMessageBoxParent(), tr("Зелёный Ритм"),
                                          tr("Подписка добавлена (профилей: %1). Подключиться сейчас?").arg(profiles.size())) == QMessageBox::Yes) {
                    neko_start(profiles.first()->id);
                }
            });
        });
    } else {
        // Single vless:// profile into the current group.
        if (QMessageBox::question(GetMessageBoxParent(), tr("Зелёный Ритм — импорт"),
                                  tr("Добавить профиль сервера %1?").arg(url.host())) != QMessageBox::Yes) return;
        // Snapshot existing profile ids so we start exactly the one this link added,
        // not whatever a concurrent update happened to give the highest id.
        auto before = std::make_shared<QSet<int>>();
        {
            QMutexLocker locker(&NekoGui::profileManager->mutex);
            for (const auto &[pid, p]: NekoGui::profileManager->profiles) before->insert(pid);
        }
        NekoGui_sub::groupUpdater->AsyncUpdate(payload, -1, [this, before] {
            runOnUiThread([this, before] {
                refresh_proxy_list();
                int added = -1;
                {
                    QMutexLocker locker(&NekoGui::profileManager->mutex);
                    for (const auto &[pid, p]: NekoGui::profileManager->profiles) {
                        if (!before->contains(pid)) { added = pid; break; }
                    }
                }
                if (added < 0) return;
                if (QMessageBox::question(GetMessageBoxParent(), tr("Зелёный Ритм"),
                                          tr("Профиль добавлен. Подключиться сейчас?")) == QMessageBox::Yes) {
                    neko_start(added);
                }
            });
        });
    }
}

// Smart connect: pick the fastest already-tested server in the «Зелёный Ритм» group
// (lowest positive latency; first server if none tested yet) and connect. Falls back
// to the current group so it also works as a generic "connect to best" action.
void MainWindow::smart_connect_greenrhythm() {
    std::shared_ptr<NekoGui::Group> gr;
    {
        QMutexLocker locker(&NekoGui::profileManager->mutex);
        for (const auto &[gid, g]: NekoGui::profileManager->groups) {
            if (g == nullptr) continue;
            if (g->name == GreenRhythm::kServiceName || g->url.contains(QStringLiteral("verdantvibe"), Qt::CaseInsensitive)) {
                gr = g;
                break;
            }
        }
    }
    if (gr == nullptr) gr = NekoGui::profileManager->CurrentGroup();
    if (gr == nullptr) return;

    auto profiles = gr->Profiles();
    if (profiles.isEmpty()) {
        MessageBoxWarning(tr("Быстрое подключение"),
                          tr("Нет серверов. Импортируйте подписку «Зелёный Ритм»."));
        return;
    }
    std::shared_ptr<NekoGui::ProxyEntity> best;
    for (const auto &p: profiles) {
        if (p->latency > 0 && (best == nullptr || p->latency < best->latency)) best = p;
    }
    if (best == nullptr) best = profiles.first(); // nothing tested yet — take the first
    neko_start(best->id);
}

// QR bridge: show the subscription link as a QR code to scan in a mobile client —
// one subscription across devices. The image is decoded back with the bundled ZXing
// reader before it is shown: a QR we cannot read ourselves never reaches the screen.
void MainWindow::show_subscription_qr() {
    std::shared_ptr<NekoGui::Group> gr;
    {
        QMutexLocker locker(&NekoGui::profileManager->mutex);
        for (const auto &[gid, g]: NekoGui::profileManager->groups) {
            if (g == nullptr || g->url.isEmpty()) continue;
            if (g->name == GreenRhythm::kServiceName || g->url.contains(QStringLiteral("verdantvibe"), Qt::CaseInsensitive)) {
                gr = g;
                break;
            }
        }
    }
    if (gr == nullptr) {
        auto cg = NekoGui::profileManager->CurrentGroup();
        if (cg != nullptr && !cg->url.isEmpty()) gr = cg;
    }
    if (gr == nullptr) {
        MessageBoxWarning(tr("QR подписки"), tr("Нет группы-подписки. Импортируйте подписку «Зелёный Ритм»."));
        return;
    }

    QImage im;
    try {
        const auto qr = qrcodegen::QrCode::encodeText(gr->url.toUtf8().data(), qrcodegen::QrCode::Ecc::MEDIUM);
        constexpr qint32 pad = 2;
        const qint32 sz = qr.getSize();
        im = QImage(sz + pad * 2, sz + pad * 2, QImage::Format_RGB32);
        im.fill(qRgb(255, 255, 255));
        for (int y = 0; y < sz; y++)
            for (int x = 0; x < sz; x++)
                if (qr.getModule(x, y)) im.setPixel(x + pad, y + pad, qRgb(0, 0, 0));
    } catch (const std::exception &ex) {
        MessageBoxWarning(tr("QR подписки"), ex.what());
        return;
    }

#ifndef NKR_NO_ZXING
    {
        using namespace ZXingQt;
        auto hints = DecodeHints()
                         .setFormats(BarcodeFormat::QRCode)
                         .setTryRotate(false)
                         .setBinarizer(Binarizer::FixedThreshold);
        const auto scaled = im.scaled(im.width() * 4, im.height() * 4, Qt::KeepAspectRatio, Qt::FastTransformation);
        if (ReadBarcode(scaled, hints).text() != gr->url) {
            MessageBoxWarning(tr("QR подписки"), tr("Самопроверка QR-кода не прошла — код не показан."));
            return;
        }
        MW_show_log(tr("QR подписки: самопроверка декодирования пройдена."));
    }
#endif

    auto w = new QDialog(this);
    w->setWindowTitle(tr("QR подписки"));
    auto lay = new QVBoxLayout(w);
    auto pic = new QLabel(w);
    pic->setPixmap(QPixmap::fromImage(im.scaled(340, 340, Qt::KeepAspectRatio, Qt::FastTransformation), Qt::MonoOnly));
    pic->setAlignment(Qt::AlignCenter);
    pic->setMargin(8);
    lay->addWidget(pic);
    auto hint = new QLabel(tr("Отсканируйте в мобильном клиенте — одна подписка на всех устройствах."), w);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    auto copyBtn = new QPushButton(tr("Копировать ссылку"), w);
    connect(copyBtn, &QPushButton::clicked, w, [url = gr->url] { QApplication::clipboard()->setText(url); });
    lay->addWidget(copyBtn);
    w->exec();
    w->deleteLater();
}

// Onboarding / paste import that offers to connect on success — mirrors the deep-link
// flow so the first-run funnel doesn't dead-end at an empty table. Diffs the profile
// set around the async import to find what was added; offers the first new server.
void MainWindow::import_link_offer_connect(const QString &link) {
    const auto trimmed = link.trimmed();
    if (trimmed.isEmpty()) return;
    auto before = std::make_shared<QSet<int>>();
    {
        QMutexLocker locker(&NekoGui::profileManager->mutex);
        for (const auto &[pid, p]: NekoGui::profileManager->profiles) before->insert(pid);
    }
    NekoGui_sub::groupUpdater->AsyncUpdate(trimmed, -1, [this, before] {
        runOnUiThread([this, before] {
            refresh_proxy_list();
            int first = -1, added = 0;
            {
                QMutexLocker locker(&NekoGui::profileManager->mutex);
                for (const auto &[pid, p]: NekoGui::profileManager->profiles) {
                    if (before->contains(pid)) continue;
                    added++;
                    if (first < 0) first = pid; // std::map iterates ascending → lowest new id
                }
            }
            if (first < 0) return; // nothing added (import cancelled or failed)
            if (QMessageBox::question(GetMessageBoxParent(), GreenRhythm::kServiceName,
                                      tr("Добавлено серверов: %1. Подключиться сейчас?").arg(added)) == QMessageBox::Yes) {
                neko_start(first);
            }
        });
    });
}

// One-click connection diagnostics — the manual check we kept doing by hand (is the
// internet up? does DNS resolve? does the server's port accept TCP? does its TLS
// handshake complete, or is it DPI-filtered?). Runs off the UI thread; produces a
// plain-language verdict plus a secrets-free report the user can hand to support.
void MainWindow::run_diagnostics() {
    QString host;
    int port = 0;
    {
        std::shared_ptr<NekoGui::ProxyEntity> ent;
        auto sel = get_now_selected_list();
        if (!sel.isEmpty()) {
            ent = sel.first();
        } else if (NekoGui::dataStore->started_id >= 0) {
            ent = NekoGui::profileManager->GetProfile(NekoGui::dataStore->started_id);
        } else {
            auto cg = NekoGui::profileManager->CurrentGroup();
            if (cg != nullptr) {
                auto ps = cg->ProfilesWithOrder();
                if (!ps.isEmpty()) ent = ps.first();
            }
        }
        if (ent != nullptr && ent->bean != nullptr) {
            host = ent->bean->serverAddress;
            port = ent->bean->serverPort;
        }
    }

    // Whether the tunnel is up, and where to reach it, are UI-thread facts — capture them
    // before the worker so it can probe THROUGH the tunnel, not just around it.
    const bool up = (running != nullptr);
    const QString proxyAddr = NekoGui::dataStore->inbound_address;
    const quint16 proxyPort = quint16(NekoGui::dataStore->inbound_socks_port);
    const QString header = diagnostics_header();

    auto *dlg = new QProgressDialog(tr("Проверка соединения…"), QString(), 0, 0, this);
    dlg->setWindowTitle(tr("Диагностика соединения"));
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setCancelButton(nullptr);
    dlg->setMinimumDuration(0);
    dlg->show();

    runOnNewThread([this, host, port, up, proxyAddr, proxyPort, header, dlg] {
        bool net, dns, tcp = false, tls = false;
        {
            QTcpSocket s;
            s.connectToHost(QStringLiteral("1.1.1.1"), 443);
            net = s.waitForConnected(4000);
            s.abort();
        }
        {
            auto hi = QHostInfo::fromName(QStringLiteral("www.google.com"));
            dns = hi.error() == QHostInfo::NoError && !hi.addresses().isEmpty();
        }
        if (!host.isEmpty() && port > 0) {
            {
                QTcpSocket s;
                s.connectToHost(host, port);
                tcp = s.waitForConnected(5000);
                s.abort();
            }
            if (tcp) {
                QSslSocket ss;
                ss.setPeerVerifyMode(QSslSocket::VerifyNone); // Reality/self-signed nodes
                ss.connectToHostEncrypted(host, port);
                tls = ss.waitForEncrypted(6000);
                ss.abort();
            }
        }

        // The decisive test support could never run: does real traffic actually pass THROUGH
        // the running tunnel, and what exit IP does the world see? A 204 through the local
        // inbound proves the whole path; cloudflare's trace gives the exit IP for free.
        bool tunnelTested = false, tunnelOk = false;
        QString exitIp;
        if (up) {
            tunnelTested = true;
            QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, proxyAddr, proxyPort);
            QNetworkAccessManager nam;
            nam.setProxy(proxy);
            {
                auto *r = nam.get(QNetworkRequest(QUrl(QStringLiteral("http://cp.cloudflare.com/generate_204"))));
                QEventLoop loop;
                QTimer::singleShot(7000, r, &QNetworkReply::abort);
                QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                loop.exec();
                tunnelOk = r->error() == QNetworkReply::NoError;
                r->deleteLater();
            }
            if (tunnelOk) {
                auto *r = nam.get(QNetworkRequest(QUrl(QStringLiteral("https://cp.cloudflare.com/cdn-cgi/trace"))));
                QEventLoop loop;
                QTimer::singleShot(7000, r, &QNetworkReply::abort);
                QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                loop.exec();
                if (r->error() == QNetworkReply::NoError) {
                    for (const auto &ln: QString::fromUtf8(r->readAll()).split('\n')) {
                        if (ln.startsWith("ip=")) { exitIp = ln.mid(3).trimmed(); break; }
                    }
                }
                r->deleteLater();
            }
        }

        runOnUiThread([=] {
            dlg->close();
            dlg->deleteLater();

            QString verdict;
            if (!net)
                verdict = tr("Нет интернета — проверьте подключение к сети.");
            else if (!dns)
                verdict = tr("Интернет есть, но DNS не отвечает — попробуйте сменить DNS-сервер.");
            else if (host.isEmpty())
                verdict = tr("Сеть в порядке. Сервер не выбран — импортируйте подписку и выберите сервер.");
            else if (!tcp)
                verdict = tr("Сервер недоступен из вашей сети — возможно, блокировка провайдера или сервер выключен. Попробуйте другой сервер.");
            else if (!tls)
                verdict = tr("Сервер отвечает, но TLS не проходит — вероятна фильтрация (DPI). Попробуйте другой сервер или порт 443.");
            else if (tunnelTested && !tunnelOk)
                verdict = tr("VPN подключён, но трафик через него не проходит. Частые причины: QUIC/DoH "
                             "в браузере или посторонний перехватчик (Zapret/GoodbyeDPI/WARP). "
                             "Нажмите «Починить сеть Windows» в меню «Зелёный Ритм».");
            else if (tunnelTested && tunnelOk)
                verdict = tr("Всё работает — трафик идёт через VPN, внешний IP: %1.").arg(exitIp.isEmpty() ? "—" : exitIp);
            else
                verdict = tr("Сеть и сервер доступны. Нажмите «Подключиться», чтобы пустить трафик через VPN.");

            auto mark = [](bool b) { return b ? QString::fromUtf8("\xE2\x9C\x94") : QString::fromUtf8("\xE2\x9C\x95"); }; // ✔ / ✕
            QStringList rows;
            rows << tr("Интернет: %1").arg(mark(net));
            rows << tr("DNS: %1").arg(mark(dns));
            if (!host.isEmpty()) {
                rows << tr("Сервер (TCP): %1").arg(mark(tcp));
                rows << tr("TLS-соединение: %1").arg(mark(tls));
            }
            if (tunnelTested) {
                rows << tr("Трафик через VPN: %1").arg(mark(tunnelOk));
                if (tunnelOk) rows << tr("Внешний IP: %1").arg(exitIp.isEmpty() ? "—" : exitIp);
            }

            // Support report — no secrets: server host:port is public and helps support,
            // but the subscription token / keys are never included.
            QString report = header;
            report += QStringLiteral("Internet: %1\nDNS: %2\n").arg(net ? "OK" : "FAIL", dns ? "OK" : "FAIL");
            if (!host.isEmpty()) {
                report += QStringLiteral("Server %1:%2 TCP: %3\nTLS: %4\n")
                              .arg(host).arg(port).arg(tcp ? "OK" : "FAIL", tls ? "OK" : "FAIL");
            }
            if (tunnelTested) {
                report += QStringLiteral("Tunnel: %1\nExitIP: %2\n").arg(tunnelOk ? "OK" : "FAIL", exitIp.isEmpty() ? "-" : exitIp);
            }
            report += QStringLiteral("Verdict: %1\n").arg(verdict);

            QMessageBox box(QMessageBox::Information, tr("Диагностика соединения"),
                            rows.join("\n") + "\n\n" + verdict, QMessageBox::Ok, GetMessageBoxParent());
            auto *copyBtn = box.addButton(tr("Скопировать отчёт"), QMessageBox::ActionRole);
            auto *tgBtn = box.addButton(tr("Написать в поддержку"), QMessageBox::ActionRole);
            box.exec();
            if (box.clickedButton() == copyBtn) {
                QApplication::clipboard()->setText(report);
            } else if (box.clickedButton() == tgBtn) {
                QApplication::clipboard()->setText(report); // report is on the clipboard to paste
                QDesktopServices::openUrl(QUrl(GreenRhythm::kTelegramUrl));
            }
        });
    });
}

// Repairs a Windows network stack left broken by OTHER tools. Users who tried
// Zapret/GoodbyeDPI/WARP and "uninstalled" them keep the parts that actually block
// us: the WinDivert driver still filters packets, services still run, and the system
// proxy/DNS still point at a resolver that no longer exists — so our tunnel cannot
// come up and the user only sees "не подключается". Deleting the app folder removes
// none of that, which is why support kept hitting a wall.
//
// Everything here is destructive and needs elevation, so it is strictly opt-in: we
// spell out what will change, require confirmation, and never touch the hosts file —
// on a real machine it held the user's own work entries (corporate hosts, docker),
// and wiping those would break something we were never asked to touch.
void MainWindow::repair_windows_network() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, tr("Починить сеть Windows"),
                             tr("Эта функция доступна только в Windows."));
#else
    QMessageBox ask(QMessageBox::Warning, tr("Починить сеть Windows"),
                    tr("Другие программы обхода блокировок (Zapret, GoodbyeDPI, WARP) "
                       "и посторонние VPN перехватывают трафик раньше нашего клиента — "
                       "из-за этого подключение есть, а сайты не открываются.\n\n"
                       "Будут убраны:\n"
                       "• драйверы-перехватчики (WinDivert и подобные);\n"
                       "• их службы, задания и записи автозапуска;\n"
                       "• посторонние VPN-адаптеры;\n"
                       "• зависший прокси, мёртвый DNS, кэш DNS, Winsock и стек TCP/IP.\n\n"
                       "Ваши файлы, пароли и файл hosts НЕ затрагиваются.\n\n"
                       "Нужны права администратора и перезагрузка. Продолжить?"),
                    QMessageBox::NoButton, this);
    auto *go = ask.addButton(tr("Починить"), QMessageBox::AcceptRole);
    ask.addButton(tr("Отмена"), QMessageBox::RejectRole);
    ask.exec();
    if (ask.clickedButton() != go) return;

    // Written from what actually broke on customer machines, not from a generic list:
    //  - Zapret registers its service under an arbitrary name (winws1, zapret1, ...),
    //    so services and drivers are matched on their ImagePath. Matching by name
    //    reported a clean machine while Zapret was plainly installed on it.
    //  - Its WinDivert driver keeps filtering TCP until a reboot even after the files
    //    are gone, which is why the reboot below is not optional.
    //  - Foreign TUN/TAP adapters (a stale outline-tap0 among them) keep their own DNS
    //    and compete for routing; ours is excluded by name so we never disable it.
    //  - hosts is never touched: a real machine had legitimate work entries in it.
    // Two passes. Most of the work needs admin rights, but two things must run as the
    // logged-in customer, not the elevating admin: the per-user system proxy and the
    // browser DoH check both live in the customer's own profile, and on a standard-user PC
    // the UAC prompt elevates a different account whose HKCU/AppData is the wrong one. So a
    // non-elevated pass runs first as the customer, then the elevated pass does the rest.
    //
    // The scripts are C++11 raw literals (no backslash/quote escaping) written to temp .ps1
    // files with a UTF-8 BOM so PowerShell 5.1 reads the Cyrillic. Written from what actually
    // breaks RU machines:
    //  - bypass tools register services under arbitrary names and via nssm, so services are
    //    matched on ImagePath, Name AND DisplayName; ByeDPI's real binary is ciadpi.exe
    //    (+proxifyre.exe / the bdmanager supervisor), which the old 'byedpi' token never hit.
    //  - the forced reboot used to re-arm the tool from its Run key / Startup shortcut; those
    //    are now removed first, ours excluded by the 'greenrhythm' marker, and the HKCU Run
    //    key is swept per real-user SID under HKEY_USERS so the elevating admin's hive is not
    //    the one we look at.
    //  - stopping WARP's service leaves the adapter on WARP's dead loopback DNS (127.0.2.x),
    //    so the machine could resolve nothing; loopback resolvers are probed and only the
    //    dead ones reset — never a live AdGuard/Acrylic the user installed on purpose.
    //  - kill-switch WFP filters, the ndisrd driver and browser Secure-DNS are only reported,
    //    never touched: they explain "clean machine still broken" without us deleting an
    //    antivirus or a setting the user needs.
    //  - the adapter pass can never touch real hardware. It used to select purely on the
    //    description text and then act by NAME, which is two hazards at once: a physical
    //    NIC whose description happens to carry one of the tokens would be disabled, and
    //    Disable-NetAdapter -Name treats its argument as a WILDCARD — and Windows really
    //    does name adapters "Подключение по локальной сети* 2". A report naming
    //    "Ethernet 2" (which on the dev box is the physical Intel NIC) is what prompted
    //    this. Now: virtual adapters only, never the one carrying the default route,
    //    acted on by object rather than by name, and the log records the description and
    //    whether the adapter actually ended up disabled instead of assuming it did.
    //  - 'netsh int ip reset' is skipped when a PHYSICAL adapter has a static IP (offices),
    //    so we do not wipe a fixed LAN address; Hyper-V/WSL virtual adapters do not count.
    //  - hosts is never touched: real customers had legitimate work entries in it.
    static const char *const kUserPs = R"PS(
$u=@()
$k='HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
if((Get-ItemProperty $k -Name ProxyEnable -EA SilentlyContinue).ProxyEnable){$u+='системный прокси отключён'}
Set-ItemProperty $k ProxyEnable 0 -EA SilentlyContinue
if((Get-ItemProperty $k -Name AutoConfigURL -EA SilentlyContinue).AutoConfigURL){$u+='PAC-скрипт удалён'; Remove-ItemProperty $k AutoConfigURL -EA SilentlyContinue}
$t='winws|zapret|goodbyedpi|byedpi|ciadpi|proxifyre|spoofdpi|powertunnel'
$rk='HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if(Test-Path $rk){$ip=Get-ItemProperty $rk; foreach($n in (Get-Item $rk).Property){$v=[string]$ip.$n; if($v -match $t -and $v -notmatch 'greenrhythm'){$u+=('автозапуск удалён: '+$n); Remove-ItemProperty $rk $n -EA SilentlyContinue}}}
$sf=[Environment]::GetFolderPath('Startup')
if($sf -and (Test-Path $sf)){foreach($f in Get-ChildItem $sf -File -EA SilentlyContinue){$c=''; if($f.Extension -match '\.(bat|cmd|ps1)$'){$c=Get-Content $f.FullName -Raw -EA SilentlyContinue} elseif($f.Extension -eq '.lnk'){try{$sh=New-Object -ComObject WScript.Shell; $l=$sh.CreateShortcut($f.FullName); $c=$l.TargetPath+' '+$l.Arguments}catch{}}; if($c -match $t){$u+=('автозапуск удалён: '+$f.Name); Remove-Item $f.FullName -Force -EA SilentlyContinue}}}
foreach($b in @(@('Chrome',"$env:LOCALAPPDATA\Google\Chrome\User Data\Local State"),@('Edge',"$env:LOCALAPPDATA\Microsoft\Edge\User Data\Local State"),@('Yandex',"$env:LOCALAPPDATA\Yandex\YandexBrowser\User Data\Local State"),@('Opera',"$env:APPDATA\Opera Software\Opera Stable\Local State"))){if(Test-Path $b[1]){try{$j=Get-Content $b[1] -Raw|ConvertFrom-Json; if($j.dns_over_https.mode -eq 'secure'){$u+=('браузер '+$b[0]+': включён Безопасный DNS')}}catch{}}}
foreach($pf in Get-ChildItem "$env:APPDATA\Mozilla\Firefox\Profiles" -Directory -EA SilentlyContinue){$pj=Join-Path $pf.FullName 'prefs.js'; if((Test-Path $pj) -and ((Get-Content $pj -Raw -EA SilentlyContinue) -match 'network\.trr\.mode",\s*3')){$u+='браузер Firefox: включён строгий DoH'}}
if($u.Count){$u -join [Environment]::NewLine}
)PS";

    static const char *const kAdminPs = R"PS(
param([string]$Report)
$ErrorActionPreference='SilentlyContinue'
$log=@()
$t='winws|windivert|zapret|goodbyedpi|byedpi|ciadpi|proxifyre|spoofdpi|powertunnel'
foreach($s in Get-CimInstance Win32_Service | Where-Object {$_.PathName -match $t -or $_.Name -match $t -or $_.DisplayName -match $t}){$log+=('служба: '+$s.Name); sc.exe stop $s.Name|Out-Null; sc.exe config $s.Name start= disabled|Out-Null; sc.exe delete $s.Name|Out-Null}
foreach($n in 'WinDivert','WinDivert1.4','WinDivert14'){if(Get-Service $n -EA SilentlyContinue){$log+=('драйвер: '+$n); sc.exe stop $n|Out-Null; sc.exe delete $n|Out-Null}}
foreach($d in Get-CimInstance Win32_SystemDriver | Where-Object {$_.PathName -match 'divert|zapret|winws'}){$log+=('драйвер: '+$d.Name); sc.exe stop $d.Name|Out-Null; sc.exe delete $d.Name|Out-Null}
foreach($s in Get-Service | Where-Object {$_.Name -match 'warp|cloudflare|outline|amnezia' -or $_.DisplayName -match 'warp|cloudflare|outline|amnezia'}){$log+=('служба: '+$s.Name); Stop-Service $s.Name -Force; Set-Service $s.Name -StartupType Disabled}
foreach($p in Get-Process | Where-Object {$_.ProcessName -match 'winws|goodbyedpi|zapret|byedpi|ciadpi|proxifyre|bdmanager|spoofdpi|powertunnel|warp-svc'}){$log+=('процесс: '+$p.ProcessName); Stop-Process -Id $p.Id -Force}
foreach($tk in Get-ScheduledTask | Where-Object {($_.Actions.Execute -match $t) -or ($_.Actions.Arguments -match $t)}){$log+=('задание: '+$tk.TaskName); $tk | Unregister-ScheduledTask -Confirm:$false}
$runkeys=@('HKLM:\Software\Microsoft\Windows\CurrentVersion\Run','HKLM:\Software\Wow6432Node\Microsoft\Windows\CurrentVersion\Run')
foreach($sid in (Get-ChildItem 'Registry::HKEY_USERS' | Where-Object {$_.PSChildName -match '^S-1-5-21-' -and $_.PSChildName -notmatch '_Classes$'})){$runkeys+="Registry::HKEY_USERS\$($sid.PSChildName)\Software\Microsoft\Windows\CurrentVersion\Run"}
foreach($rk in $runkeys){if(Test-Path $rk){$ip=Get-ItemProperty $rk; foreach($n in (Get-Item $rk).Property){$v=[string]$ip.$n; if($v -match $t -and $v -notmatch 'greenrhythm'){$log+=('автозапуск: '+$n); Remove-ItemProperty $rk $n}}}}
$cs=[Environment]::GetFolderPath('CommonStartup')
if($cs -and (Test-Path $cs)){foreach($f in Get-ChildItem $cs -File){$c=''; if($f.Extension -match '\.(bat|cmd|ps1)$'){$c=Get-Content $f.FullName -Raw} elseif($f.Extension -eq '.lnk'){try{$sh=New-Object -ComObject WScript.Shell; $l=$sh.CreateShortcut($f.FullName); $c=$l.TargetPath+' '+$l.Arguments}catch{}}; if($c -match $t){$log+=('автозапуск: '+$f.Name); Remove-Item $f.FullName -Force}}}
$defIdx=@(Get-NetRoute -DestinationPrefix '0.0.0.0/0' -EA SilentlyContinue | Sort-Object RouteMetric | Select-Object -ExpandProperty InterfaceIndex)
foreach($a in Get-NetAdapter | Where-Object {$_.InterfaceDescription -match 'TAP|TUN|WireGuard|Wintun|WARP|Outline' -and $_.InterfaceDescription -notmatch 'sing-tun' -and $_.Status -ne 'Disabled' -and -not $_.HardwareInterface -and $_.ifIndex -notin $defIdx}){
Disable-NetAdapter -InputObject $a -Confirm:$false
if((Get-NetAdapter -InterfaceIndex $a.ifIndex -EA SilentlyContinue).Status -eq 'Disabled'){$log+=('адаптер отключён: '+$a.Name+' ['+$a.InterfaceDescription+']')}else{$log+=('НЕ удалось отключить адаптер: '+$a.Name+' ['+$a.InterfaceDescription+']')}}
foreach($i in Get-DnsClientServerAddress | Where-Object {$_.ServerAddresses -match '^127\.|^::1$|^fd01:db8:1111'}){$dead=@($i.ServerAddresses | Where-Object {$_ -match '^127\.|^::1$|^fd01:db8:1111'} | Where-Object { -not (Resolve-DnsName -Name 'dns.msftncsi.com' -Server $_ -DnsOnly -QuickTimeout -EA SilentlyContinue) }); if($dead){$log+=('мёртвый DNS '+($dead -join ',')+' сброшен: '+$i.InterfaceAlias); Set-DnsClientServerAddress -InterfaceIndex $i.InterfaceIndex -ResetServerAddresses}}
$wf=Join-Path $env:TEMP 'gr_wfp.xml'; netsh wfp show state file="$wf"|Out-Null
if(Test-Path $wf){try{$w=[xml](Get-Content $wf -Raw); $nm=@($w.wfpstate.providers.item|ForEach-Object{$_.displayData.name})+@($w.wfpstate.subLayers.item|ForEach-Object{$_.displayData.name}); foreach($x in ($nm|Where-Object{$_}|Sort-Object -Unique)){if($x -notmatch 'Microsoft|Windows|MPSSVC|NetIo|FWPM|Teredo|IPsec|WSH|sing-?(box|tun)|Hyper-V|WNV|WSL|Built-in'){$log+=('сетевой фильтр (НЕ удалён): '+$x)}}}catch{}; Remove-Item $wf -Force}
if(Get-CimInstance Win32_SystemDriver | Where-Object {$_.Name -eq 'ndisrd' -and $_.State -eq 'Running'}){$log+='драйвер ndisrd (ProxiFyre/WireSock, НЕ удалён)'}
Set-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' ProxyEnable 0 -EA SilentlyContinue
Remove-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' AutoConfigURL -EA SilentlyContinue
netsh winhttp reset proxy|Out-Null
netsh winsock reset|Out-Null
$phys=Get-NetAdapter -Physical | Where-Object {$_.Status -eq 'Up'} | Select-Object -ExpandProperty ifIndex
$sp=Get-NetIPInterface -AddressFamily IPv4 | Where-Object {$_.Dhcp -eq 'Disabled' -and $_.ConnectionState -eq 'Connected' -and $phys -contains $_.InterfaceIndex}
if($sp){$log+='статический IP — глубокий сброс IP пропущен'}else{netsh int ip reset|Out-Null; netsh int ipv6 reset|Out-Null}
ipconfig /flushdns|Out-Null
$out = if($log.Count){$log -join [Environment]::NewLine}else{'NOTHING'}
Set-Content -LiteralPath $Report -Value $out -Encoding UTF8
)PS";

    const QString dir = QDir::tempPath();
    const QString userPs = dir + "/gr_fixnet_user.ps1";
    const QString adminPs = dir + "/gr_fixnet_admin.ps1";
    const QString report = dir + "/gr_fixnet.txt";
    QFile::remove(report);

    auto writePs = [](const QString &path, const char *body) {
        QFile pf(path);
        if (!pf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        pf.write("\xEF\xBB\xBF"); // UTF-8 BOM: PowerShell 5.1 reads .ps1 as ANSI otherwise
        pf.write(QByteArray(body));
        pf.close();
        return true;
    };
    if (!writePs(userPs, kUserPs) || !writePs(adminPs, kAdminPs)) {
        QMessageBox::warning(this, tr("Починить сеть Windows"),
                             tr("Не удалось подготовить очистку (нет доступа к временной папке)."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Customer-context pass: runs as the logged-in user, so its HKCU proxy and AppData
    // browser paths are the right ones. Its stdout is the report, no file needed.
    QProcess up;
    up.start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", userPs});
    up.waitForFinished(60000);
    const QString userOut = QString::fromUtf8(up.readAllStandardOutput()).trimmed();

    // Elevated pass: the destructive work. Paths are quoted inside a single -ArgumentList
    // string so a user name with spaces cannot split them.
    const QString launcher =
        QString("Start-Process powershell -Verb RunAs -WindowStyle Hidden -Wait "
                "-ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%1\" -Report \"%2\"'")
            .arg(QDir::toNativeSeparators(adminPs), QDir::toNativeSeparators(report));

    QProcess proc;
    proc.start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", launcher});
    if (!proc.waitForFinished(240000)) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, tr("Починить сеть Windows"),
                             tr("Очистка не завершилась вовремя. Попробуйте ещё раз."));
        return;
    }

    QString adminOut;
    QFile rf(report);
    if (rf.open(QIODevice::ReadOnly)) {
        adminOut = QString::fromUtf8(rf.readAll());
        rf.close();
        QFile::remove(report);
    }
    QFile::remove(userPs);
    QFile::remove(adminPs);
    QApplication::restoreOverrideCursor();

    // Set-Content -Encoding UTF8 prepends a BOM; strip it so 'NOTHING' and isEmpty() work.
    if (!adminOut.isEmpty() && adminOut.front() == QChar(0xFEFF)) adminOut.remove(0, 1);
    adminOut = adminOut.trimmed();

    if (adminOut.isEmpty()) {
        // The elevated pass always writes at least 'NOTHING', so an empty report means it
        // never ran — almost always a declined UAC prompt, a choice, not an error.
        QMessageBox::warning(this, tr("Починить сеть Windows"),
                             tr("Очистка не выполнена — не были выданы права администратора.\n\n"
                                "Попробуйте ещё раз и подтвердите запрос Windows."));
        return;
    }

    // Combine both passes. "Nothing at all" only when neither pass did anything.
    const bool adminNothing = adminOut.startsWith("NOTHING");
    QStringList parts;
    if (!adminNothing) parts << adminOut;
    if (!userOut.isEmpty()) parts << userOut;
    const QString found = parts.join("\n");
    const bool nothingAll = found.isEmpty();

    // Advisories for the report-only findings the cleaner deliberately does not touch.
    QString extra;
    if (found.contains(QString::fromUtf8("Безопасный DNS")) || found.contains("DoH")) {
        extra += QString::fromUtf8(
            "\n\nВ браузере включён Безопасный DNS. Если сайты не открываются только в браузере — "
            "отключите его: Настройки → Конфиденциальность и безопасность → «Использовать безопасный "
            "DNS-сервер» выключить.");
    }
    if (found.contains(QString::fromUtf8("сетевой фильтр")) || found.contains("ndisrd")) {
        extra += QString::fromUtf8(
            "\n\nНайдены сетевые фильтры сторонних программ — мы их не удаляем. Если после "
            "перезагрузки интернета нет совсем, откройте эту программу (WARP, AmneziaVPN и т.п.), "
            "отключите в ней Kill Switch / «постоянную защиту» и удалите её штатным деинсталлятором.");
    }

    QString body =
        nothingAll
            ? tr("Посторонних программ не найдено.\n\n"
                 "Сетевые настройки сброшены. Если подключение всё равно "
                 "не работает, перезагрузите компьютер и напишите в поддержку.")
            // Adapters are only disabled, never removed, and we say so:
            // someone who needs a work VPN should not think we deleted it.
            : tr("Сделано:\n\n%1\n\nСетевые настройки сброшены.\n\n"
                 "Чтобы изменения вступили в силу, нужна перезагрузка: "
                 "драйверы-перехватчики остаются в памяти до неё.\n\n"
                 "Адаптеры только отключены, не удалены. Если какой-то из них "
                 "нужен для работы, включите его обратно в «Сетевые подключения» "
                 "(Win+R → ncpa.cpl → правой кнопкой → Включить).")
                  .arg(found);
    body += extra;
    QMessageBox done(QMessageBox::Information, tr("Починить сеть Windows"), body,
                     QMessageBox::NoButton, this);
    auto *reboot = done.addButton(tr("Перезагрузить сейчас"), QMessageBox::AcceptRole);
    done.addButton(tr("Позже"), QMessageBox::RejectRole);
    done.exec();
    if (done.clickedButton() == reboot) {
        // /t 8, not /t 0: we just asked them to trust us with their network stack;
        // killing whatever they have open without warning would be a poor thanks.
        QProcess::startDetached("shutdown", {"/r", "/t", "8"});
    }
#endif
}

// «Автопилот» watchdog. Probes the RUNNING tunnel end-to-end (HTTP 204 through the
// local mixed inbound — not a bare server ping) and self-heals on repeated failure,
// encoding the real-world failure modes: panel key rotation → refresh the subscription
// and reconnect; DPI/server death → switch to the best other server; then back off
// with a tray hint at Диагностика. No telemetry; everything stays local.
void MainWindow::autopilot_tick() {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!NekoGui::dataStore->connection_autopilot || NekoGui::dataStore->prepare_exit ||
        running == nullptr || autopilot_probing || now < autopilot_cooldown_until) {
        autopilot_fails = 0;
        if (now >= autopilot_cooldown_until) autopilot_stage = 0;
        autopilot_timer->start(60 * 1000);
        return;
    }
    autopilot_probing = true;
    const auto proxyAddr = NekoGui::dataStore->inbound_address;
    const auto proxyPort = quint16(NekoGui::dataStore->inbound_socks_port);
    runOnNewThread([=] {
        QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, proxyAddr, proxyPort);
        QNetworkAccessManager nam;
        nam.setProxy(proxy);
        auto *reply = nam.get(QNetworkRequest(QUrl(QStringLiteral("http://cp.cloudflare.com/generate_204"))));
        QEventLoop loop;
        QTimer::singleShot(6000, reply, &QNetworkReply::abort);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        runOnUiThread([=] {
            autopilot_probing = false;
            if (running == nullptr) { // stopped while probing
                autopilot_fails = 0;
                conn_health = Health_Unknown;
                autopilot_timer->start(60 * 1000);
                return;
            }
            // Surface the probe result in the status pill regardless of whether we act on it.
            conn_health = ok ? Health_Ok : Health_NoTraffic;
            refresh_status();
            if (ok) {
                autopilot_fails = 0;
                autopilot_stage = 0;
                autopilot_timer->start(60 * 1000);
                return;
            }
            autopilot_fails++;
            MW_show_log(tr("Автопилот: проверка соединения не прошла (%1/2).").arg(autopilot_fails));
            if (autopilot_fails >= 2) {
                autopilot_fails = 0;
                autopilot_recover();
                autopilot_timer->start(30 * 1000); // re-check the fix soon
            } else {
                autopilot_timer->start(15 * 1000); // quick confirm before acting
            }
        });
    });
}

void MainWindow::autopilot_recover() {
    auto ent = running;
    if (ent == nullptr) return;
    const int gid = ent->gid;
    const int curId = ent->id;
    auto group = NekoGui::profileManager->GetGroup(gid);
    autopilot_stage++;

    const auto giveUp = [this] {
        MW_show_log(tr("Автопилот: восстановить не удалось — запустите «Диагностику соединения» в меню «Зелёный Ритм»."));
        if (tray != nullptr)
            tray->showMessage(GreenRhythm::kServiceName,
                              tr("Не удалось восстановить соединение — откройте «Диагностику соединения»."),
                              QSystemTrayIcon::Warning);
        autopilot_cooldown_until = QDateTime::currentMSecsSinceEpoch() + 5 * 60 * 1000;
        autopilot_stage = 0;
    };

    if (autopilot_stage >= 3) {
        giveUp();
        return;
    }

    if (autopilot_stage == 1 && group != nullptr && !group->url.isEmpty()) {
        // Stage 1 — keys may have rotated on the panel: refresh the subscription,
        // then reconnect to the best profile in the group (ids may change on update).
        MW_show_log(tr("Автопилот: обновляю подписку и переподключаюсь…"));
        if (tray != nullptr)
            tray->showMessage(GreenRhythm::kServiceName, tr("Соединение потеряно — восстанавливаю…"));
        NekoGui_sub::groupUpdater->AsyncUpdate(group->url, gid, [this, gid] {
            runOnUiThread([this, gid] {
                auto g = NekoGui::profileManager->GetGroup(gid);
                if (g == nullptr) return;
                auto ps = g->Profiles();
                if (ps.isEmpty()) return;
                std::shared_ptr<NekoGui::ProxyEntity> best;
                for (const auto &p: ps)
                    if (p->latency > 0 && (best == nullptr || p->latency < best->latency)) best = p;
                if (best == nullptr) best = ps.first();
                neko_start(best->id);
            });
        });
        return;
    }

    // Stage 2 (or stage 1 without a subscription) — switch to the best OTHER server.
    auto ps = group != nullptr ? group->Profiles() : QList<std::shared_ptr<NekoGui::ProxyEntity>>{};
    std::shared_ptr<NekoGui::ProxyEntity> next;
    for (const auto &p: ps) {
        if (p->id == curId) continue;
        if (next == nullptr) next = p;
        else if (p->latency > 0 && (next->latency <= 0 || p->latency < next->latency)) next = p;
    }
    if (next != nullptr) {
        MW_show_log(tr("Автопилот: сервер недоступен, переключаюсь на «%1»…").arg(next->bean->DisplayName()));
        if (tray != nullptr)
            tray->showMessage(GreenRhythm::kServiceName, tr("Переключаюсь на %1").arg(next->bean->DisplayName()));
        neko_start(next->id);
        return;
    }
    giveUp();
}

// «Зелёный Ритм» subscription badge in the bottom status row: days + traffic left,
// parsed from the Subscription-UserInfo the server already sends (no extra request,
// no telemetry). Green normally; amber + a «Продлить» link when nearly out. Only
// shows for the brand's own subscription group — other services are untouched.
void MainWindow::refresh_subscription_status() {
    if (ui->label_sub_status == nullptr) return;

    std::shared_ptr<NekoGui::Group> gr;
    {
        QMutexLocker locker(&NekoGui::profileManager->mutex);
        for (const auto &[gid, g]: NekoGui::profileManager->groups) {
            if (g == nullptr || g->url.isEmpty()) continue;
            if (g->name == GreenRhythm::kServiceName || g->url.contains(QStringLiteral("verdantvibe"), Qt::CaseInsensitive)) {
                gr = g;
                break;
            }
        }
    }
    if (gr == nullptr || gr->info.trimmed().isEmpty()) {
        ui->label_sub_status->clear();
        ui->label_sub_status->setVisible(false);
        return;
    }

    auto grab = [&](const QString &key) -> long long {
        auto m = QRegularExpression(key + "=([0-9]+)").match(gr->info);
        return m.hasMatch() ? m.captured(1).toLongLong() : -1;
    };
    const long long total = grab("total"), up = grab("upload"), down = grab("download"), expire = grab("expire");
    const long long used = (up < 0 ? 0 : up) + (down < 0 ? 0 : down);

    QStringList parts;
    bool low = false;
    if (expire > 0) {
        long long days = (expire - QDateTime::currentSecsSinceEpoch()) / 86400;
        if (days < 0) days = 0;
        parts << tr("%1 дн.").arg(days);
        if (days <= 3) low = true;
    }
    if (total > 0) {
        long long left = total - used;
        if (left < 0) left = 0;
        parts << ReadableSize(left);
        if (static_cast<double>(left) / total <= 0.10) low = true;
    }
    if (parts.isEmpty()) {
        ui->label_sub_status->clear();
        ui->label_sub_status->setVisible(false);
        return;
    }

    const QString color = low ? QStringLiteral("#E3A008") : QStringLiteral("#3FB950");
    QString text = QStringLiteral("<span style='color:%1;'>%2 %3</span>")
                       .arg(color, QString::fromUtf8("\xF0\x9F\x8C\xBF"), parts.join(QStringLiteral(" \xC2\xB7 "))); // 🌿 ·
    if (low) {
        text += QStringLiteral(" <a href='%1' style='color:#E3A008;text-decoration:none;'>%2</a>")
                    .arg(GreenRhythm::kRenewUrl, tr("Продлить"));
    }
    ui->label_sub_status->setText(text);
    ui->label_sub_status->setToolTip(tr("Подписка «Зелёный Ритм»"));
    ui->label_sub_status->setVisible(true);
}

// Small round status dot for a profile row (green connected / red last-test-failed / grey idle).
static QIcon MakeStatusDot(const QColor &color) {
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(4, 4, 10, 10);
    return QIcon(pm);
}

void MainWindow::refresh_proxy_list_impl_refresh_data(const int &id) {
    // 绘制或更新item(s)
    for (int row = 0; row < ui->proxyListTable->rowCount(); row++) {
        auto profileId = ui->proxyListTable->row2Id[row];
        if (id >= 0 && profileId != id) continue; // refresh ONE item
        auto profile = NekoGui::profileManager->GetProfile(profileId);
        if (profile == nullptr) continue;

        auto isRunning = profileId == NekoGui::dataStore->started_id;
        auto f0 = std::make_unique<QTableWidgetItem>();
        f0->setData(114514, profileId);

        // Check state
        auto check = f0->clone();
        check->setText(isRunning ? "✓" : Int2String(row + 1));
        ui->proxyListTable->setVerticalHeaderItem(row, check);

        // C0: Type (+ status dot: green connected / red last-test-failed / grey idle)
        auto f = f0->clone();
        f->setText(profile->bean->DisplayType());
        if (isRunning) f->setForeground(palette().link());
        f->setIcon(MakeStatusDot(isRunning ? QColor(0x3F, 0xB9, 0x50)
                                           : (profile->latency < 0 ? QColor(0xE5, 0x48, 0x4D)
                                                                   : QColor(0x5A, 0x5F, 0x66))));
        ui->proxyListTable->setItem(row, 0, f);

        // C1: Address+Port
        f = f0->clone();
        f->setText(profile->bean->DisplayAddress());
        if (isRunning) f->setForeground(palette().link());
        ui->proxyListTable->setItem(row, 1, f);

        // C2: Name
        f = f0->clone();
        f->setText(profile->bean->name);
        if (isRunning) f->setForeground(palette().link());
        ui->proxyListTable->setItem(row, 2, f);

        // C3: Test Result
        f = f0->clone();
        if (profile->full_test_report.isEmpty()) {
            auto color = profile->DisplayLatencyColor();
            if (color.isValid()) f->setForeground(color);
            f->setText(profile->DisplayLatency());
        } else {
            f->setText(profile->full_test_report);
            auto color = profile->DisplayLatencyColor();
            if (color.isValid()) f->setForeground(color);
        }
        ui->proxyListTable->setItem(row, 3, f);

        // C4: Traffic
        f = f0->clone();
        f->setText(profile->traffic_data->DisplayTraffic());
        ui->proxyListTable->setItem(row, 4, f);
    }
}

// table菜单相关

void MainWindow::on_proxyListTable_itemDoubleClicked(QTableWidgetItem *item) {
    auto id = item->data(114514).toInt();
    if (select_mode) {
        emit profile_selected(id);
        select_mode = false;
        refresh_status();
        return;
    }
    auto dialog = new DialogEditProfile("", id, this);
    connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);
}

void MainWindow::on_menu_add_from_input_triggered() {
    auto dialog = new DialogEditProfile("socks", NekoGui::dataStore->current_group, this);
    connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);
}

void MainWindow::on_menu_add_from_clipboard_triggered() {
    auto clipboard = QApplication::clipboard()->text();
    NekoGui_sub::groupUpdater->AsyncUpdate(clipboard);
}

void MainWindow::on_menu_clone_triggered() {
    auto ents = get_now_selected_list();
    if (ents.isEmpty()) return;

    auto btn = QMessageBox::question(this, tr("Clone"), tr("Clone %1 item(s)").arg(ents.count()));
    if (btn != QMessageBox::Yes) return;

    QStringList sls;
    for (const auto &ent: ents) {
        sls << ent->bean->ToNekorayShareLink(ent->type);
    }

    NekoGui_sub::groupUpdater->AsyncUpdate(sls.join("\n"));
}

void MainWindow::on_menu_move_triggered() {
    auto ents = get_now_selected_list();
    if (ents.isEmpty()) return;

    auto items = QStringList{};
    for (auto gid: NekoGui::profileManager->groupsTabOrder) {
        auto group = NekoGui::profileManager->GetGroup(gid);
        if (group == nullptr) continue;
        items += Int2String(gid) + " " + group->name;
    }

    bool ok;
    auto a = QInputDialog::getItem(nullptr,
                                   tr("Move"),
                                   tr("Move %1 item(s)").arg(ents.count()),
                                   items, 0, false, &ok);
    if (!ok) return;
    auto gid = SubStrBefore(a, " ").toInt();
    for (const auto &ent: ents) {
        NekoGui::profileManager->MoveProfile(ent, gid);
    }
    refresh_proxy_list();
}

void MainWindow::on_menu_delete_triggered() {
    auto ents = get_now_selected_list();
    if (ents.count() == 0) return;
    if (QMessageBox::question(this, tr("Confirmation"), QString(tr("Remove %1 item(s) ?")).arg(ents.count())) ==
        QMessageBox::StandardButton::Yes) {
        for (const auto &ent: ents) {
            NekoGui::profileManager->DeleteProfile(ent->id);
        }
        refresh_proxy_list();
    }
}

void MainWindow::on_menu_reset_traffic_triggered() {
    auto ents = get_now_selected_list();
    if (ents.count() == 0) return;
    for (const auto &ent: ents) {
        ent->traffic_data->Reset();
        ent->Save();
        refresh_proxy_list(ent->id);
    }
}

void MainWindow::on_menu_profile_debug_info_triggered() {
    auto ents = get_now_selected_list();
    if (ents.count() != 1) return;
    auto btn = QMessageBox::information(this, software_name, ents.first()->ToJsonBytes(), "OK", "Edit", "Reload", 0, 0);
    if (btn == 1) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(QStringLiteral("profiles/%1.json").arg(ents.first()->id)).absoluteFilePath()));
    } else if (btn == 2) {
        NekoGui::dataStore->Load();
        NekoGui::profileManager->LoadManager();
        refresh_proxy_list();
    }
}

void MainWindow::on_menu_copy_links_triggered() {
    if (ui->masterLogBrowser->hasFocus()) {
        ui->masterLogBrowser->copy();
        return;
    }
    auto ents = get_now_selected_list();
    QStringList links;
    for (const auto &ent: ents) {
        links += ent->bean->ToShareLink();
    }
    if (links.length() == 0) return;
    QApplication::clipboard()->setText(links.join("\n"));
    show_log_impl(tr("Copied %1 item(s)").arg(links.length()));
}

void MainWindow::on_menu_copy_links_nkr_triggered() {
    auto ents = get_now_selected_list();
    QStringList links;
    for (const auto &ent: ents) {
        links += ent->bean->ToNekorayShareLink(ent->type);
    }
    if (links.length() == 0) return;
    QApplication::clipboard()->setText(links.join("\n"));
    show_log_impl(tr("Copied %1 item(s)").arg(links.length()));
}

void MainWindow::on_menu_export_config_triggered() {
    auto ents = get_now_selected_list();
    if (ents.count() != 1) return;
    auto ent = ents.first();
    if (ent->bean->DisplayCoreType() != software_core_name) return;

    auto result = BuildConfig(ent, false, true);
    QString config_core = QJsonObject2QString(result->coreConfig, false);
    QApplication::clipboard()->setText(config_core);

    QMessageBox msg(QMessageBox::Information, tr("Config copied"), tr("Config copied"));
    msg.addButton("Copy core config", QMessageBox::YesRole);
    msg.addButton("Copy test config", QMessageBox::NoRole);
    msg.addButton(QMessageBox::Ok);
    msg.setEscapeButton(QMessageBox::Ok);
    msg.setDefaultButton(QMessageBox::Ok);
    auto ret = msg.exec();
    if (ret == 2) {
        result = BuildConfig(ent, false, false);
        config_core = QJsonObject2QString(result->coreConfig, false);
        QApplication::clipboard()->setText(config_core);
    } else if (ret == 3) {
        result = BuildConfig(ent, true, false);
        config_core = QJsonObject2QString(result->coreConfig, false);
        QApplication::clipboard()->setText(config_core);
    }
}

void MainWindow::display_qr_link(bool nkrFormat) {
    auto ents = get_now_selected_list();
    if (ents.count() != 1) return;

    class W : public QDialog {
    public:
        QLabel *l = nullptr;
        QCheckBox *cb = nullptr;
        //
        QPlainTextEdit *l2 = nullptr;
        QImage im;
        //
        QString link;
        QString link_nk;

        void show_qr(const QSize &size) const {
            auto side = size.height() - 20 - l2->size().height() - cb->size().height();
            l->setPixmap(QPixmap::fromImage(im.scaled(side, side, Qt::KeepAspectRatio, Qt::FastTransformation),
                                            Qt::MonoOnly));
            l->resize(side, side);
        }

        void refresh(bool is_nk) {
            auto link_display = is_nk ? link_nk : link;
            l2->setPlainText(link_display);
            constexpr qint32 qr_padding = 2;
            //
            try {
                qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(link_display.toUtf8().data(), qrcodegen::QrCode::Ecc::MEDIUM);
                qint32 sz = qr.getSize();
                im = QImage(sz + qr_padding * 2, sz + qr_padding * 2, QImage::Format_RGB32);
                QRgb black = qRgb(0, 0, 0);
                QRgb white = qRgb(255, 255, 255);
                im.fill(white);
                for (int y = 0; y < sz; y++)
                    for (int x = 0; x < sz; x++)
                        if (qr.getModule(x, y))
                            im.setPixel(x + qr_padding, y + qr_padding, black);
                show_qr(size());
            } catch (const std::exception &ex) {
                QMessageBox::warning(nullptr, "error", ex.what());
            }
        }

        W(const QString &link_, const QString &link_nk_) {
            link = link_;
            link_nk = link_nk_;
            //
            setLayout(new QVBoxLayout);
            setMinimumSize(256, 256);
            QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            sizePolicy.setHeightForWidth(true);
            setSizePolicy(sizePolicy);
            //
            l = new QLabel();
            l->setMinimumSize(256, 256);
            l->setMargin(6);
            l->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            l->setScaledContents(true);
            layout()->addWidget(l);
            cb = new QCheckBox;
            cb->setText("GreenRhythm Links");
            layout()->addWidget(cb);
            l2 = new QPlainTextEdit();
            l2->setReadOnly(true);
            layout()->addWidget(l2);
            //
            connect(cb, &QCheckBox::toggled, this, &W::refresh);
            refresh(false);
        }

        void resizeEvent(QResizeEvent *resizeEvent) override {
            show_qr(resizeEvent->size());
        }
    };

    auto link = ents.first()->bean->ToShareLink();
    auto link_nk = ents.first()->bean->ToNekorayShareLink(ents.first()->type);
    auto w = new W(link, link_nk);
    w->setWindowTitle(ents.first()->bean->DisplayTypeAndName());
    w->exec();
    w->deleteLater();
}

void MainWindow::on_menu_scan_qr_triggered() {
#ifndef NKR_NO_ZXING
    using namespace ZXingQt;

    hide();
    QThread::sleep(1);

    auto screen = QGuiApplication::primaryScreen();
    auto geom = screen->geometry();
    auto qpx = screen->grabWindow(0, geom.x(), geom.y(), geom.width(), geom.height());

    show();

    auto hints = DecodeHints()
                     .setFormats(BarcodeFormat::QRCode)
                     .setTryRotate(false)
                     .setBinarizer(Binarizer::FixedThreshold);

    auto result = ReadBarcode(qpx.toImage(), hints);
    const auto &text = result.text();
    if (text.isEmpty()) {
        MessageBoxInfo(software_name, tr("QR Code not found"));
    } else {
        show_log_impl("QR Code Result:\n" + text);
        NekoGui_sub::groupUpdater->AsyncUpdate(text);
    }
#endif
}

void MainWindow::on_menu_clear_test_result_triggered() {
    for (const auto &profile: get_selected_or_group()) {
        profile->latency = 0;
        profile->full_test_report = "";
        profile->Save();
    }
    refresh_proxy_list();
}

void MainWindow::on_menu_select_all_triggered() {
    if (ui->masterLogBrowser->hasFocus()) {
        ui->masterLogBrowser->selectAll();
        return;
    }
    ui->proxyListTable->selectAll();
}

void MainWindow::on_menu_delete_repeat_triggered() {
    QList<std::shared_ptr<NekoGui::ProxyEntity>> out;
    QList<std::shared_ptr<NekoGui::ProxyEntity>> out_del;

    NekoGui::ProfileFilter::Uniq(NekoGui::profileManager->CurrentGroup()->Profiles(), out, true, false);
    NekoGui::ProfileFilter::OnlyInSrc_ByPointer(NekoGui::profileManager->CurrentGroup()->Profiles(), out, out_del);

    int remove_display_count = 0;
    QString remove_display;
    for (const auto &ent: out_del) {
        remove_display += ent->bean->DisplayTypeAndName() + "\n";
        if (++remove_display_count == 20) {
            remove_display += "...";
            break;
        }
    }

    if (out_del.length() > 0 &&
        QMessageBox::question(this, tr("Confirmation"), tr("Remove %1 item(s) ?").arg(out_del.length()) + "\n" + remove_display) == QMessageBox::StandardButton::Yes) {
        for (const auto &ent: out_del) {
            NekoGui::profileManager->DeleteProfile(ent->id);
        }
        refresh_proxy_list();
    }
}

bool mw_sub_updating = false;

void MainWindow::on_menu_update_subscription_triggered() {
    auto group = NekoGui::profileManager->CurrentGroup();
    if (group->url.isEmpty()) return;
    if (mw_sub_updating) return;
    mw_sub_updating = true;
    NekoGui_sub::groupUpdater->AsyncUpdate(group->url, group->id, [&] { mw_sub_updating = false; });
}

void MainWindow::on_menu_remove_unavailable_triggered() {
    QList<std::shared_ptr<NekoGui::ProxyEntity>> out_del;

    for (const auto &[_, profile]: NekoGui::profileManager->profiles) {
        if (NekoGui::dataStore->current_group != profile->gid) continue;
        if (profile->latency < 0) out_del += profile;
    }

    int remove_display_count = 0;
    QString remove_display;
    for (const auto &ent: out_del) {
        remove_display += ent->bean->DisplayTypeAndName() + "\n";
        if (++remove_display_count == 20) {
            remove_display += "...";
            break;
        }
    }

    if (out_del.length() > 0 &&
        QMessageBox::question(this, tr("Confirmation"), tr("Remove %1 item(s) ?").arg(out_del.length()) + "\n" + remove_display) == QMessageBox::StandardButton::Yes) {
        for (const auto &ent: out_del) {
            NekoGui::profileManager->DeleteProfile(ent->id);
        }
        refresh_proxy_list();
    }
}

void MainWindow::on_menu_resolve_domain_triggered() {
    auto profiles = get_selected_or_group();
    if (profiles.isEmpty()) return;

    if (QMessageBox::question(this,
                              tr("Confirmation"),
                              tr("Resolving domain to IP, if support.")) != QMessageBox::StandardButton::Yes) {
        return;
    }
    if (mw_sub_updating) return;
    mw_sub_updating = true;
    NekoGui::dataStore->resolve_count = profiles.count();

    for (const auto &profile: profiles) {
        profile->bean->ResolveDomainToIP([=] {
            profile->Save();
            if (--NekoGui::dataStore->resolve_count != 0) return;
            refresh_proxy_list();
            mw_sub_updating = false;
        });
    }
}

void MainWindow::on_proxyListTable_customContextMenuRequested(const QPoint &pos) {
    ui->menu_server->popup(ui->proxyListTable->viewport()->mapToGlobal(pos)); // 弹出菜单
}

QList<std::shared_ptr<NekoGui::ProxyEntity>> MainWindow::get_now_selected_list() {
    auto items = ui->proxyListTable->selectedItems();
    QList<std::shared_ptr<NekoGui::ProxyEntity>> list;
    for (auto item: items) {
        auto id = item->data(114514).toInt();
        auto ent = NekoGui::profileManager->GetProfile(id);
        if (ent != nullptr && !list.contains(ent)) list += ent;
    }
    return list;
}

QList<std::shared_ptr<NekoGui::ProxyEntity>> MainWindow::get_selected_or_group() {
    auto selected_or_group = ui->menu_server->property("selected_or_group").toInt();
    QList<std::shared_ptr<NekoGui::ProxyEntity>> profiles;
    if (selected_or_group > 0) {
        profiles = get_now_selected_list();
        if (profiles.isEmpty() && selected_or_group == 2) profiles = NekoGui::profileManager->CurrentGroup()->ProfilesWithOrder();
    } else {
        profiles = NekoGui::profileManager->CurrentGroup()->ProfilesWithOrder();
    }
    return profiles;
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
        case Qt::Key_Escape:
            // take over by shortcut_esc
            break;
        case Qt::Key_Enter:
            neko_start();
            break;
        default:
            QMainWindow::keyPressEvent(event);
    }
}

// Log

inline void FastAppendTextDocument(const QString &message, QTextDocument *doc) {
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    cursor.insertBlock();
    cursor.insertText(message);
    cursor.endEditBlock();
}

void MainWindow::show_about_greenrhythm() {
    auto title = tr("<b>Клиент сервиса «%1»</b>").arg(GreenRhythm::kServiceName);
    auto body = tr("Версия: %1<br><br>"
                   "Сайт: <a href=\"%2\">%2</a><br>"
                   "Поддержка: <a href=\"%3\">%4</a>")
                    .arg(QString(NKR_VERSION))
                    .arg(GreenRhythm::kSiteUrl)
                    .arg(GreenRhythm::kTelegramUrl, GreenRhythm::kTelegramHandle);
    QMessageBox box(QMessageBox::Information, tr("О программе"), title, QMessageBox::Ok, this);
    box.setTextFormat(Qt::RichText);
    box.setInformativeText(body);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.exec();
}

void MainWindow::show_log_impl(const QString &log) {
    auto lines = SplitLines(log.trimmed());
    if (lines.isEmpty()) return;

    // Strip ANSI colour/escape sequences the sing-box / xray cores emit on stderr —
    // otherwise raw «\x1b[36mINFO\x1b[0m» garbage leaks into the log view.
    static const QRegularExpression ansiRe(QStringLiteral("\x1B\\[[0-9;]*[A-Za-z]"));
    QStringList newLines;
    auto log_ignore = NekoGui::dataStore->log_ignore;
    for (const auto &rawLine: lines) {
        QString line = QString(rawLine).remove(ansiRe);
        bool showThisLine = true;
        for (const auto &str: log_ignore) {
            if (line.contains(str)) {
                showThisLine = false;
                break;
            }
        }
        if (showThisLine) newLines << line;
    }
    if (newLines.isEmpty()) return;

    // Persist to disk before rendering: the on-screen buffer is capped at max_log_line and
    // gone on close, which is why support only ever got screenshots. The file keeps the
    // history a customer can attach in one click.
    append_log_to_file(newLines);

    // Colour-code so routing reads at a glance: [proxy] (foreign) blue,
    // [bypass] (domestic/direct) green, errors red, everything else default.
    {
        QTextCursor cursor(qvLogDocument);
        cursor.movePosition(QTextCursor::End);
        cursor.beginEditBlock();
        for (const auto &line: newLines) {
            cursor.insertBlock();
            QTextCharFormat fmt;
            if (line.contains("ERROR")) {
                fmt.setForeground(QColor(0xE5, 0x48, 0x4D));
            } else if (line.contains("[proxy]")) {
                fmt.setForeground(QColor(0x4C, 0x9A, 0xFF));
            } else if (line.contains("[bypass]")) {
                fmt.setForeground(QColor(0x3F, 0xB9, 0x50));
            }
            cursor.insertText(line, fmt);
        }
        cursor.endEditBlock();
    }
    // From https://gist.github.com/jemyzhang/7130092
    auto block = qvLogDocument->begin();

    while (block.isValid()) {
        if (qvLogDocument->blockCount() > NekoGui::dataStore->max_log_line) {
            QTextCursor cursor(block);
            block = block.next();
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            continue;
        }
        break;
    }
}

// Append filtered log lines to a rotating file next to the executable. Kept small (one
// live file + one .1 backup) — this is a support aid, not an audit trail.
void MainWindow::append_log_to_file(const QStringList &lines) {
    if (lines.isEmpty()) return;
    if (log_file_path.isEmpty()) {
        const QString dir = QCoreApplication::applicationDirPath() + "/logs";
        QDir().mkpath(dir);
        log_file_path = dir + "/greenrhythm.log";
    }
    constexpr qint64 kMaxBytes = 3 * 1024 * 1024;
    if (QFileInfo(log_file_path).size() > kMaxBytes) {
        const QString bak = log_file_path + ".1";
        QFile::remove(bak);
        QFile::rename(log_file_path, bak); // keep exactly one previous file
    }
    QFile f(log_file_path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    f.write((lines.join("\n") + "\n").toUtf8());
    f.close();
}

// A short, secret-free preamble for the copyable report and the saved log: what version,
// OS, server and mode — the facts support asks for first. Never the token or keys.
QString MainWindow::diagnostics_header() const {
    QString h = QStringLiteral("GreenRhythm %1\nOS: %2\n")
                    .arg(QString(NKR_VERSION), QSysInfo::prettyProductName());
    if (running != nullptr && running->bean != nullptr) {
        h += QStringLiteral("Server: %1:%2\n").arg(running->bean->serverAddress).arg(running->bean->serverPort);
    } else {
        h += QStringLiteral("Server: (не запущен)\n");
    }
    h += QStringLiteral("Mode: %1%2\n")
             .arg(NekoGui::dataStore->spmode_vpn ? "TUN " : "",
                  NekoGui::dataStore->spmode_system_proxy ? "SystemProxy" : (NekoGui::dataStore->spmode_vpn ? "" : "Proxy-only"));
    return h;
}

#define ADD_TO_CURRENT_ROUTE(a, b)                                                                   \
    NekoGui::dataStore->routing->a = (SplitLines(NekoGui::dataStore->routing->a) << (b)).join("\n"); \
    NekoGui::dataStore->routing->Save();

void MainWindow::on_masterLogBrowser_customContextMenuRequested(const QPoint &pos) {
    QMenu *menu = ui->masterLogBrowser->createStandardContextMenu();

    auto sep = new QAction(this);
    sep->setSeparator(true);
    menu->addAction(sep);

    auto action_add_ignore = new QAction(this);
    action_add_ignore->setText(tr("Set ignore keyword"));
    connect(action_add_ignore, &QAction::triggered, this, [=] {
        auto list = NekoGui::dataStore->log_ignore;
        auto newStr = ui->masterLogBrowser->textCursor().selectedText().trimmed();
        if (!newStr.isEmpty()) list << newStr;
        bool ok;
        newStr = QInputDialog::getMultiLineText(GetMessageBoxParent(), tr("Set ignore keyword"), tr("Set the following keywords to ignore?\nSplit by line."), list.join("\n"), &ok);
        if (ok) {
            NekoGui::dataStore->log_ignore = SplitLines(newStr);
            NekoGui::dataStore->Save();
        }
    });
    menu->addAction(action_add_ignore);

    auto action_add_route = new QAction(this);
    action_add_route->setText(tr("Save as route"));
    connect(action_add_route, &QAction::triggered, this, [=] {
        auto newStr = ui->masterLogBrowser->textCursor().selectedText().trimmed();
        if (newStr.isEmpty()) return;
        //
        bool ok;
        newStr = QInputDialog::getText(GetMessageBoxParent(), tr("Save as route"), tr("Edit"), {}, newStr, &ok).trimmed();
        if (!ok) return;
        if (newStr.isEmpty()) return;
        //
        auto select = IsIpAddress(newStr) ? 0 : 3;
        QStringList items = {"proxyIP", "bypassIP", "blockIP", "proxyDomain", "bypassDomain", "blockDomain"};
        auto item = QInputDialog::getItem(GetMessageBoxParent(), tr("Save as route"),
                                          tr("Save \"%1\" as a routing rule?").arg(newStr),
                                          items, select, false, &ok);
        if (ok) {
            auto index = items.indexOf(item);
            switch (index) {
                case 0:
                    ADD_TO_CURRENT_ROUTE(proxy_ip, newStr);
                    break;
                case 1:
                    ADD_TO_CURRENT_ROUTE(direct_ip, newStr);
                    break;
                case 2:
                    ADD_TO_CURRENT_ROUTE(block_ip, newStr);
                    break;
                case 3:
                    ADD_TO_CURRENT_ROUTE(proxy_domain, newStr);
                    break;
                case 4:
                    ADD_TO_CURRENT_ROUTE(direct_domain, newStr);
                    break;
                case 5:
                    ADD_TO_CURRENT_ROUTE(block_domain, newStr);
                    break;
                default:
                    break;
            }
            MW_dialog_message("", "UpdateDataStore,RouteChanged");
        }
    });
    menu->addAction(action_add_route);

    // «Сохранить лог…» — one attachment for support instead of a wall of screenshots.
    // Writes the secret-free header plus the full on-disk history (both rotated files).
    auto action_save_log = new QAction(this);
    action_save_log->setText(tr("Сохранить лог в файл…"));
    connect(action_save_log, &QAction::triggered, this, [=] {
        const QString suggested = QDir::homePath() + "/greenrhythm-log-" +
                                  QDateTime::currentDateTime().toString("yyyyMMdd-HHmm") + ".txt";
        const QString dst = QFileDialog::getSaveFileName(GetMessageBoxParent(), tr("Сохранить лог"), suggested,
                                                         tr("Текст (*.txt)"));
        if (dst.isEmpty()) return;
        QFile out(dst);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(GetMessageBoxParent(), tr("Сохранить лог"), tr("Не удалось записать файл."));
            return;
        }
        out.write(diagnostics_header().toUtf8());
        out.write("\n");
        // Prefer the on-disk history (full); fall back to the on-screen buffer if no file yet.
        bool wroteFile = false;
        for (const QString &src: {log_file_path + ".1", log_file_path}) {
            if (src.isEmpty()) continue;
            QFile in(src);
            if (in.open(QIODevice::ReadOnly)) {
                out.write(in.readAll());
                in.close();
                wroteFile = true;
            }
        }
        if (!wroteFile) out.write(qvLogDocument->toPlainText().toUtf8());
        out.close();
        QMessageBox box(QMessageBox::Information, tr("Сохранить лог"),
                        tr("Лог сохранён:\n%1\n\nПрикрепите этот файл в поддержку.").arg(dst),
                        QMessageBox::Ok, GetMessageBoxParent());
        auto *openBtn = box.addButton(tr("Показать папку"), QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == openBtn)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(dst).absolutePath()));
    });
    menu->addAction(action_save_log);

    auto action_clear = new QAction(this);
    action_clear->setText(tr("Clear"));
    connect(action_clear, &QAction::triggered, this, [=] {
        qvLogDocument->clear();
        ui->masterLogBrowser->clear();
    });
    menu->addAction(action_clear);

    menu->exec(ui->masterLogBrowser->viewport()->mapToGlobal(pos)); // 弹出菜单
}

// eventFilter

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto mouseEvent = dynamic_cast<QMouseEvent *>(event);
        if (obj == ui->label_running && mouseEvent->button() == Qt::LeftButton && running != nullptr) {
            speedtest_current();
            return true;
        } else if (obj == ui->label_inbound && mouseEvent->button() == Qt::LeftButton) {
            on_menu_basic_settings_triggered();
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonDblClick) {
        if (obj == ui->splitter) {
            auto size = ui->splitter->size();
            ui->splitter->setSizes({size.height() / 2, size.height() / 2});
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// profile selector

void MainWindow::start_select_mode(QObject *context, const std::function<void(int)> &callback) {
    select_mode = true;
    connectOnce(this, &MainWindow::profile_selected, context, callback);
    refresh_status();
}

// 连接列表

inline QJsonArray last_arr; // format is nekoray_connections_json

void MainWindow::refresh_connection_list(const QJsonArray &arr) {
    if (last_arr == arr) {
        return;
    }
    last_arr = arr;

    if (NekoGui::dataStore->flag_debug) qDebug() << arr;

    ui->tableWidget_conn->setRowCount(0);

    int nProxy = 0, nDirect = 0, nBlock = 0; // route-map tallies (active connections only)
    int row = -1;
    for (const auto &_item: arr) {
        auto item = _item.toObject();
        if (NekoGui::dataStore->ignoreConnTag.contains(item["Tag"].toString())) continue;

        // Count active (not-yet-ended) connections per outbound for the route map.
        if (item["End"].toInt() == 0) {
            const auto t = item["Tag"].toString();
            if (t == "proxy") nProxy++;
            else if (t == "direct" || t == "bypass") nDirect++;
            else if (t == "block") nBlock++;
        }

        row++;
        ui->tableWidget_conn->insertRow(row);

        auto f0 = std::make_unique<QTableWidgetItem>();
        f0->setData(114514, item["ID"].toInt());

        // C0: Status
        auto c0 = new QLabel;
        auto start_t = item["Start"].toInt();
        auto end_t = item["End"].toInt();
        // icon
        auto outboundTag = item["Tag"].toString();
        if (outboundTag == "block") {
            c0->setPixmap(Icon::GetMaterialIcon("cancel"));
        } else {
            if (end_t > 0) {
                c0->setPixmap(Icon::GetMaterialIcon("history"));
            } else {
                c0->setPixmap(Icon::GetMaterialIcon("swap-vertical"));
            }
        }
        c0->setAlignment(Qt::AlignCenter);
        c0->setToolTip(tr("Start: %1\nEnd: %2").arg(DisplayTime(start_t), end_t > 0 ? DisplayTime(end_t) : ""));
        ui->tableWidget_conn->setCellWidget(row, 0, c0);

        // C1: Outbound — humanised + colour-coded so RU/direct vs foreign/proxy is
        // obvious at a glance (helps curate routing rules). Raw tag kept in the tooltip.
        auto f = f0->clone();
        f->setToolTip(outboundTag);
        QString obLabel = outboundTag;
        QColor obColor;
        if (outboundTag == "proxy") {
            obLabel = QString::fromUtf8("\xF0\x9F\x8C\x8D ") + tr("Прокси"); // 🌍 foreign
            obColor = QColor(0x4C, 0x9A, 0xFF);
        } else if (outboundTag == "direct" || outboundTag == "bypass") {
            obLabel = QString::fromUtf8("\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA ") + tr("Напрямую"); // 🇷🇺 domestic
            obColor = QColor(0x3F, 0xB9, 0x50);
        } else if (outboundTag == "block") {
            obLabel = QString::fromUtf8("\xE2\x9B\x94 ") + tr("Блокировка"); // ⛔
            obColor = QColor(0xE5, 0x48, 0x4D);
        }
        f->setText(obLabel);
        if (obColor.isValid()) f->setForeground(QBrush(obColor));
        ui->tableWidget_conn->setItem(row, 1, f);

        // C2: Destination
        f = f0->clone();
        QString target1 = item["Dest"].toString();
        QString target2 = item["RDest"].toString();
        if (target2.isEmpty() || target1 == target2) {
            target2 = "";
        }
        f->setText("[" + target1 + "] " + target2);
        // Stash the bare host (no port) so the context menu can build a rule from it.
        // Prefer the resolved domain (RDest) over a bare IP when both exist.
        QString host = !target2.isEmpty() ? target2 : target1;
        int colon = host.lastIndexOf(':');
        if (colon > 0) {
            bool okPort = false;
            host.mid(colon + 1).toInt(&okPort);
            if (okPort) host = host.left(colon);
        }
        f->setData(Qt::UserRole, host);
        // C2: Program that opened the connection — the answer to "what actually goes
        // through the VPN", which the tab could never show before.
        {
            auto proc = item["Process"].toString();
            if (proc.isEmpty()) proc = "—";
            auto fp = new QTableWidgetItem(proc);
            fp->setToolTip(proc);
            ui->tableWidget_conn->setItem(row, 2, fp);
        }

        ui->tableWidget_conn->setItem(row, 3, f);
    }

    // Update the route-map strip.
    if (conn_route_summary != nullptr) {
        const int total = nProxy + nDirect + nBlock;
        if (total == 0) {
            conn_route_summary->setText(tr("Нет активных соединений"));
        } else {
            const int W = 44; // bar cells
            int wp = nProxy * W / total, wd = nDirect * W / total;
            int wb = W - wp - wd; // give the remainder to block so the bar is always full
            if (nBlock == 0) { wb = 0; if (nDirect >= nProxy) wd = W - wp; else wp = W - wd; }
            auto seg = [](int n, const QString &color) {
                return n > 0 ? QStringLiteral("<span style='color:%1;'>%2</span>").arg(color, QString(n, QChar(0x2588))) : QString(); // █
            };
            const QString bar = QStringLiteral("<span style='font-family:monospace;font-size:11px;'>%1%2%3</span>")
                                    .arg(seg(wp, "#4C9AFF"), seg(wd, "#3FB950"), seg(wb, "#E5484D"));
            const QString counts =
                QString::fromUtf8("\xF0\x9F\x8C\x8D ") + QStringLiteral("<span style='color:#4C9AFF;'>") + tr("Прокси: %1").arg(nProxy) + "</span>&nbsp;&nbsp;" +
                QString::fromUtf8("\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA ") + QStringLiteral("<span style='color:#3FB950;'>") + tr("Напрямую: %1").arg(nDirect) + "</span>&nbsp;&nbsp;" +
                QString::fromUtf8("\xE2\x9B\x94 ") + QStringLiteral("<span style='color:#E5484D;'>") + tr("Блок: %1").arg(nBlock) + "</span>";
            conn_route_summary->setText(bar + "<br>" + counts);
        }
    }
}

// Right-click a live connection → make a persistent routing rule from its destination.
// Turns "I see youtube.com going direct" into a one-click «Всегда через прокси».
void MainWindow::show_conn_context_menu(const QPoint &pos) {
    auto *cell = ui->tableWidget_conn->itemAt(pos);
    if (cell == nullptr) return;
    auto *destItem = ui->tableWidget_conn->item(cell->row(), 3); // destination moved right when the process column landed
    const QString host = destItem != nullptr ? destItem->data(Qt::UserRole).toString() : QString();
    if (host.isEmpty()) return;

    QMenu menu(this);
    auto *aDirect = menu.addAction(QString::fromUtf8("\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA ") + tr("Всегда напрямую: %1").arg(host));   // 🇷🇺
    auto *aProxy = menu.addAction(QString::fromUtf8("\xF0\x9F\x8C\x8D ") + tr("Всегда через прокси: %1").arg(host));               // 🌍
    auto *aBlock = menu.addAction(QString::fromUtf8("\xE2\x9B\x94 ") + tr("Блокировать: %1").arg(host));                          // ⛔
    menu.addSeparator();
    auto *aCopy = menu.addAction(tr("Копировать адрес"));
    auto *chosen = menu.exec(ui->tableWidget_conn->viewport()->mapToGlobal(pos));
    if (chosen == nullptr) return;
    if (chosen == aCopy) {
        QApplication::clipboard()->setText(host);
    } else if (chosen == aDirect) {
        add_routing_rule(host, 0);
    } else if (chosen == aProxy) {
        add_routing_rule(host, 1);
    } else if (chosen == aBlock) {
        add_routing_rule(host, 2);
    }
}

void MainWindow::add_routing_rule(const QString &host, int kind) {
    auto &r = NekoGui::dataStore->routing;
    if (r == nullptr || host.isEmpty()) return;
    const bool isIp = !QHostAddress(host).isNull();

    QString *field = nullptr;
    QString kindName;
    if (kind == 0) {
        field = isIp ? &r->direct_ip : &r->direct_domain;
        kindName = tr("напрямую");
    } else if (kind == 1) {
        field = isIp ? &r->proxy_ip : &r->proxy_domain;
        kindName = tr("через прокси");
    } else {
        field = isIp ? &r->block_ip : &r->block_domain;
        kindName = tr("в блокировку");
    }
    const QString entry = isIp ? host : QStringLiteral("domain:") + host;
    if (field->split('\n', Qt::SkipEmptyParts).contains(entry)) {
        show_log_impl(tr("Правило уже есть: %1").arg(host));
        return;
    }
    if (!field->isEmpty() && !field->endsWith('\n')) field->append('\n');
    field->append(entry);
    r->Save();
    show_log_impl(tr("Правило добавлено: %1 → %2").arg(host, kindName));

    if (NekoGui::dataStore->started_id >= 0 &&
        QMessageBox::question(GetMessageBoxParent(), tr("Правило добавлено"),
                              tr("«%1» теперь идёт %2. Перезапустить подключение, чтобы применить?").arg(host, kindName)) == QMessageBox::Yes) {
        neko_start(NekoGui::dataStore->started_id);
    }
}

// Hotkey

#ifndef NKR_NO_QHOTKEY

#include <QHotkey>

inline QList<std::shared_ptr<QHotkey>> RegisteredHotkey;

void MainWindow::RegisterHotkey(bool unregister) {
    while (!RegisteredHotkey.isEmpty()) {
        auto hk = RegisteredHotkey.takeFirst();
        hk->deleteLater();
    }
    if (unregister) return;

    QStringList regstr{
        NekoGui::dataStore->hotkey_mainwindow,
        NekoGui::dataStore->hotkey_group,
        NekoGui::dataStore->hotkey_route,
        NekoGui::dataStore->hotkey_system_proxy_menu,
    };

    for (const auto &key: regstr) {
        if (key.isEmpty()) continue;
        if (regstr.count(key) > 1) return; // Conflict hotkey
    }
    for (const auto &key: regstr) {
        QKeySequence k(key);
        if (k.isEmpty()) continue;
        auto hk = std::make_shared<QHotkey>(k, true);
        if (hk->isRegistered()) {
            RegisteredHotkey += hk;
            connect(hk.get(), &QHotkey::activated, this, [=] { HotkeyEvent(key); });
        } else {
            hk->deleteLater();
        }
    }
}

void MainWindow::HotkeyEvent(const QString &key) {
    if (key.isEmpty()) return;
    runOnUiThread([=] {
        if (key == NekoGui::dataStore->hotkey_mainwindow) {
            tray->activated(QSystemTrayIcon::ActivationReason::Trigger);
        } else if (key == NekoGui::dataStore->hotkey_group) {
            on_menu_manage_groups_triggered();
        } else if (key == NekoGui::dataStore->hotkey_route) {
            on_menu_routing_settings_triggered();
        } else if (key == NekoGui::dataStore->hotkey_system_proxy_menu) {
            ui->menu_spmode->popup(QCursor::pos());
        }
    });
}

#else

void MainWindow::RegisterHotkey(bool unregister) {}

void MainWindow::HotkeyEvent(const QString &key) {}

#endif

// VPN Launcher

bool MainWindow::StartVPNProcess() {
    //
    if (vpn_pid != 0) {
        return true;
    }
    //
    auto configPath = NekoGui::WriteVPNSingBoxConfig();
    auto scriptPath = NekoGui::WriteVPNLinuxScript(configPath);
    //
#ifdef Q_OS_WIN
    runOnNewThread([=] {
        vpn_pid = 1; // TODO get pid?
        WinCommander::runProcessElevated(QApplication::applicationDirPath() + "/greenrhythm_core.exe",
                                         {"--disable-color", "run", "-c", configPath}, "",
                                         NekoGui::dataStore->vpn_hide_console ? WinCommander::SW_HIDE : WinCommander::SW_SHOWMINIMIZED); // blocking
        vpn_pid = 0;
        runOnUiThread([=] { neko_set_spmode_vpn(false); });
    });
#else
    //
    auto vpn_process = new QProcess;
    QProcess::connect(vpn_process, &QProcess::stateChanged, this, [=](QProcess::ProcessState state) {
        if (state == QProcess::NotRunning) {
            vpn_pid = 0;
            vpn_process->deleteLater();
            GetMainWindow()->neko_set_spmode_vpn(false);
        }
    });
    //
    vpn_process->setProcessChannelMode(QProcess::ForwardedChannels);
#ifdef Q_OS_MACOS
    vpn_process->start("osascript", {"-e", QStringLiteral("do shell script \"%1\" with administrator privileges")
                                               .arg("bash " + scriptPath)});
#else
    vpn_process->start("pkexec", {"bash", scriptPath});
#endif
    vpn_process->waitForStarted();
    vpn_pid = vpn_process->processId(); // actually it's pkexec or bash PID
#endif
    return true;
}

bool MainWindow::StopVPNProcess(bool unconditional) {
    if (unconditional || vpn_pid != 0) {
        bool ok;
        core_process->processId();
#ifdef Q_OS_WIN
        auto ret = WinCommander::runProcessElevated("taskkill", {"/IM", "greenrhythm_core.exe",
                                                                 "/FI",
                                                                 "PID ne " + Int2String(core_process->processId())});
        ok = ret == 0;
#else
        QProcess p;
#ifdef Q_OS_MACOS
        p.start("osascript", {"-e", QStringLiteral("do shell script \"%1\" with administrator privileges")
                                        .arg("pkill -2 -U 0 greenrhythm_core")});
#else
        if (unconditional) {
            p.start("pkexec", {"killall", "-2", "greenrhythm_core"});
        } else {
            p.start("pkexec", {"pkill", "-2", "-P", Int2String(vpn_pid)});
        }
#endif
        p.waitForFinished();
        ok = p.exitCode() == 0;
#endif
        if (!unconditional) {
            ok ? vpn_pid = 0 : MessageBoxWarning(tr("Error"), tr("Failed to stop Tun process"));
        }
        return ok;
    }
    return true;
}
