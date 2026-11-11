#include "ExternalProcess.hpp"
#include "main/NekoGui.hpp"
#include "main/RelayTrace.hpp"
#include "main/StartupPolicy.hpp"

#include <QTimer>
#include <QDir>
#include <QApplication>
#include <QElapsedTimer>

namespace NekoGui_sys {

    ExternalProcess::ExternalProcess() : QProcess() {
        // qDebug() << "[Debug] ExternalProcess()" << this << running_ext;
        this->env = QProcessEnvironment::systemEnvironment().toStringList();
    }

    ExternalProcess::~ExternalProcess() {
        // qDebug() << "[Debug] ~ExternalProcess()" << this << running_ext;
    }

    /**
     * Вывод процесса, которому нельзя говорить своими словами.
     *
     * Разбирается ПОСТРОЧНО и по одной причине: труба отдаёт куски произвольной
     * длины, и веха может приехать склеенной с посторонней строкой. Пропустив
     * такой кусок целиком, мы бы вынесли в журнал ровно то, ради недопущения
     * чего всё и затевалось.
     *
     * Строка, которая не разобралась, отбрасывается молча. Показать её «на
     * всякий случай» нельзя: непонятая строка — это в точности тот случай,
     * когда неизвестно, что в ней написано.
     */
    void ExternalProcess::ShowFilteredLog(const QByteArray &raw) {
        const auto text = QString::fromUtf8(raw);
        for (const auto &line: text.split('\n')) {
            const auto said = RelayTrace::Line(line);
            if (!said.isEmpty()) MW_show_log_ext(tag, said);
        }
    }

    void ExternalProcess::Start() {
        if (started) return;
        started = true;

        if (managed) {
            connect(this, &QProcess::readyReadStandardOutput, this, [&]() {
                auto log = readAllStandardOutput();
                if (logCounter.fetchAndAddRelaxed(log.count("\n")) > NekoGui::dataStore->max_log_line) return;
                if (log_policy == 1) {
                    ShowFilteredLog(log);
                    return;
                }
                MW_show_log_ext_vt100(log);
            });
            connect(this, &QProcess::readyReadStandardError, this, [&]() {
                auto log = readAllStandardError().trimmed();
                if (log_policy == 1) {
                    ShowFilteredLog(log);
                    return;
                }
                MW_show_log_ext_vt100(log);
            });
            connect(this, &QProcess::errorOccurred, this, [&](QProcess::ProcessError error) {
                if (!killed) {
                    crashed = true;
                    MW_show_log_ext(tag, "errorOccurred:" + errorString());
                    MW_dialog_message("ExternalProcess", "Crashed");
                }
            });
            connect(this, &QProcess::stateChanged, this, [&](QProcess::ProcessState state) {
                if (state == QProcess::NotRunning) {
                    if (killed) { // 用户命令退出
                        MW_show_log_ext(tag, "External core stopped");
                    } else if (!crashed) { // 异常退出
                        crashed = true;
                        MW_show_log_ext(tag, "[Error] Program exited accidentally: " + errorString());
                        Kill();
                        MW_dialog_message("ExternalProcess", "Crashed");
                    }
                }
            });
            /*
             * ОКРУЖЕНИЕ НЕ ПЕЧАТАЕТСЯ НИКОГДА И НИ У КОГО — только его размер.
             *
             * Здесь стояло `env_secret ? число : env.join(" ")`, и до перехода
             * на слияние окружений это было почти безобидно: `env` содержал
             * ровно то, что положил профиль, — у xray и hysteria2 пусто, у naive
             * одну SSL_CERT_FILE. Слияние (mainwindow_grpc.cpp) починило запуск
             * внешних ядер и ровно этой же строкой превратило запись в свалку
             * всего окружения пользователя: сотня переменных со значениями, а
             * среди них USERNAME, USERPROFILE и всё, что человек держит в
             * окружении рабочей машины, — GITHUB_TOKEN, AWS_SECRET_ACCESS_KEY.
             *
             * Журнал пишется в файл, переживает перезапуск, и пункт меню
             * «Сохранить лог в файл…» прямо предлагает отправить его в
             * поддержку. То есть утечка не гипотетическая: она уходит по почте
             * человеком, который делает ровно то, о чём его попросили.
             *
             * Признак `env_secret`, делавший это выборочно, удалён вместе с
             * условием. Он делил ядра на «секретные» и остальные, и именно это
             * деление позволило утечке пережить вычитку: флаг читался как
             * «резерв защищён», и вопрос про остальных не возникал.
             */
            const auto shownEnv = QString("env: %1 var(s)").arg(env.size());

            /*
             * АРГУМЕНТЫ У NAIVE НЕСУТ ПАРОЛЬ, и это не наша выдумка, а его
             * формат: --proxy=https://user:пароль@host:443. QUrl::FullyEncoded
             * кодирует, но не удаляет — обычный пароль виден как есть.
             *
             * Поэтому строится отдельное представление для журнала. Подменять
             * сами `arguments` нельзя: это те же строки, что уходят в QProcess,
             * и профиль перестал бы подключаться. Пустой args_display означает
             * «показывать как есть» — так остаются видны аргументы xray и
             * остальных, которыми поддержка сегодня чинит подключения.
             */
            const auto shownArgs = args_display.isEmpty() ? arguments.join(" ") : args_display;
            MW_show_log_ext(tag, "External core starting: " + shownEnv + " " + program + " " + shownArgs);
        }

        QProcess::setEnvironment(env);
        QProcess::start(program, arguments);
    }

