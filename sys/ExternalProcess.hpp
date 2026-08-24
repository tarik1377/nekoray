#pragma once

#include <memory>
#include <QProcess>

namespace NekoGui_sys {
    class ExternalProcess : public QProcess {
    public:
        QString tag;
        QString program;
        QStringList arguments;
        QStringList env;

        /** Чем заменить аргументы в журнале — см. ExternalBuildResult::args_display. */
        QString args_display;

        /** Что делать с выводом процесса — см. ExternalBuildResult::log_policy. */
        int log_policy = 0;

        bool managed = true; // MW_dialog_message

        ExternalProcess();
        ~ExternalProcess();

        // start & kill is one time

        virtual void Start();

        void Kill();

    protected:
        /** Вывод под политикой log_policy=1 — только разобранные вехи. */
        void ShowFilteredLog(const QByteArray &raw);

        bool started = false;
        bool killed = false;
        bool crashed = false;
    };

    class CoreProcess : public ExternalProcess {
    public:
        CoreProcess(const QString &core_path, const QStringList &args);

        void Start() override;

        void Restart();

        int start_profile_when_core_is_up = -1;

    private:
        bool show_stderr = false;
        bool failed_to_start = false;
        bool restarting = false;
    };

    // 手动管理
    inline std::list<std::shared_ptr<ExternalProcess>> running_ext;

    inline QAtomicInt logCounter;
} // namespace NekoGui_sys
