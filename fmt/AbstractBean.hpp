#pragma once

#include <QJsonObject>
#include <QJsonArray>

#include "main/NekoGui.hpp"

namespace NekoGui_fmt {
    struct CoreObjOutboundBuildResult {
    public:
        QJsonObject outbound;
        QString error;
    };

    struct ExternalBuildResult {
    public:
        QString program;
        QStringList env;
        QStringList arguments;
        //
        QString tag;
        //
        QString error;
        QString config_export;

        /**
         * Переменные, которые надо УБРАТЬ из унаследованного окружения.
         *
         * Нужно процессам, которые сами ходят наружу: унаследованный HTTPS_PROXY
         * отправит их через прокси, а если этот прокси — наш же туннель,
         * получится петля. Выглядит она как «подключилось и намертво встало».
         */
        QStringList env_unset;

        /*
         * Здесь стоял признак `env_secret` — «не печатать окружение этого
         * процесса». Он удалён, и это не упрощение, а исправление.
         *
         * Признак делил ядра на те, чьё окружение секретно, и остальные, — и
         * ровно это деление оказалось ложным. После перехода на СЛИЯНИЕ
         * окружений (mainwindow_grpc.cpp) любой внешний процесс получает
         * окружение пользователя целиком, а «остальные» печатали его в журнал
         * со значениями: сотня переменных, среди них USERNAME, USERPROFILE и
         * всё, что человек держит в окружении рабочей машины.
         *
         * Утечку пережил и разбор — потому что флаг читался как «резерв
         * защищён», и вопрос «а остальные?» просто не возникал. Теперь
         * окружение не печатается ни у кого, всегда, и выбирать нечего:
         * см. ExternalProcess::Start().
         */

        /**
         * Чем заменить аргументы в журнале. Пусто — печатать как есть.
         *
         * Нужно там, где секрет живёт В САМОМ аргументе и убрать его оттуда
         * нельзя. Такой ровно один: у naive пароль входит в --proxy=, потому
         * что этого требует его формат. QUrl::FullyEncoded кодирует, но не
         * удаляет, и в журнал уходит `--proxy=https://user:пароль@host:443`.
         *
         * Поле отдельное, а не «не печатать аргументы вовсе»: у остальных ядер
         * аргументы — это то, чем поддержка сегодня чинит подключения, и
         * молчание там обошлось бы дороже утечки, которой у них нет.
         */
        QString args_display;

        /**
         * Что делать с выводом процесса.
         *
         * 0 — как было: всё, что процесс написал, идёт в журнал. Верно для xray
         *     и naive — там адреса, которые человек добавил сам.
         * 1 — в журнал идут только разобранные вехи (main/RelayTrace.hpp), и
         *     фраза собирается заново из чисел. Остальное молча отбрасывается.
         */
        int log_policy = 0;
    };

    class AbstractBean : public JsonStore {
    public:
        int version;

        QString name = "";
        QString serverAddress = "127.0.0.1";
        int serverPort = 1080;

        QString custom_config = "";
        QString custom_outbound = "";

        explicit AbstractBean(int version);

        //

        QString ToNekorayShareLink(const QString &type);

        void ResolveDomainToIP(const std::function<void()> &onFinished);

        //

        [[nodiscard]] virtual QString DisplayAddress();

        [[nodiscard]] virtual QString DisplayName();

        virtual QString DisplayCoreType() { return software_core_name; };

        virtual QString DisplayType() { return {}; };

        virtual QString DisplayTypeAndName();

        //

        virtual int NeedExternal(bool isFirstProfile) { return 0; };

        virtual CoreObjOutboundBuildResult BuildCoreObjSingBox() { return {}; };

        virtual ExternalBuildResult BuildExternal(int mapping_port, int socks_port, int external_stat) { return {}; };

        virtual QString ToShareLink() { return {}; };
    };

} // namespace NekoGui_fmt
