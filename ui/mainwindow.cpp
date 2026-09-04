#include "ui/mainwindow_common.hpp"

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

#ifdef Q_OS_MACOS
    /*
     * ПЕРВЫМ ДЕЛОМ — вернуть системные настройки, если прошлый раз кончился
     * плохо. Файл снимка, переживший запуск, означает ровно это: приложение
     * упало или было убито, а в системе до сих пор стоит наш адрес
     * автонастройки, ведущий на порт, которого больше нет. Человек при этом
     * остаётся без интернета в браузере и никак не свяжет это с нами.
     *
     * Зовётся ДО всего остального намеренно: любая наша ошибка ниже не должна
     * помешать вернуть чужую систему в рабочее состояние.
     */
    NekoGui_sys::MacProxy::RestoreIfCrashed();
#endif

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
    // ОДИН ПУНКТ МЕНЮ, РАЗНАЯ НАЧИНКА. Прятать его вне Windows было ошибкой:
    // нужда «включил, а сеть не работает» есть на всех платформах, и человек,
    // не нашедший в меню того, что видел на скриншотах, решает, что у него не
    // та версия. Совпадать должно меню; отличаться — то, что за ним стоит.
    connect(ui->menu_gr_fixnet, &QAction::triggered, this, [=] {
#ifdef Q_OS_MACOS
        repair_macos_network();
#else
        repair_windows_network();
#endif
    });
#ifdef Q_OS_MACOS
    // На маке чинится не «сеть Windows», и называться пункт должен по делу.
    ui->menu_gr_fixnet->setText(tr("Починить сеть"));
    ui->menu_gr_fixnet->setToolTip(tr("Вернуть системные настройки, сбросить кэш имён "
                                      "и показать сторонние туннели"));
#endif
    connect(ui->menu_gr_adapters, &QAction::triggered, this, [=] { disable_extra_adapters(); });
#ifdef Q_OS_MACOS
    // Тот же пункт, но на маке он показывает, а не выключает: интерфейсы utun
    // держит поднявшая их программа, и гасить их снаружи бессмысленно.
    ui->menu_gr_adapters->setText(tr("Сторонние туннели…"));
    ui->menu_gr_adapters->setToolTip(tr("Показать чужие туннели и их маршруты. Ничего не выключается."));
#endif
    connect(ui->menu_gr_howto, &QAction::triggered, this, [=] { show_macos_modes(false); });
#ifndef Q_OS_MACOS
    // Пункт про выбор режима нужен там, где выбор есть. На Windows туннель
    // перехватывает всё и объяснять нечего.
    ui->menu_gr_howto->setVisible(false);
#endif
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    // Остаётся Linux: там ни того, ни другого пока не сделано, и показывать
    // пункт, за которым ничего нет, хуже, чем не показывать. Прятать, а не
    // гасить: серый пункт обещает условие, при котором он заработает.
    ui->menu_gr_adapters->setVisible(false);
    ui->menu_gr_fixnet->setVisible(false);