    void ExternalProcess::Kill() {
        if (killed) return;
        killed = true;

        if (!crashed) {
            QProcess::kill();
            QProcess::waitForFinished(500);
        }
    }

    //

    QElapsedTimer coreRestartTimer;

    CoreProcess::CoreProcess(const QString &core_path, const QStringList &args) : ExternalProcess() {
        ExternalProcess::managed = false;
        ExternalProcess::program = core_path;
        ExternalProcess::arguments = args;

        connect(this, &QProcess::started, this, [&]() {
            MW_show_log(QStringLiteral("[Startup] core process created after %1 ms.").arg(startup_timer.elapsed()));
        });
        connect(this, &QProcess::readyReadStandardOutput, this, [&]() {
            auto log = readAllStandardOutput();
            if (!first_stdout && !log.isEmpty()) {
                first_stdout = true;
                MW_show_log(QStringLiteral("[Startup] core first stdout after %1 ms.").arg(startup_timer.elapsed()));
            }
            if (!retiring && !NekoGui::dataStore->core_running) {
                const auto readiness = readiness_tail + log;
                readiness_tail = readiness.right(128);
                if (readiness.contains("grpc server listening")) {
                    // The core really started
                    NekoGui::dataStore->core_running = true;
                    MW_show_log(QStringLiteral("[Startup] core RPC ready after %1 ms.").arg(startup_timer.elapsed()));
                    if (start_profile_when_core_is_up >= 0) {
                        MW_dialog_message("ExternalProcess", "CoreStarted," + Int2String(start_profile_when_core_is_up));
                        start_profile_when_core_is_up = -1;
                    }
                } else if (readiness.contains("failed to serve")) {
                    // The core failed to start
                    QProcess::kill();
                }
            }
            if (logCounter.fetchAndAddRelaxed(log.count("\n")) > NekoGui::dataStore->max_log_line) return;
            MW_show_log(log);
        });
        connect(this, &QProcess::readyReadStandardError, this, [&]() {
            auto log = readAllStandardError().trimmed();
            if (!first_stderr && !log.isEmpty()) {
                first_stderr = true;
                MW_show_log(QStringLiteral("[Startup] core first stderr after %1 ms.").arg(startup_timer.elapsed()));
            }
            if (show_stderr) {
                MW_show_log(log);
                return;
            }
            if (log.contains("token is set")) {
                show_stderr = true;
            }
        });
        connect(this, &QProcess::errorOccurred, this, [&](QProcess::ProcessError error) {
            if (error == QProcess::ProcessError::FailedToStart) {
                failed_to_start = true;
                start_profile_when_core_is_up = -1;
                MW_show_log("start core error occurred: " + errorString() + "\n");
                MW_dialog_message("ExternalProcess", "CoreStartFailed");
            }
        });
        connect(this, &QProcess::stateChanged, this, [&](QProcess::ProcessState state) {
            if (state == QProcess::NotRunning) {
                NekoGui::dataStore->core_running = false;
            }

            if (!NekoGui::dataStore->prepare_exit && state == QProcess::NotRunning) {
                if (failed_to_start) return; // no retry
                if (restarting) return;

                MW_dialog_message("ExternalProcess", "CoreCrashed");

                // Retry rate limit
                if (coreRestartTimer.isValid()) {
                    if (coreRestartTimer.restart() < 10 * 1000) {
                        coreRestartTimer = QElapsedTimer();
                        MW_show_log("[Error] " + QObject::tr("Core exits too frequently, stop automatic restart this profile."));
                        return;
                    }
                } else {
                    coreRestartTimer.start();
                }

                // Restart
                start_profile_when_core_is_up = NekoGui::dataStore->started_id;
                MW_show_log("[Error] " + QObject::tr("Core exited, restarting."));
                const auto crashedGeneration = generation();
                setTimeout([=] { if (generation() == crashedGeneration) Restart(); }, this, 1000);
            }
        });
    }

