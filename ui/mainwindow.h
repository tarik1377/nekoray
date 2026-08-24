#pragma once

#include <QMainWindow>

#include "main/NekoGui.hpp"

#ifndef MW_INTERFACE

#include <QTime>
#include <QTimer>
#include <QTableWidgetItem>
#include <QKeyEvent>
#include <QSystemTrayIcon>
#include <QProcess>
#include <QTextDocument>
#include <QShortcut>
#include <QSemaphore>
#include <QMutex>
#include <QSet>

#include "GroupSort.hpp"

#ifdef Q_OS_MACOS
#include "sys/macos/PacServer.hpp"
#endif

#include "db/ProxyEntity.hpp"
#include "main/GuiUtils.hpp"

#endif

namespace NekoGui_sys {
    class CoreProcess;
}

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow() override;

    void refresh_proxy_list(const int &id = -1);

    void show_group(int gid);

    void refresh_groups();

    void refresh_status(const QString &traffic_update = "");

    void neko_start(int _id = -1);

    void neko_stop(bool crash = false, bool sem = false);

    void neko_set_spmode_system_proxy(bool enable, bool save = true);

    void neko_set_spmode_vpn(bool enable, bool save = true);

    void show_log_impl(const QString &log);

    void start_select_mode(QObject *context, const std::function<void(int)> &callback);

    void refresh_connection_list(const QJsonArray &arr);

    void RegisterHotkey(bool unregister);

    bool StopVPNProcess(bool unconditional = false);

signals:

    void profile_selected(int id);

public slots:

    void on_commitDataRequest();

    void on_menu_exit_triggered();

#ifndef MW_INTERFACE

private slots:

    void on_masterLogBrowser_customContextMenuRequested(const QPoint &pos);

    void on_menu_basic_settings_triggered();

    void on_menu_routing_settings_triggered();

    void on_menu_vpn_settings_triggered();

    void on_menu_hotkey_settings_triggered();

    void on_menu_add_from_input_triggered();

    void on_menu_add_from_clipboard_triggered();

    void on_menu_clone_triggered();

    void on_menu_move_triggered();

    void on_menu_delete_triggered();

    void on_menu_reset_traffic_triggered();

    void on_menu_profile_debug_info_triggered();

    void on_menu_copy_links_triggered();

    void on_menu_copy_links_nkr_triggered();

    void on_menu_export_config_triggered();

    void display_qr_link(bool nkrFormat = false);

    void on_menu_scan_qr_triggered();

    void on_menu_clear_test_result_triggered();

    void on_menu_manage_groups_triggered();

    void on_menu_select_all_triggered();

    void on_menu_delete_repeat_triggered();

    void on_menu_remove_unavailable_triggered();

    void on_menu_update_subscription_triggered();

    void on_menu_resolve_domain_triggered();

    void on_proxyListTable_itemDoubleClicked(QTableWidgetItem *item);

    void on_proxyListTable_customContextMenuRequested(const QPoint &pos);

    void on_tabWidget_currentChanged(int index);

private:
    Ui::MainWindow *ui;
    QSystemTrayIcon *tray;
    QShortcut *shortcut_ctrl_f = new QShortcut(QKeySequence("Ctrl+F"), this);
    QShortcut *shortcut_esc = new QShortcut(QKeySequence("Esc"), this);
    //
    NekoGui_sys::CoreProcess *core_process;
    qint64 vpn_pid = 0;
    //
    bool qvLogAutoScoll = true;
    QTextDocument *qvLogDocument = new QTextDocument(this);
    //
    QString title_error;
    int icon_status = -1;
    std::shared_ptr<NekoGui::ProxyEntity> running;
    QString traffic_update_cache;
    QTime last_test_time;
    //
    int proxy_last_order = -1;
    bool select_mode = false;
    QMutex mu_starting;
    QMutex mu_stopping;
    QMutex mu_exit;
    QSemaphore sem_stopped;
    int exit_reason = 0;

    QList<std::shared_ptr<NekoGui::ProxyEntity>> get_now_selected_list();

    QList<std::shared_ptr<NekoGui::ProxyEntity>> get_selected_or_group();

    void dialog_message_impl(const QString &sender, const QString &info);

    void refresh_proxy_list_impl(const int &id = -1, GroupSortAction groupSortAction = {});

    void refresh_proxy_list_impl_refresh_data(const int &id = -1);

    // Onboarding / empty-state page (takes the table's layout slot while the profile
    // list is empty; branding entry points, opt-in help panel)
    QWidget *onboarding_panel = nullptr;
    bool onboarding_dismissed = false;
    void build_onboarding_panel();
    void refresh_onboarding();

    // greenrhythm://import/<payload> deep link (untrusted; validated inside)
    void import_scheme_url(const QString &raw);
    QSet<QString> scheme_import_inflight; // dedupe reentrant identical deep links

    void keyPressEvent(QKeyEvent *event) override;

    void closeEvent(QCloseEvent *event) override;

    //

    void HotkeyEvent(const QString &key);

    bool StartVPNProcess();

    // grpc and ...

    static void setup_grpc();

    void speedtest_current_group(int mode, bool test_group);

    void speedtest_current();

    static void stop_core_daemon();

    void CheckUpdate();

    void show_about_greenrhythm();
    void refresh_subscription_status(); // «Зелёный Ритм» days/traffic-left badge + renew nudge
    void smart_connect_greenrhythm();   // connect to the fastest server in the brand group
    void show_subscription_qr();        // QR bridge: scan the subscription into a mobile client
    void import_link_offer_connect(const QString &link); // onboarding import → «Подключиться?»
    void run_diagnostics();             // internet/DNS/server/TLS checks + support report
