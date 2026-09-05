#pragma once

#include "dpi/DpiPlan.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace NekoGui_sys {
    class ExternalProcess;
}

/**
 * Модуль обхода: winws под нашим управлением.
 *
 * ЗАДАЧА ОДНА — грамотно разделять. Запускать обход, когда он нужен и
 * безопасен; останавливать, когда он мешает другому или другой мешает ему;
 * не оставлять после себя ничего, что живёт без хозяина.
 *
 * ПРАВИЛА, КАЖДОЕ ИЗ КОТОРЫХ ЧЕМ-ТО ОПЛАЧЕНО.
 *
 *  1. Управляем ДРАЙВЕРОМ, а не процессом. Античиты бьют по резидентному
 *     WinDivert, а не по winws: Vanguard находит его после закрытия zapret,
 *     EAC пишет «driver loaded but file missing». Любой выход winws — не
 *     перезапуск, а немедленная выгрузка драйвера. Ни одного пути, где драйвер
 *     остаётся в памяти без нас.
 *
 *  2. Не стартуем при античите и гасимся при его появлении. EAC с лета 2026
 *     закрывает winws и выдаёт отстранения; Vanguard и BattlEye — сообщения о
 *     банах. Режима «предупредить и включить» нет как класса: у человека нет
 *     данных для этого решения, а бан ложится на его аккаунт.
 *
 *  3. Не запускаем второй winws рядом с чужим. Два дескриптора с одним
 *     приоритетом на пересекающемся фильтре дают неопределённый порядок, чужая
 *     версия драйвера — DRIVER_FAILED_PRIOR_UNLOAD. Отказ вслух, ничего чужого
 *     не удаляем: для этого есть «Починить сеть», и она теперь отличает своё.
 *
 *  4. Ярус ядра и winws не работают одновременно. Дробление в ядре режет
 *     приветствие внутри имени сервера; winws видит обрезанное SNI и по
 *     умолчанию отпускает пакет нетронутым. Пока модуль работает, ядро не
 *     дробит — за этим следит ConfigBuilder по dpi_module_active.
 *
 *  5. Смотрим только на физический адаптер (--wf-iface). Пакеты игры, уходящие
 *     в наш туннель, winws не видит. Наш собственный туннель к серверу — в
 *     исключениях порчи, не захвата.
 *
 *  6. winws живёт в Job-объекте с KILL_ON_JOB_CLOSE: умер клиент — умер winws.
 *     Драйвер при этом останется до перезагрузки, и это единственная дыра,
 *     которую закрывает только отдельный сторож; он в следующем шаге.
 *
 * Модуль ничего не знает об окне: принимает состояние, отдаёт сигнал.
 */
namespace GreenRhythm::Dpi {

    class DpiModule : public QObject {
        Q_OBJECT

    public:
        enum class State {
            Off,        ///< выключен человеком
            NoBundle,   ///< включён, но файлов модуля нет
            Installing, ///< качаем и проверяем
            NoAdmin,    ///< без прав администратора драйвер не поднять
            Foreign,    ///< на машине чужой перехватчик
            Blocked,    ///< запущен античит из списка
            Starting,
            Running,
            Failed,     ///< winws не поднялся или упал; причина в reason()
        };

        explicit DpiModule(QObject *parent = nullptr);
        ~DpiModule() override;

        State state() const { return st; }
        QString reason() const { return why; }
        /** Одной строкой для фишки и панели. */
        QString stateText() const;
        bool running() const { return st == State::Running; }

        /**
         * Свести желаемое с действительным. Идемпотентна и дешева: зовётся с
         * обновлением состояния окна, действует только на переходах.
         *
         * @param enabled      человек включил модуль
         * @param serverHost   адрес сервера профиля, если подключены; пусто — нет
         */
        void reconcile(bool enabled, const QString &serverHost);

        /** Перед выходом клиента: погасить winws и выгрузить драйвер. */
        void shutdown();

        /** Скачать файлы модуля. Блокирующая, звать из рабочего потока. */
        bool install(QString *error);

        /** Отпечаток плана, с которым работает winws сейчас. */
        QString currentPlanHash() const { return planHash; }

        /** Программы из списка античитов, запущенные сейчас. */
        static QStringList antiCheatsRunning(const QStringList &programs);

    signals:
        void stateChanged();
        /** Ядру надо пересобрать конфиг: модуль встал или остановился. */
        void coreRestartWanted();

    private:
        void setState(State s, const QString &reason = QString());
        void start(const QString &serverHost);
        void stop(const QString &reason, State next);
        void stopProcess();
        void unloadDriver();
        void assignToJob(qint64 pid);
        bool foreignInterceptor(QString *what) const;
        int physicalIfIndex() const;
        Plan buildCurrentPlan(const QString &serverHost);
        bool writeLists(const Plan &plan);
        void ensureServerIps(const QString &host);
        void tick();
        static bool elevated();
        static QStringList literalIp(const QString &host);

        State st = State::Off;
        QString why;
        QString planHash;
        QString serverForPlan;
        QString lastOutput;
        int ifIndexForPlan = 0;
        /// Адреса сервера профиля для --ipset-exclude и имя, для которого они получены.
        QStringList serverIps;
        QString resolvedHost;
        NekoGui_sys::ExternalProcess *proc = nullptr;
        QTimer *guard = nullptr;   ///< тикает всё время, пока модуль включён
        bool wanted = false;       ///< последнее желание человека
        QString wantedHost;        ///< последний известный адрес сервера
        bool driverTouched = false;///< winws поднимался — значит, есть что выгружать
        bool inTick = false;       ///< защита от повторного входа: setState будит окно, окно зовёт reconcile
        void *job = nullptr;       ///< HANDLE Job-объекта (Windows)
        qint64 startedAtMs = 0;
        int crashesInRow = 0;
        bool stopping = false;     ///< гасим сами — обработчик выхода не должен считать это падением
    };

} // namespace GreenRhythm::Dpi
