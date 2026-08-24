// DO NOT INCLUDE THIS

namespace NekoGui {

    class Routing : public JsonStore {
    public:
        QString direct_ip;
        QString direct_domain;
        QString proxy_ip;
        QString proxy_domain;
        QString block_ip;
        QString block_domain;
        QString def_outbound = "proxy";
        QString custom = "{\"rules\": []}";

        // DNS
        QString remote_dns = "https://1.1.1.1/dns-query";
        QString remote_dns_strategy = "ipv4_only";
        QString direct_dns = "77.88.8.8";
        QString direct_dns_strategy = "ipv4_only";
        bool dns_routing = true;
        bool use_dns_object = false;
        QString dns_object = "";
        QString dns_final_out = "proxy";

        // Misc
        QString domain_strategy = "AsIs";
        QString outbound_domain_strategy = "AsIs";
        int sniffing_mode = SniffingMode::FOR_ROUTING;

        explicit Routing(int preset = 0);

        [[nodiscard]] QString DisplayRouting() const;

        static QStringList List();

        static bool SetToActive(const QString &name);

        // One-shot repair of schemes written before the RU preset shipped. Additive only:
        // see the implementation for exactly what it will and will not touch.
        static bool MigrateOne(Routing *r);

        static int MigrateAll();
    };

    class ExtraCore : public JsonStore {
    public:
        QString core_map;

        explicit ExtraCore();

        [[nodiscard]] QString Get(const QString &id) const;

        void Set(const QString &id, const QString &path);

        void Delete(const QString &id);
    };

    class InboundAuthorization : public JsonStore {
    public:
        QString username;
        QString password;

        InboundAuthorization();

        [[nodiscard]] bool NeedAuth() const;
    };

    class DataStore : public JsonStore {
    public:
        // Running

        QString core_token;
        int core_port = 19810;
        int started_id = -1919;
        bool core_running = false;
        bool prepare_exit = false;
        bool spmode_vpn = false;
        bool spmode_system_proxy = false;
        bool need_keep_vpn_off = false;
        QString appdataDir = "";
        QStringList ignoreConnTag = {};

        std::unique_ptr<Routing> routing;
        int imported_count = 0;
        bool refreshing_group_list = false;
        bool refreshing_group = false;
        int resolve_count = 0;

        // Flags
        QStringList argv = {};
        bool flag_use_appdata = false;
        bool flag_many = false;
        bool flag_tray = false;
        bool flag_debug = false;
        bool flag_restart_tun_on = false;
        bool flag_reorder = false;

        // Saved

        // Misc
        QString log_level = "info";
        QString test_latency_url = "http://cp.cloudflare.com/";
        QString test_download_url = "http://cachefly.cachefly.net/10mb.test";
        int test_download_timeout = 30;
        int test_concurrent = 5;
        bool old_share_link_format = true;
        int traffic_loop_interval = 1000;
        // Drives the Connections tab and the route summary. Off by default meant users
        // had no way to see what actually goes through the tunnel versus direct — the
        // single most common support question.
        bool connection_statistics = true;
        int current_group = 0; // group id
        QString mux_protocol = "h2mux";
        bool mux_padding = false;
        int mux_concurrency = 8;
        bool mux_default_on = false;
        bool auto_failover = false; // urltest: auto-pick fastest server in the group + fail over
        bool connection_autopilot = true; // watchdog: probe the live tunnel, self-heal (sub refresh / server switch)
        QString theme = "4"; // GreenRhythm Modern by default; existing installs keep their saved choice
        int language = 0;
        QString mw_size = "";
        bool check_include_pre = false;
        QString system_proxy_format = "";
        QStringList log_ignore = {};
        bool start_minimal = false;
        int max_log_line = 200;
        QString splitter_state = "";
        // First profile has ever been added → retire the full first-run welcome panel
        // forever (the compact empty-state CTAs still show when the list is emptied).
        bool onboarding_completed = false;

        // Subscription
        QString user_agent = ""; // set at main.cpp
        bool sub_use_proxy = false;
        bool sub_clear = false;
        bool sub_insecure = false;
        // Auto-refresh subscriptions by default (positive = enabled, minutes; min 30) so
        // server-side Reality key rotations are picked up without a manual re-import.
        int sub_auto_update = 120;
        // One-shot flag: flip legacy configs still on the old disabled default (-30) to the
        // new enabled default exactly once, without ever re-overriding a user's own choice.
        bool sub_auto_update_migrated = false;
        // Same idea for conn_stat. Every existing config pins it to false — both because the
        // shipped template said so and because the settings checkbox was disabled, so no user
        // ever chose the value. Without this, flipping the in-code default changes nothing.
        bool conn_stat_migrated = false;
        // And for the routing schemes: an existing routes_box file shadows the shipped
        // preset forever, so the QUIC block reached nobody who had already run the app.
        bool routing_quic_migrated = false;

        // Security
        bool skip_cert = false;
        QString utlsFingerprint = "";

        // Remember
        QStringList remember_spmode = {};
        int remember_id = -1919;
        bool remember_enable = false;

        // Socks & HTTP Inbound
        QString inbound_address = "127.0.0.1";
        int inbound_socks_port = 2080; // or Mixed
        InboundAuthorization *inbound_auth = new InboundAuthorization;
        QString custom_inbound = "{\"inbounds\": []}";

        // Routing
        QString custom_route_global = "{\"rules\": []}";
        QString active_routing = "Default";

        // VPN
        bool fake_dns = false;
        bool vpn_internal_tun = true;
        int vpn_implementation = 0;
        int vpn_mtu = 1500;
        bool vpn_ipv6 = false;
        bool vpn_hide_console = true;
        bool vpn_strict_route = false;

        /**
         * На macOS вести трафик системным прокси, а не туннелем.
         *
         * Существует потому, что у обоих режимов честная цена. Туннель ничего
         * не пропускает мимо, но спрашивает пароль при КАЖДОМ включении и
         * выключении — кэша прав между процессами на маке нет. Системный прокси
         * пароля не просит вовсе, но его уважают браузеры и обычные программы,
         * а терминал (curl, git, ssh), Docker и часть игр — нет: их трафик
         * пойдёт мимо. Выбирать это должен человек, а не мы за него.
         *
         * На остальных платформах поле не читается.
         */
        bool macos_use_pac = false;
        bool vpn_rule_white = false;
        QString vpn_rule_process = "";
        QString vpn_rule_cidr = "";

        // Hotkey
        QString hotkey_mainwindow = "";
        QString hotkey_group = "";
        QString hotkey_route = "";
        QString hotkey_system_proxy_menu = "";

        // Core
        int core_box_clash_api = -9090;
        QString core_box_clash_api_secret = "";
        QString core_box_underlying_dns = "";

        // Other Core
        ExtraCore *extraCore = new ExtraCore;

        // Methods

        DataStore();

        void UpdateStartedId(int id);

        QString GetUserAgent(bool isDefault = false) const;
    };

    extern DataStore *dataStore;

} // namespace NekoGui