#ifdef Q_OS_MACOS
    // Системный прокси на маке: свой файл автонастройки и снимок прежних
    // настроек. Подробно — sys/macos/MacProxyController.hpp.
    bool macos_apply_pac();
    void macos_clear_pac();
    NekoGui_sys::PacServer *pac_server = nullptr;
#endif
    void repair_windows_network();      // strip leftovers of other VPN/DPI tools that hijack traffic
    void show_conn_context_menu(const QPoint &pos);     // right-click a connection → make a routing rule
    void add_routing_rule(const QString &host, int kind); // kind: 0 direct, 1 proxy, 2 block

    QLabel *conn_route_summary = nullptr; // live route "map": proxy/direct/block split + bar

    // Connection health for the status pill, so it can say more than up/down: the tunnel
    // can be "connected" yet pass no traffic (QUIC stall, DPI, dead DNS), which is exactly
    // the case support cannot see today. Driven by the autopilot probe's result.
    enum ConnHealth { Health_Unknown = 0, Health_Ok = 1, Health_NoTraffic = 2 };
    int conn_health = Health_Unknown;

    // Persist the log to a rotating file so a customer can send it as one attachment
    // instead of a screenshot; also the source for «Сохранить лог…».
    QString log_file_path;
    void append_log_to_file(const QStringList &lines);
    QString diagnostics_header() const; // version/OS/server/mode, no secrets — for report + log

    // «Автопилот»: watchdog that probes the live tunnel end-to-end and self-heals —
    // refresh subscription (rotated keys), reconnect, switch server, then back off.
    QTimer *autopilot_timer = nullptr;
    int autopilot_fails = 0;
    int autopilot_stage = 0;
    qint64 autopilot_cooldown_until = 0;
    bool autopilot_probing = false;

    // Отход на резервное подключение и дорога обратно.
    //
    // ЗАЧЕМ ОТДЕЛЬНАЯ МАШИНА СОСТОЯНИЙ, а не «попробовать вернуться на
    // следующем тике». Без выдержки и без счётчика подряд удачных проверок
    // клиент на дрожащем канале метался бы между основным сервером и резервом
    // каждую минуту, обрывая соединения на каждом переключении. Резерв к тому
    // же платный по трафику: лишние возвраты стоят денег.
    int autopilot_fallback_from = -1;   // профиль, с которого ушли; -1 — не уходили
    qint64 autopilot_fallback_since = 0;
    qint64 autopilot_fallback_next_probe = 0;
    int autopilot_fallback_ok = 0;      // сколько удачных проверок основного подряд
    int autopilot_fallback_cycles = 0;  // сколько раз ушли за последний час
    qint64 autopilot_fallback_first = 0; // когда начался этот час

    void autopilot_tick();
    void autopilot_recover();
    /** Профиль резерва в списке, или nullptr. */
    std::shared_ptr<NekoGui::ProxyEntity> autopilot_relay_profile() const;
    /** Проверить прежний профиль, НЕ переключаясь на него. */
    void autopilot_probe_home();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

#endif // MW_INTERFACE
};

inline MainWindow *GetMainWindow() {
    return (MainWindow *) mainwindow;
}

void UI_InitMainWindow();