#endif
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
        // Список сам себя не перерисовывает: без этого заведённый профиль
        // появился бы только после перезапуска, и человек решил бы, что
        // активация не сработала.
        if (d.ProfileAdded()) refresh_proxy_list();
    });
    /*
     * ПУНКТ ВИДЕН ВСЕГДА — И ЭТО ИСПРАВЛЕНИЕ ПРЕЖНЕГО РЕШЕНИЯ.
     *
     * Раньше он прятался, когда компонента нет: пункт без компонента считался
     * дорогой в никуда, потому что в пакет программы компонент не кладётся.
     * Он и не будет класться — резерв продаётся отдельно и выдаётся по
     * подписке, а не раздаётся вместе с программой.
     *
     * Но из этой пары получался замкнутый круг: компонент приезжает во время
     * активации, активация начинается отсюда, а пункт спрятан, пока компонента
     * нет. То есть спрятана была ровно та дверь, через которую он приходит.
     * Единственный способ попасть внутрь — не прятать её.
     */
    connect(ui->menu_gr_buy, &QAction::triggered, this, [=] { QDesktopServices::openUrl(QUrl(GreenRhythm::kBuyUrl)); });
    connect(ui->menu_gr_telegram, &QAction::triggered, this, [=] { QDesktopServices::openUrl(QUrl(GreenRhythm::kTelegramUrl)); });
    connect(ui->menu_gr_about, &QAction::triggered, this, [=] { show_about_greenrhythm(); });

    // ПАНЕЛЬ — ПЕРВОЙ КНОПКОЙ И ПЕРВЫМ ПУНКТОМ.
    //
    // Всё наше лежало в подменю второго уровня, а слово «Панель» на панели
    // инструментов занято чужим дашбордом Clash. Пока средство лечения спрятано
    // на два уровня вглубь и названо по-английски, им не пользуются: владелец
    // полдня искал, почему не работает игра, а нужный список лежал в настройках
    // TUN под заголовком «Bypass Process Name».
    {
        auto *panelAction = new QAction(tr("Зелёный Ритм"), this);
        // Прямо из ресурсов, а не через QIcon::fromTheme: поиск по теме молча
        // отдаёт пустую иконку, если имя не нашлось, и кнопка остаётся без знака
        // — отказ, который на глаз не отличить от «так и задумано».
        panelAction->setIcon(QIcon(QStringLiteral(":/icon/gr-panel.svg")));
        connect(panelAction, &QAction::triggered, this, [=] { open_greenrhythm_panel(); });
        if (!ui->menu_greenrhythm->actions().isEmpty()) {
            auto *first = ui->menu_greenrhythm->actions().first();
            ui->menu_greenrhythm->insertAction(first, panelAction);
            ui->menu_greenrhythm->insertSeparator(first);
        } else {
            ui->menu_greenrhythm->addAction(panelAction);
        }

        auto *panelButton = new QToolButton(this);
        panelButton->setDefaultAction(panelAction);
        panelButton->setIconSize(QSize(24, 24));
        panelButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        panelButton->setCursor(Qt::PointingHandCursor);
        ui->horizontalLayout_2->insertWidget(0, panelButton);
    }

    // НОВАЯ ОБОЛОЧКА: боковая колонка и страницы вместо ряда кнопок и таблицы
    // во весь экран. Ставится ПОСЛЕ всей проводки — она не создаёт ни таблицы
    // серверов, ни вкладок журнала, а забирает уже существующие и раскладывает
    // по страницам. Поэтому сортировка, перетаскивание, контекстное меню, поиск
    // и проверка задержки продолжают работать: это те же объекты, к которым
    // привязан весь код выше.
    {
        shell = new GreenRhythm::MainShell(this);
        shell->adopt(ui->tabWidget, ui->down_tab);

        // ШУМНЫЕ КОЛОНКИ ПРЯЧУТСЯ, А НЕ УДАЛЯЮТСЯ. «Тип» у всех строк один и тот
        // же, «Адрес» человеку не говорит ничего, а имя из-за них обрезалось до
        // «Germanyyy…». Скрытие оставляет ячейки заполненными, поэтому поиск по
        // адресу и сортировка по нему продолжают работать — удаление их бы
        // сломало, причём молча.
        ui->proxyListTable->setColumnHidden(0, true); // тип
        ui->proxyListTable->setColumnHidden(1, true); // адрес

        // Имени — всё свободное место. Ради него список и существует, а до
        // сих пор оно обрезалось до «Germanyyy…», потому что ширину делили
        // поровну между шестью колонками, из которых две человеку не нужны.
        ui->proxyListTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

        // Строки рисуются карточками. Делегат меняет ТОЛЬКО отрисовку: модель,
        // сортировка, перетаскивание, меню и поиск остаются теми же. Скрытые
        // колонки он читает как данные — потому их и не удаляли.
        ui->proxyListTable->setColumnHidden(3, true); // результат теста
        ui->proxyListTable->setColumnHidden(4, true); // трафик
        ui->proxyListTable->setItemDelegateForColumn(
            2, new GreenRhythm::ServerCardDelegate(0, 1, 3, this));
        ui->proxyListTable->verticalHeader()->setVisible(false);
        ui->proxyListTable->horizontalHeader()->setVisible(false);
        ui->proxyListTable->setShowGrid(false);

        // Вкладки групп прячем, когда группа одна: полоса с единственной
        // вкладкой занимает место и не даёт выбора.
        ui->tabWidget->tabBar()->setVisible(ui->tabWidget->count() > 1);
        setCentralWidget(shell);

#ifdef Q_OS_WIN
        // Полосу меню прячем: в современных клиентах её нет, а всё её содержимое
        // теперь достижимо кнопкой «Ещё». На macOS не трогаем — там полоса
        // системная, живёт вверху экрана и человек её там ждёт.
        menuBar()->setVisible(false);
#endif

        connect(shell, &GreenRhythm::MainShell::connectToggled, this, [this] {
            if (NekoGui::dataStore->started_id >= 0) {
                neko_stop();
            } else {
                smart_connect_greenrhythm();
            }
        });
        connect(shell, &GreenRhythm::MainShell::moreRequested, this,
                [this](const QPoint &pos) {
                    // Меню собирается из той же полосы, что была сверху: те же
                    // объекты, та же проводка. Копировать пункты сюда значило бы
                    // завести вторую правду, которая разойдётся с первой.
                    QMenu more(this);
                    for (auto *action: menuBar()->actions()) more.addAction(action);
                    more.exec(pos);
                });
        connect(shell, &GreenRhythm::MainShell::troubleRequested, this,
                [this] { open_what_broke(); });
        connect(shell, &GreenRhythm::MainShell::bypassListRequested, this,
                [this] { open_greenrhythm_panel(); });
        connect(shell, &GreenRhythm::MainShell::renewRequested, this, [] {
            QDesktopServices::openUrl(QUrl(GreenRhythm::kRenewUrl));
        });
        connect(shell, &GreenRhythm::MainShell::panelRequested, this,
                [this] { open_greenrhythm_panel(); });
        connect(shell, &GreenRhythm::MainShell::addServerRequested, ui->menu_add_from_clipboard,
                &QAction::trigger);
    }

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
    // Высота под карточку. Задаётся здесь, а не подсказкой делегата: таблица
    // берёт высоту из вертикального заголовка, и он перебивал sizeHint —
    // карточки выходили сплюснутыми, а подпись под именем не помещалась.
    ui->proxyListTable->verticalHeader()->setDefaultSectionSize(62);
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
    connect(ui->checkBox_VPN, &QCheckBox::clicked, this, [=](bool checked) {
        // ПЕРВОЕ включение на маке объясняем ДО того, как что-то произойдёт.
        // Иначе человек нажимает галку, получает запрос пароля и не понимает
        // ни зачем он, ни почему повторится в следующий раз. На Windows и
        // Linux окно не показывается: там выбора нет.
        // РАННИЙ ВЫХОД ТОЛЬКО ПО ОТВЕТУ ОКНА, а не по состоянию настроек.
        //
        // Здесь стояла проверка «spmode_vpn || spmode_system_proxy», и она читала
        // состояние ДО нажатия. Значит у всякого, у кого уже включён системный
        // прокси, галка «Туннель» не делала НИЧЕГО: обработчик выходил, а
        // refresh_status возвращал её на место. Ни диалога, ни строки в журнале —
        // человек видел, что галка отщёлкнулась сама, ровно как в той поломке,
        // ради которой окно и заводилось.
        //
        // На macOS било сильнее: окно объясняет режимы ОДИН раз, дальше
        // show_macos_modes выходит сразу, и галка оставалась мёртвой навсегда.
        if (checked && show_macos_modes(true)) {
            // Окно само включило выбранный режим — делать здесь больше нечего.
            refresh_status();
            return;
        }
        neko_set_spmode_vpn(checked);
    });
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
                QProcess::execute(System32Exe("taskkill.exe"), {"/F", "/IM", "greenrhythm_core.exe"});
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

    // Второй одноразовый ремонт, свой флаг: правило «мимо туннеля» поднимается выше
    // блокировки udp/443. Ниже неё оно не работало вовсе — см. MigrateGameBypass.
    if (!NekoGui::dataStore->routing_games_migrated) {
        const int migrated = NekoGui::Routing::MigrateGamesAll();
        NekoGui::dataStore->routing_games_migrated = true;
        NekoGui::dataStore->Save();
        if (migrated > 0)
            MW_show_log(tr("Маршрутизация обновлена: игры и звонки теперь идут мимо туннеля раньше блокировок (схем: %1).").arg(migrated));
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
#ifdef Q_OS_MACOS
            /*
             * На маке — свой путь, и не ради красоты.
             *
             * Вендорный SetSystemProxy прописывает ОДИН адрес на всё, а
             * ClearSystemProxy потом гасит прокси на всех сетевых службах, не
             * помня, что там было до нас. Первое отправляет в канал домашний
             * NAS, принтер и рабочую сеть; второе стирает человеку его
             * собственный корпоративный прокси без возможности вернуть.
             *
             * Здесь вместо этого файл автонастройки (местное всегда напрямую) и
             * снимок прежнего состояния на диске до всякой правки.
             */
            if (!macos_apply_pac()) {
                MessageBoxWarning(software_name,
                                  tr("Не удалось включить системный прокси. "
                                     "Возможно, настройками сети управляет организация."));
                refresh_status();
                return;
            }
#else
            SetSystemProxy(http_port, socks_port);
#endif
        } else {
#ifdef Q_OS_MACOS
            macos_clear_pac();
#else
            ClearSystemProxy();
#endif
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
    // Настройка «туннель внутри ядра» спрашивается ВМЕСТЕ с платформой. На маке
    // такого режима нет вовсе (см. PlatformSupportsInternalTun), и без этой
    // проверки включение уходило в ветку запроса прав, которая знает только про
    // Linux и Windows, — то есть проваливалось в отказ молча, без диалога и без
    // строки в журнале. Человек видел, что галка отщёлкнулась сама.
    const bool internalTun = NekoGui::UseInternalTun();

    if (enable != NekoGui::dataStore->spmode_vpn) {
        if (enable) {
            /*
             * ЧУЖОЙ ЖИВОЙ ТУННЕЛЬ — ОТКАЗ ВСЛУХ, А НЕ МОЛЧАЛИВОЕ ПРИСОЕДИНЕНИЕ.
             *
             * sing-tun 0.8.12 сменил поведение: раньше «адаптер уже существует»
             * было громкой ошибкой, теперь существующий молча ОТКРЫВАЕТСЯ. То
             * есть второй запущенный клиент цепляется к туннелю первого, два
             * ядра делят один адаптер, и пакеты рвутся пополам. Снаружи —
             * «туннель поднят, DNS отвечает, страницы не грузятся»: поломка
             * сети на вид, второй запуск на деле.
             *
             * Замечено живьём: у владельца шесть суток работал прежний клиент и
             * держал адаптер, а каждая новая сборка молча к нему цеплялась и
             * считалась сломанной.
             *
             * Молчание здесь дороже отказа: отказ человек прочитает и закроет
             * лишнюю копию, а присоединение он будет чинить неделю.
             */
            if (NekoGui_sys::OurTunnelHeldByAnother()) {
                MessageBoxWarning(
                    software_name,
                    tr("Туннель уже поднят другой копией программы.\n\n"
                       "Закройте её полностью и попробуйте снова. Две копии не могут "
                       "пользоваться одним туннелем: трафик делится между ними, и тогда "
                       "подключение выглядит исправным, но страницы не открываются."));
                neko_set_spmode_FAILED
            }
            if (internalTun) {
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
            if (internalTun) {
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

    if (internalTun && NekoGui::dataStore->started_id >= 0) neko_start(NekoGui::dataStore->started_id);
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

        // Та же правда — в новую оболочку. Состояние живёт в ОДНОМ месте: пилюля
        // внизу осталась для тех, кто к ней привык, но главным теперь служит
        // крупная кнопка, и расходиться этим двум нельзя.
        if (shell != nullptr) {
            QString latency;
            if (up) {
                const auto ms = running->latency;
                if (ms > 0) latency = tr("%1 мс").arg(ms);
            }
            shell->setConnectionState(up, nm, latency);

            // Метки под именем: протокол и транспорт. Берём у самого профиля, а
            // не выдумываем — DisplayType() уже собирает это для таблицы.
            QStringList tags;
            if (up) {
                const auto type = running->bean->DisplayType().trimmed();
                if (!type.isEmpty()) tags << type;
            }
            shell->setServerTags(tags);

            // Пустой список — не пустой экран.
            shell->setEmpty(NekoGui::profileManager->profiles.empty());
        }
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
        // ЧЕТВЁРТОЕ СОСТОЯНИЕ: работаем на резерве.
        //
        // Синий, а не зелёный, и это по делу: зелёный означает «всё как
        // задумано», а резерв — это запасной путь, он медленнее и тратит
        // трафик подписки. Одно поле, по которому и человек, и поддержка сразу
        // понимают, почему стало медленнее, не задавая вопросов.
        if (up && !noTraffic && running != nullptr && running->type == "relay") {
            pillText = tr("Подключено") + QStringLiteral(" · ") + tr("резервное подключение");
            pillColor = QStringLiteral("#3B82F6");
        }
        ui->label_conn_pill->setText(pillText);
        // Пункт «Починить сеть Windows» на маке и в Linux СПРЯТАН, поэтому
        // звать по имени в него можно только на Windows. Человек, открывший
        // меню и не нашедший названного, решает, что у него сломано меню или не
        // та версия, — и это хуже, чем не подсказать вовсе.
        ui->label_conn_pill->setToolTip(noTraffic
                                            ? tr("Соединение с сервером есть, но сайты не грузятся. "
                                                 "Частые причины: QUIC/DoH в браузере, DPI, посторонний "
                                                 "перехватчик. Попробуйте «Диагностику».")
#ifdef Q_OS_WIN
                                                  + tr(" Или «Починить сеть Windows».")
#endif
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



// One-click connection diagnostics — the manual check we kept doing by hand (is the
// internet up? does DNS resolve? does the server's port accept TCP? does its TLS
// handshake complete, or is it DPI-filtered?). Runs off the UI thread; produces a
// plain-language verdict plus a secrets-free report the user can hand to support.
#ifdef Q_OS_MACOS
/**
 * Собрать файл автонастройки по текущим правилам, поднять его и отдать системе.
 *
 * Файл пересобирается на КАЖДОЕ включение, а не хранится: правила человек
 * правит между сеансами, и отданный системе устаревший файл — это трафик,
 * идущий не туда, о чём никто не узнает.
 *
 * Пропущенные правила называются в журнале поимённо. Молчание здесь дороже
 * всего: поддержка иначе ищет причину «почему этот сайт пошёл не туда» вслепую,
 * а причина — в том, что правило просто не выразимо в этом режиме.
 */
bool MainWindow::macos_apply_pac() {
    NekoGui_sys::PacInput in;
    in.socksPort = NekoGui::dataStore->inbound_socks_port;
    in.directDomain = NekoGui::dataStore->routing->direct_domain;
    in.directIp = NekoGui::dataStore->routing->direct_ip;
    in.proxyDomain = NekoGui::dataStore->routing->proxy_domain;
    in.proxyIp = NekoGui::dataStore->routing->proxy_ip;
    in.blockDomain = NekoGui::dataStore->routing->block_domain;

    NekoGui_sys::PacNotes notes;
    const auto pac = NekoGui_sys::BuildPac(in, &notes);

    if (!notes.skipped.isEmpty()) {
        MW_show_log(tr("Системный прокси: часть правил в этом режиме не применяется — %1")
                        .arg(notes.skipped.join(", ")));
        MW_show_log(tr("Эти правила работают только в режиме туннеля."));
    }

    // ЧЕСТНО ПРО ОХВАТ, И ЗАРАНЕЕ. Системный прокси уважают браузеры и обычные
    // программы; терминал (curl, git, ssh), Docker и часть игр — нет, их трафик
    // идёт мимо. Не сказав этого здесь, мы получим переписку вида «через браузер
    // работает, через терминал нет», и разбираться в ней будет дороже, чем
    // прочитать одну строку.
    MW_show_log(tr("Системный прокси включён. Браузеры и обычные программы пойдут через "
                   "канал; терминал, Docker и часть игр — мимо. Полный охват даёт режим "
                   "туннеля."));

    if (pac_server == nullptr) pac_server = new NekoGui_sys::PacServer(this);
    const auto url = pac_server->Start(pac);
    if (url.isEmpty()) {
        MW_show_log(tr("Системный прокси: не удалось поднять локальную выдачу настроек."));
        return false;
    }

    if (!NekoGui_sys::MacProxy::Enable(url)) {
        // Снимок к этому моменту уже на диске, поэтому откат возможен и делается
        // сразу: полувключённое состояние хуже выключенного.
        NekoGui_sys::MacProxy::Disable();
        pac_server->Stop();
        return false;
    }
    return true;
}

void MainWindow::macos_clear_pac() {
    NekoGui_sys::MacProxy::Disable();
    if (pac_server != nullptr) pac_server->Stop();
}
#endif

/**
 * Показать выбор режима и объяснение к нему.
 *
 * `onlyOnce` — вызов из потока событий подключения: показываем, если человек
 * этого экрана ещё не видел. Из меню приходит false, то есть открывается всегда.
 *
 * Функция собирается на всех платформах, а показывает окно только на маке.
 * Прятать под #ifdef ЦЕЛИКОМ нельзя: код под ним не компилируется нигде, кроме
 * своей платформы, и ошибка в нём всплывает только на сборке, которую здесь
 * проверить нечем. Уже обжигались.
 */
bool MainWindow::show_macos_modes(bool onlyOnce) {
#ifdef Q_OS_MACOS
    // Объясняем ОДИН раз. Одно и то же окно при каждом запуске приучает
    // закрывать его не читая — а тогда объяснение не работает вовсе.
    if (onlyOnce && NekoGui::dataStore->macos_mode_explained) return false;

    DialogMacosMode dlg(this);
    dlg.exec();

    // Засчитывается любой исход, включая «решу позже»: человек прочитал.
    if (!NekoGui::dataStore->macos_mode_explained) {
        NekoGui::dataStore->macos_mode_explained = true;
        NekoGui::dataStore->Save();
    }

    switch (dlg.Chosen()) {
        case DialogMacosMode::Tunnel:
            neko_set_spmode_system_proxy(false);
            neko_set_spmode_vpn(true);
            return true;
        case DialogMacosMode::SystemProxy:
            neko_set_spmode_vpn(false);
            neko_set_spmode_system_proxy(true);
            return true;
        default:
            // «Решу позже» — режимом никто не распорядился, и вызывающий
            // обязан продолжить обычным путём.
            return false;
    }
#else
    Q_UNUSED(onlyOnce)
    return false;
#endif
}






/** Профиль резерва — он в списке один, потому что реквизиты выданы устройству. */
std::shared_ptr<NekoGui::ProxyEntity> MainWindow::autopilot_relay_profile() const {
    for (const auto &[id, ent]: NekoGui::profileManager->profiles) {
        if (ent != nullptr && ent->type == "relay") return ent;
    }
    return nullptr;
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

/**
 * Панель «Зелёный Ритм».
 *
 * Действия НЕ дублируются: каждая кнопка дёргает тот же QAction, что и пункт
 * меню. Скопировать сюда тела обработчиков было бы короче на вид, но тогда
 * правка любого из них чинила бы одно место из двух — а второе продолжало бы
 * работать по-старому и молча.
 *
 * Панель не закрывается при нажатии: иначе несохранённый список программ
 * пропадал бы всякий раз, когда человек по дороге нажал «Проверить».
 */
/**
 * Окно разбора «почему не работает эта программа».
 *
 * Живёт не модально: полноэкранная игра закрывает собой любое окно, и человек
 * должен иметь возможность свернуть нас, попробовать в игре и вернуться. Пока
 * окно открыто, оно получает те же записи о соединениях, что и таблица.
 */
void MainWindow::open_what_broke() {
    if (what_broke != nullptr) {
        ActivateWindow(what_broke);
        return;
    }
    auto *d = new DialogWhatBroke(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setAlreadyDirect(NekoGui::dataStore->vpn_rule_process.split(QChar(0x0A), Qt::SkipEmptyParts));

    // Действует ли поимённый список ПРЯМО СЕЙЧАС. Признак намеренно не включает
    // UseInternalTun(): на macOS внутреннего туннеля нет, а список там применяется
    // вторым путём, при сборке конфига туннеля, — по этой проверке кнопка была бы
    // навсегда погашена ровно на той платформе, которую сейчас доводят.
    if (!NekoGui::dataStore->spmode_vpn) {
        d->setFixAvailable(false,
                           tr("Но пустить её напрямую сейчас нельзя: поимённый выбор действует "
                              "только при включённом режиме VPN. Включите его и загляните сюда "
                              "снова."));
    } else if (NekoGui::dataStore->vpn_rule_white) {
        d->setFixAvailable(false,
                           tr("Но сейчас список работает наоборот: в нём перечислены те, кого "
                              "пускают через VPN, а не мимо. Пока так, увести программу отсюда "
                              "нельзя."));
    } else {
        d->setFixAvailable(true, QString());
    }

#ifdef Q_OS_WIN
    // СОСТОЯНИЕ ПОРТОВ МАШИНЫ — спрашиваем систему один раз, при открытии.
    //
    // Windows умеет отдавать номера портов Hyper-V, WSL и Docker, и программа,
    // попросившая такой номер, получает отказ доступа. Жалуется она при этом на
    // что угодно, только не на порт: у нас это было «подключение не
    // запускается», у чужого игрового лаунчера — «ошибка соединения» и сорванная
    // установка. Увидеть это можно только спросив систему, и никто не спрашивал.
    {
        auto ask = [](const QStringList &args) {
            QProcess p;
            p.start(QStringLiteral("netsh"), args);
            if (!p.waitForFinished(5000)) {
                p.kill();
                return QString();
            }
            return QString::fromLocal8Bit(p.readAllStandardOutput());
        };
        const auto health = GreenRhythm::parsePortHealth(
            ask({"int", "ipv4", "show", "dynamicport", "tcp"}),
            ask({"int", "ipv4", "show", "excludedportrange", "protocol=tcp"}));
        d->setSystemNote(health.verdict());
    }
#endif

    connect(d, &DialogWhatBroke::fixRequested, this, [this](const QString &program) {
        auto list = NekoGui::dataStore->vpn_rule_process.split(QChar(0x0A), Qt::SkipEmptyParts);
        // Дважды не добавляем: список читает человек, и повтор в нём выглядит
        // ошибкой, хотя вреда не несёт.
        for (const auto &line: list) {
            if (line.trimmed().compare(program, Qt::CaseInsensitive) == 0) return;
        }
        list << program;
        NekoGui::dataStore->vpn_rule_process = list.join(QChar(0x0A));
        NekoGui::dataStore->Save();
        MW_show_log(tr("«%1» пойдёт напрямую, мимо туннеля.").arg(program));
    });

    connect(d, &DialogWhatBroke::reconnectRequested, this, [this] {
        if (NekoGui::dataStore->started_id >= 0) neko_start(NekoGui::dataStore->started_id);
    });

    connect(d, &QObject::destroyed, this, [this] { what_broke = nullptr; });
    what_broke = d;
    d->show();
}
void MainWindow::open_greenrhythm_panel() {
    DialogGreenRhythm d(this);

    d.setBypassList(NekoGui::dataStore->vpn_rule_process);
    d.setAutopilot(NekoGui::dataStore->connection_autopilot);

    auto *running = NekoGui::dataStore->started_id >= 0
                        ? NekoGui::profileManager->GetProfile(NekoGui::dataStore->started_id).get()
                        : nullptr;
    d.setConnectionState(running != nullptr,
                         running != nullptr ? running->bean->DisplayName().left(40) : QString());

    connect(&d, &DialogGreenRhythm::connectRequested, ui->menu_gr_connect, &QAction::trigger);
    connect(&d, &DialogGreenRhythm::relayRequested, ui->menu_gr_relay, &QAction::trigger);
    connect(&d, &DialogGreenRhythm::qrRequested, ui->menu_gr_qr, &QAction::trigger);
    connect(&d, &DialogGreenRhythm::troubleRequested, this, [this, &d] {
        // Панель показана модально, и немодальное окно разбора поверх неё не
        // принимало бы НИ ОДНОГО нажатия: оно появлялось бы, клики проваливались,
        // и человек при первом же знакомстве решал бы, что механизм сломан.
        //
        // Закрываем именно accept(), а не close(): по «Сохранить» панель отдаёт
        // наружу список «мимо туннеля», а по отказу — нет. close() потерял бы
        // несохранённые правки, то есть ровно то, что мы бережём.
        d.accept();
        open_what_broke();
    });
    connect(&d, &DialogGreenRhythm::diagnosticsRequested, ui->menu_gr_diag, &QAction::trigger);
    connect(&d, &DialogGreenRhythm::buyRequested, ui->menu_gr_buy, &QAction::trigger);
    connect(&d, &DialogGreenRhythm::telegramRequested, ui->menu_gr_telegram, &QAction::trigger);
    connect(&d, &DialogGreenRhythm::fixNetRequested, ui->menu_gr_fixnet, &QAction::trigger);
    connect(&d, &DialogGreenRhythm::adaptersRequested, ui->menu_gr_adapters, &QAction::trigger);

    connect(&d, &DialogGreenRhythm::autopilotChanged, this, [this](bool on) {
        // Через тот же пункт меню, а не полем напрямую: у пункта есть галка, и
        // разошедшись, они показывали бы разное состояние одного переключателя.
        ui->menu_gr_autopilot->setChecked(on);
    });

    connect(&d, &DialogGreenRhythm::bypassChanged, this, [this](const QString &text) {
        if (NekoGui::dataStore->vpn_rule_process == text) return;
        NekoGui::dataStore->vpn_rule_process = text;
        NekoGui::dataStore->Save();
        MW_show_log(tr("Список «мимо туннеля» сохранён. Начнёт действовать при следующем подключении."));
    });

    d.exec();
}
void MainWindow::show_about_greenrhythm() {
    auto title = tr("<b>Клиент сервиса «%1»</b>").arg(GreenRhythm::kServiceName);
    // Лицензия и ссылка на исходники — не украшение: GPL-3.0 требует сообщать
    // их получателю программы, а «О программе» — то место, где человек их и
    // станет искать.
    auto body = tr("Версия: %1<br><br>"
                   "Сайт: <a href=\"%2\">%2</a><br>"
                   "Поддержка: <a href=\"%3\">%4</a><br><br>"
                   "Лицензия GPL-3.0 · исходный код: <a href=\"%5\">%5</a>")
                    .arg(QString(NKR_VERSION))
                    .arg(GreenRhythm::kSiteUrl)
                    .arg(GreenRhythm::kTelegramUrl, GreenRhythm::kTelegramHandle,
                         QStringLiteral("https://github.com/tarik1377/nekoray"));
    QMessageBox box(QMessageBox::Information, tr("О программе"), title, QMessageBox::Ok, this);
    box.setTextFormat(Qt::RichText);
    box.setInformativeText(body);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.exec();
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
    /*
     * ВЫВОД ПРИВИЛЕГИРОВАННОГО ПРОЦЕССА — В ЖУРНАЛ ПРИЛОЖЕНИЯ, а не в терминал.
     *
     * Стояло ForwardedChannels: вывод уходил на стандартные потоки самого
     * приложения, то есть в терминал, которого у человека, запустившего значок,
     * нет вовсе. Значит причина любого отказа туннеля пропадала бесследно —
     * именно поэтому на маке «не работает TUN» до сих пор не имело подробностей.
     */
    vpn_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcess::connect(vpn_process, &QProcess::readyRead, this, [=] {
        const auto text = QString::fromLocal8Bit(vpn_process->readAll());
        for (const auto &line: text.split(QChar(10), Qt::SkipEmptyParts)) {
            MW_show_log_ext("tun", line.trimmed());
        }
    });
#ifdef Q_OS_MACOS
    /*
     * ПУТЬ НА МАКЕ СОДЕРЖИТ ПРОБЕЛ, и здесь он проходит через ДВА разбора.
     *
     * Каталог настроек — ~/Library/Application Support/…; скрипт пишется туда.
     * Строка ниже сначала читается как литерал AppleScript, потом её содержимое
     * отдаётся оболочке. Собранная без кавычек, она превращалась в
     * `bash /Users/x/Library/Application` — то есть туннель не запускался
     * никогда, а человек видел только, что галка отщёлкнулась сама.
     *
     * Поэтому уровней кавычек два, и порядок важен: сначала путь берётся в
     * одинарные кавычки для оболочки (внутри них она не трогает ни пробелы, ни
     * доллары), затем получившееся экранируется как литерал AppleScript.
     *
     * Сырые литералы здесь не для красоты: замена одинарной кавычки — это
     * последовательность из четырёх символов, и записанная обычным литералом со
     * слэшами она уже однажды свернулась в три при первой же правке. Ошибка
     * такого рода не видна глазом и всплывает на одном пути из тысячи.
     */
    const auto shellQuoted = "'" + QString(scriptPath).replace("'", R"('\'')") + "'";
    auto asLiteral = QString("bash " + shellQuoted);
    asLiteral.replace("\\", "\\\\").replace("\"", "\\\"");
    vpn_process->start("osascript", {"-e", QStringLiteral("do shell script \"%1\" with administrator privileges")
                                               .arg(asLiteral)});
#else
    vpn_process->start("pkexec", {"bash", scriptPath});
#endif
    /*
     * РЕЗУЛЬТАТ ПРОВЕРЯЕТСЯ, И ЭТО ИСПРАВЛЕНИЕ, А НЕ ПРИДИРКА.
     *
     * Стояло `waitForStarted(); return true;` — то есть «поднялся» возвращалось
     * всегда. Человек, отменивший запрос пароля, получал включённую галку и
     * выключенный туннель; отказ скрипта выглядел так же. На маке это и есть
     * самый частый способ не запустить туннель, и он не давал ни одного признака.
     *
     * Двух проверок мало по отдельности и хватает вместе: waitForStarted ловит
     * «не запустилось вовсе», короткая выдержка следом — «запустилось и сразу
     * умерло», а это как раз отменённый пароль и упавший скрипт.
     */
    if (!vpn_process->waitForStarted(20000)) {
        MessageBoxWarning(software_name,
                          tr("Не удалось запустить туннель.") + "\n\n" +
                              tr("Скорее всего, запрос прав администратора был отменён. "
                                 "Попробуйте снова и подтвердите его."));
        vpn_process->deleteLater();
        return false;
    }
    // Умирает такой процесс мгновенно: осталось дать ему это сделать.
    if (vpn_process->waitForFinished(1500)) {
        const auto tail = QString::fromLocal8Bit(vpn_process->readAll()).trimmed();
        MW_show_log_ext("tun", tail.isEmpty() ? tr("процесс туннеля завершился сразу") : tail);
        MessageBoxWarning(software_name,
                          tr("Туннель запустился и сразу закрылся.") + "\n\n" +
                              tr("Подробности — в журнале приложения, раздел «tun»."));
        vpn_process->deleteLater();
        return false;
    }
    vpn_pid = vpn_process->processId(); // actually it's pkexec or bash PID
#endif
    return true;
}

bool MainWindow::StopVPNProcess(bool unconditional) {
    if (unconditional || vpn_pid != 0) {
        bool ok;
        core_process->processId();
#ifdef Q_OS_WIN
        auto ret = WinCommander::runProcessElevated(System32Exe("taskkill.exe"), {"/IM", "greenrhythm_core.exe",
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
