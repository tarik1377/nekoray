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

        /**
         * Не печатать окружение в журнал.
         *
         * Журнал пишется в файл и уезжает в поддержку (collect-report.ps1).
         * Скрыть ЗНАЧЕНИЯ здесь мало: имя переменной само рассказывает, из чего
         * сложено подключение. Поэтому при этом признаке в журнал идёт только
         * число переменных.
         */
        bool env_secret = false;

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