    void CoreProcess::Start() {
        if (started) return;
        ++process_generation;
        readiness_tail.clear();
        show_stderr = false;
        failed_to_start = false;
        first_stdout = false;
        first_stderr = false;
        startup_timer.start();
        // cwd: same as GUI, at ./config
        ExternalProcess::Start();
        write((NekoGui::dataStore->core_token + "\n").toUtf8());
        const auto startingGeneration = generation();
        // Cold Windows process/device initialization can exceed 15 seconds.
        // Keep a finite limit without killing a valid slow startup prematurely.
        setTimeout([=] {
            if (generation() != startingGeneration || NekoGui::dataStore->core_running || failed_to_start || NekoGui::dataStore->prepare_exit) return;
            MW_show_log(QStringLiteral("[Startup] core readiness timeout; first_stdout=%1 first_stderr=%2 after_ms=%3")
                            .arg(first_stdout ? 1 : 0).arg(first_stderr ? 1 : 0).arg(startup_timer.elapsed()));
            // A cold Windows launch can stall before the first core output.
            // Preserve the queued connection through one replacement, then fail
            // normally. Never retry a cancelled connection or loop indefinitely.
#ifdef Q_OS_WIN
            if (!readiness_recovery_used && start_profile_when_core_is_up >= 0) {
                readiness_recovery_used = true;
                MW_show_log(QObject::tr("Ядро не готово за 60 секунд — выполняю один повторный запуск."));
                Restart();
                return;
            }
#endif
            failed_to_start = true;
            start_profile_when_core_is_up = -1;
            MW_show_log(QObject::tr("Ядро не открыло канал управления за %1 секунд.")
                            .arg(GreenRhythm::StartupPolicy::coreReadyMs / 1000));
            MW_dialog_message("ExternalProcess", "CoreStartFailed");
            QProcess::kill();
        }, this, GreenRhythm::StartupPolicy::coreReadyMs);
    }

    void CoreProcess::Restart() {
        restarting = true;
        // waitForFinished drains old stdout. It must not publish CoreStarted
        // for the process being retired or consume the replacement's queue.
        retiring = true;
        QProcess::kill();
        QProcess::waitForFinished(500);
        if (state() != QProcess::NotRunning) {
            failed_to_start = true;
            start_profile_when_core_is_up = -1;
            MW_show_log(QObject::tr("Не удалось остановить прежнее ядро; повторный запуск отменён."));
            MW_dialog_message("ExternalProcess", "CoreStartFailed");
            restarting = false;
            return;
        }
        retiring = false;
        ExternalProcess::started = false;
        Start();
        restarting = false;
    }

} // namespace NekoGui_sys
