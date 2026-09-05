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

        // Второй ремонт, отдельно от первого: порядок правил. Список игр,
        // стоящий ПОСЛЕ блокировки udp/443, не спасает игру, которой этот порт
        // нужен, — её пакеты гасятся раньше, чем доходят до списка.
        static bool MigrateGameBypass(Routing *r);

        static int MigrateGamesAll();
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
        // Порт Clash API — свой, а не core_port + 1. Соседний порт никто не
        // проверял на занятость, и ядро падало на старте с «Only one usage of
        // each socket address» ровно тогда, когда его успевал занять кто-то
        // другой. В файл не пишется: выбирается при каждом запуске, как core_port.
        int core_clash_port = 0;
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
        int language = 4; // Русский, см. NekoGui.cpp
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
        // Порядок правил чинится своим флагом: у тех, кто уже обновлялся,
        // предыдущий выставлен, и общий проход к ним второй раз не придёт.
        bool routing_games_migrated = false;
        // Третий флаг — по той же причине, что и второй. Шаблон пути расширен
        // с «-Win64-Shipping» до «-Shipping», чтобы ловить и лаунчер игры, а не
        // только её саму. Проход по схемам дописывает недостающие значения, но
        // ходит он ровно один раз за флаг: без нового флага расширенный шаблон
        // достался бы только тем, кто ставит клиент с нуля.
        bool routing_launcher_migrated = false;

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
         * Показывали ли человеку объяснение режимов на macOS.
         *
         * Смысл настройки — объяснить ОДИН раз. Спрашивать одним и тем же окном
         * при каждом запуске значит приучить закрывать его не читая, а тогда
         * объяснение не работает вовсе. Открыть заново можно пунктом меню.
         *
         * На остальных платформах не читается: выбора там нет.
         */
        bool macos_mode_explained = false;

        /**
         * Сети, которые НЕ заводить в туннель, — сверх встроенного списка.
         *
         * Не путать с vpn_rule_cidr: то кладёт правило в route.rules и влияет на
         * выбор выхода ВНУТРИ туннеля. Здесь речь о таблице маршрутов
         * операционной системы: перечисленное сюда туннель не перехватывает
         * вовсе, и потому оно остаётся доступно ровно так же, как до включения.
         *
         * Заполняется человеком, а не нами. Подставлять найденные чужие туннели
         * автоматически нельзя: полнотуннельный чужой VPN владеет половиной
         * адресного пространства, и наивное «нашёл — исключил» выключило бы обход
         * для половины интернета. Кнопка рядом с полем показывает найденное, а
         * решает человек.
         */
        QString vpn_route_exclude_extra = "";

        bool vpn_rule_white = false;
        QString vpn_rule_process = "";
        QString vpn_rule_cidr = "";

        /**
         * Программы, которые идут ЧЕРЕЗ туннель, даже если пресет уводит их мимо.
         *
         * Обратная сторона vpn_rule_process, и до сих пор её не было: список
         * человека умел только выводить из-под защиты. Загнать одну игру в
         * туннель — например ту, чьи серверы фильтруются, — можно было лишь
         * правкой файла схемы руками. Правило по имени кладётся первым и потому
         * побеждает и пресет, и списки доменов.
         */
        QString vpn_rule_process_proxy = "";

        /**
         * Все игры — через туннель, а не мимо.
         *
         * По умолчанию пресет отправляет игры и их помощников мимо туннеля: пинг
         * ниже, адрес российский, античит спокоен. Но бывает наоборот — серверы
         * игры фильтруются у провайдера, и мимо туннеля она не работает вовсе.
         * Переключатель разворачивает ВСЕ правила обхода пресета по процессам в
         * правила «через туннель», не переписывая их: список один, расходиться
         * нечему. Поимённый список человека решает раньше и здесь.
         */
        bool games_via_tunnel = false;

        /**
         * Обход фильтрации на ПРЯМОМ пути — средствами ядра, без драйвера.
         *
         * Половина трафика по замыслу остаётся на российском адресе: игры,
         * античиты, банки. Когда провайдер режет им рукопожатие TLS по имени
         * сервера, туннель не инструмент — он увёл бы их за границу. Ядро умеет
         * дробить ClientHello (tls_record_fragment, tls_fragment) — это тот же
         * приём, что у внешних обходов, только внутри нашего процесса: ни
         * кернел-драйвера, ни чужого бинаря, ни конфликта с «Починить сеть».
         *
         * Честная граница, словами самого ядра: приём против простых фильтров
         * по открытому тексту, а не против настоящей цензуры. Если не помог —
         * следующий ярус отдельным решением, не молча.
         */
        bool dpi_fragment = false;

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
