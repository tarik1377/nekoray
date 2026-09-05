// СИСТЕМНЫЕ ЗАГОЛОВКИ — ПЕРВЫМИ, И ЭТО НЕ ВКУСОВЩИНА.
//
// rpcndr.h объявляет «typedef unsigned char boolean», а у нас есть свой
// boolean — из перечисления itemType в NekoGui.hpp. Включи мы наш заголовок
// раньше, и компилятор споткнётся не у нас, а В СЕРЕДИНЕ urlmon.h: «boolean:
// неоднозначный символ», две сотни строк ошибок в чужих файлах и ни одной
// ссылки на наш код.
//
// Внутри: winsock2 строго до windows.h, иначе windows.h втянет winsock 1 и
// iphlpapi не соберётся.
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#endif

#include "dpi/DpiModule.hpp"

#include "dpi/DpiBundle.hpp"
#include "dpi/DpiCatalog.hpp"
#include "dpi/DpiPlan.hpp"
#include "main/NekoGui.hpp"
#include "main/RunningPrograms.hpp"
#include "sys/ExternalProcess.hpp"

#include <QDateTime>
#include <QScopeGuard>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>

namespace GreenRhythm::Dpi {

    namespace {
        // Античиты, при которых модуль не стартует и гасится. Только те, про
        // которые есть свидетельства (EAC, Vanguard, BattlEye) или про которые
        // нет никаких (Elytra) — второе не повод рисковать чужим аккаунтом.
        const QStringList kAntiCheat{
            QStringLiteral("EasyAntiCheat.exe"), QStringLiteral("EasyAntiCheat_EOS.exe"),
            QStringLiteral("EACLauncher.exe"), QStringLiteral("BEService.exe"),
            QStringLiteral("BEService_x64.exe"), QStringLiteral("vgc.exe"), QStringLiteral("vgtray.exe"),
        };
        const QStringList kForeign{
            QStringLiteral("winws.exe"), QStringLiteral("goodbyedpi.exe"), QStringLiteral("ciadpi.exe"),
            QStringLiteral("byedpi.exe"), QStringLiteral("proxifyre.exe"), QStringLiteral("spoofdpi.exe"),
        };

        QString listDir() {
            // Рабочий каталог клиента — это каталог настроек (main.cpp), так что
            // списки ложатся рядом с профилями, а не рядом с бинарями.
            return QDir::current().absoluteFilePath(QStringLiteral("dpi"));
        }

        bool writeAtomic(const QString &path, const QString &text) {
            QSaveFile f(path);
            if (!f.open(QIODevice::WriteOnly)) return false;
            f.write(text.toUtf8());
            return f.commit();
        }
    } // namespace

    DpiModule::DpiModule(QObject *parent) : QObject(parent) {
        // Сторож тикает ВСЁ ВРЕМЯ, пока модуль включён, а не только пока winws
        // жив. Иначе состояние Blocked («идёт игра с античитом») стало бы
        // тупиком: человек вышел из игры, а обход не вернулся, и понять почему
        // нечем. Обратно из Blocked и Foreign выводит именно этот тик.
        guard = new QTimer(this);
        guard->setInterval(5000);
        connect(guard, &QTimer::timeout, this, [this] { tick(); });
    }

    DpiModule::~DpiModule() {
#ifdef Q_OS_WIN
        if (job != nullptr) CloseHandle(static_cast<HANDLE>(job));
#endif
    }

    QString DpiModule::stateText() const {
        switch (st) {
            case State::Off: return QStringLiteral("выключен");
            case State::NoBundle: return QStringLiteral("файлы модуля не скачаны");
            case State::Installing: return QStringLiteral("скачивается…");
            case State::NoAdmin: return QStringLiteral("нужны права администратора");
            case State::Foreign: return QStringLiteral("на машине чужой обход: %1").arg(why);
            case State::Blocked: return QStringLiteral("остановлен: %1").arg(why);
            case State::Starting: return QStringLiteral("запускается…");
            case State::Running: return QStringLiteral("работает: %1").arg(why);
            case State::Failed: return QStringLiteral("не запустился: %1").arg(why);
        }
        return {};
    }

    void DpiModule::setState(State s, const QString &reason) {
        if (st == s && why == reason) return;
        st = s;
        why = reason;
        emit stateChanged();
    }

    QStringList DpiModule::antiCheatsRunning(const QStringList &programs) {
        QStringList hit;
        for (const auto &p: programs) {
            for (const auto &ac: kAntiCheat) {
                if (p.compare(ac, Qt::CaseInsensitive) == 0) hit << p;
            }
            // Имя процесса Elytra заранее неизвестно, известно только слово.
            if (p.contains(QStringLiteral("elytra"), Qt::CaseInsensitive)) hit << p;
        }
        hit.removeDuplicates();
        return hit;
    }

    bool DpiModule::foreignInterceptor(QString *what) const {
        const bool ours = proc != nullptr && proc->state() != QProcess::NotRunning;
        for (const auto &p: runningPrograms()) {
            for (const auto &f: kForeign) {
                if (p.compare(f, Qt::CaseInsensitive) != 0) continue;
                if (ours && p.compare(QStringLiteral("winws.exe"), Qt::CaseInsensitive) == 0) continue;
                if (what != nullptr) *what = p;
                return true;
            }
        }
#ifdef Q_OS_WIN
        // Служба WinDivert с чужим путём — чужая версия драйвера в памяти.
        QProcess sc;
        sc.start(QStringLiteral("sc.exe"), {QStringLiteral("qc"), QStringLiteral("WinDivert")});
        if (sc.waitForFinished(5000) && sc.exitCode() == 0) {
            const auto out = QString::fromLocal8Bit(sc.readAllStandardOutput()).toLower();
            const auto own = QDir::toNativeSeparators(bundleDir()).toLower();
            if (out.contains(QStringLiteral("binary_path_name")) && !out.contains(own)) {
                if (what != nullptr) *what = QStringLiteral("драйвер WinDivert не из нашего каталога");
                return true;
            }
        }
#endif
        return false;
    }


    int DpiModule::physicalIfIndex() const {
#ifdef Q_OS_WIN
        // Индекс адаптера, за которым стоит НАСТОЯЩИЙ выход в интернет.
        //
        // Спрашивать систему «куда пойдёт пакет на 8.8.8.8» здесь нельзя.
        // sing-tun не забирает 0.0.0.0/0 — он уводит интернет полусотней кусков
        // (0.0.0.0/5, 32.0.0.0/3, …) с метрикой 0, — и при поднятом туннеле
        // любой такой вопрос вернёт neko-tun. А winws на туннельном адаптере
        // бесполезен: наружу оттуда идёт уже зашифрованный поток к серверу.
        //
        // Поэтому адаптер выбирается по свойствам, а не по маршруту: поднят,
        // не петля, не туннель, со шлюзом, и не из известных виртуальных. Из
        // оставшихся — с наименьшей метрикой: так система выбрала бы и сама,
        // не будь туннеля.
        ULONG size = 15 * 1024;
        QByteArray buf;
        PIP_ADAPTER_ADDRESSES aa = nullptr;
        for (int attempt = 0; attempt < 3; ++attempt) {
            buf.resize(static_cast<int>(size));
            auto *candidate = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
            const ULONG rc = GetAdaptersAddresses(AF_INET,
                                                  GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST
                                                      | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                                  nullptr, candidate, &size);
            if (rc == NO_ERROR) {
                aa = candidate;
                break;
            }
            if (rc != ERROR_BUFFER_OVERFLOW) return 0;
        }
        if (aa == nullptr) return 0;

        // Виртуальные адаптеры иногда объявляют шлюз и метрику не хуже живых.
        static const QStringList virt{
            QStringLiteral("wintun"), QStringLiteral("neko-tun"), QStringLiteral("tap-windows"),
            QStringLiteral("tap adapter"), QStringLiteral("wireguard"), QStringLiteral("openvpn"),
            QStringLiteral("hyper-v"), QStringLiteral("vethernet"), QStringLiteral("virtualbox"),
            QStringLiteral("vmware"), QStringLiteral("loopback"), QStringLiteral("teredo"),
        };

        int best = 0;
        ULONG bestMetric = ULONG_MAX;
        for (auto *p = aa; p != nullptr; p = p->Next) {
            if (p->OperStatus != IfOperStatusUp) continue;
            if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK || p->IfType == IF_TYPE_TUNNEL) continue;
            if (p->FirstGatewayAddress == nullptr) continue;
            const auto text = (QString::fromWCharArray(p->Description) + QChar(' ')
                               + QString::fromWCharArray(p->FriendlyName))
                                  .toLower();
            bool skip = false;
            for (const auto &v: virt) {
                if (text.contains(v)) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
            if (p->Ipv4Metric < bestMetric) {
                bestMetric = p->Ipv4Metric;
                best = static_cast<int>(p->IfIndex);
            }
        }
        return best;
#else
        return 0;
#endif
    }

    bool DpiModule::elevated() {
#ifdef Q_OS_WIN
        // Драйвер поднимает сам winws, а запустить его повышенным из обычного
        // процесса нельзя так, чтобы он остался в нашем Job-объекте и отдавал
        // нам свой вывод. Значит, повышенным должен быть клиент — и об этом
        // человеку говорится прямо, а не «не удалось запустить».
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        TOKEN_ELEVATION info{};
        DWORD got = 0;
        const bool ok = GetTokenInformation(token, TokenElevation, &info, sizeof(info), &got) != 0;
        CloseHandle(token);
        return ok && info.TokenIsElevated != 0;
#else
        return false;
#endif
    }

    QStringList DpiModule::literalIp(const QString &host) {
        QHostAddress a;
        if (!a.setAddress(host)) return {};
        return {host
                + (a.protocol() == QAbstractSocket::IPv6Protocol ? QStringLiteral("/128") : QStringLiteral("/32"))};
    }

    void DpiModule::ensureServerIps(const QString &host) {
        if (resolvedHost == host) return;
        resolvedHost = host;
        serverIps = literalIp(host);
        if (!serverIps.isEmpty() || host.isEmpty()) return;

        // Имя сервера разрешается ФОНОМ. Синхронный запрос здесь морозил бы окно
        // на время таймаута DNS — а это ровно та минута, когда человек нажал
        // «подключить» и смотрит на клиент.
        QHostInfo::lookupHost(host, this, [this, host](const QHostInfo &info) {
            if (resolvedHost != host) return;
            QStringList found;
            for (const auto &a: info.addresses()) {
                found << a.toString()
                             + (a.protocol() == QAbstractSocket::IPv6Protocol ? QStringLiteral("/128")
                                                                             : QStringLiteral("/32"));
            }
            if (found == serverIps) return;
            serverIps = found;
            // План изменился — пересобрать, но только если модуль работает.
            if (st == State::Running || st == State::Starting) start(serverForPlan);
        });
    }

    Plan DpiModule::buildCurrentPlan(const QString &serverHost) {
        PlanInput in;
        in.ifIndex = physicalIfIndex();
        in.binDir = bundleDir();
        in.listDir = listDir();

        const auto *strategy = findStrategy(NekoGui::dataStore->dpi_module_strategy);
        if (strategy == nullptr) strategy = &catalog().first();
        in.strategy = *strategy;

        // Защищаем ровно то, что и так идёт напрямую: список «мимо туннеля» из
        // схемы маршрутов. Всё, что уходит в туннель, winws не увидит и трогать
        // не должен — там уже другой обход.
        QStringList uncovered;
        in.hosts = hostsFromDirectDomains(NekoGui::dataStore->routing->direct_domain, &uncovered);
        for (const auto &raw: NekoGui::dataStore->routing->direct_ip.split(QChar('\n'), Qt::SkipEmptyParts)) {
            const auto line = raw.trimmed();
            if (line.isEmpty() || line.startsWith(QChar('#'))) continue;
            if (line.startsWith(QStringLiteral("geoip:"))) {
                uncovered << line;
                continue;
            }
            in.ips << (line.contains(QChar('/')) ? line : line + QStringLiteral("/32"));
        }

        ensureServerIps(serverHost);
        in.excludeIps = serverIps;

        auto plan = buildPlan(in);
        plan.uncovered = uncovered;
        return plan;
    }

    bool DpiModule::writeLists(const Plan &plan) {
        QDir().mkpath(listDir());
        return writeAtomic(plan.hostlistPath, plan.hostlistText) && writeAtomic(plan.ipsetPath, plan.ipsetText)
               && writeAtomic(plan.excludePath, plan.excludeText);
    }

    void DpiModule::start(const QString &serverHost) {
#ifndef Q_OS_WIN
        Q_UNUSED(serverHost)
        setState(State::Failed, QStringLiteral("модуль есть только под Windows"));
#else
        // Порядок проверок — от той, из-за которой вообще нельзя, к той, из-за
        // которой нечем.
        // Отмечаем попытку СРАЗУ: по этой отметке tick отличает «отказ уже
        // разобран» от «сервер сменился, стоит попробовать снова».
        serverForPlan = serverHost;

        const auto ac = antiCheatsRunning(runningPrograms());
        if (!ac.isEmpty()) {
            stop(QStringLiteral("запущен %1").arg(ac.first()), State::Blocked);
            return;
        }
        QString foreign;
        if (foreignInterceptor(&foreign)) {
            stop(foreign, State::Foreign);
            return;
        }
        const auto problems = bundleProblems(bundleDir());
        if (!problems.isEmpty()) {
            setState(State::NoBundle, problems.first());
            return;
        }
        if (!elevated()) {
            setState(State::NoAdmin, QStringLiteral("запустите клиент от имени администратора"));
            return;
        }
        const int idx = physicalIfIndex();
        if (idx <= 0) {
            setState(State::Failed, QStringLiteral("не найден сетевой адаптер с выходом в интернет"));
            return;
        }

        const auto plan = buildCurrentPlan(serverHost);
        if (plan.args.isEmpty()) {
            setState(State::Failed, QStringLiteral("пустой план"));
            return;
        }
        // Уже работает ровно с этим планом — не трогаем: перезапуск winws это
        // выгрузка и загрузка драйвера, а каждая такая пара видна античиту.
        if (st == State::Running && planHash == plan.hash && ifIndexForPlan == idx) return;

        if (proc != nullptr) stopProcess();
        if (!writeLists(plan)) {
            setState(State::Failed, QStringLiteral("не удалось записать списки в %1").arg(listDir()));
            return;
        }

        planHash = plan.hash;
        ifIndexForPlan = idx;
        startedAtMs = QDateTime::currentMSecsSinceEpoch();
        lastOutput.clear();
        setState(State::Starting);

        proc = new NekoGui_sys::ExternalProcess();
        proc->managed = false; // ни одного всплывающего окна: состояние живёт в панели
        proc->tag = QStringLiteral("dpi");
        proc->program = QDir(bundleDir()).filePath(QStringLiteral("winws.exe"));
        proc->arguments = plan.args;
        proc->setWorkingDirectory(bundleDir());
        proc->setProcessChannelMode(QProcess::MergedChannels);

        connect(proc, &QProcess::readyReadStandardOutput, this, [this] {
            const auto out = QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed();
            if (!out.isEmpty()) lastOutput = out.right(400);
        });
        connect(proc, &QProcess::stateChanged, this, [this](QProcess::ProcessState s) {
            if (s != QProcess::NotRunning || stopping) return;
            // ПРАВИЛО 1: любой выход winws — немедленная выгрузка драйвера.
            const bool early = QDateTime::currentMSecsSinceEpoch() - startedAtMs < 3000;
            crashesInRow = early ? crashesInRow + 1 : 0;
            const auto tail = lastOutput.isEmpty() ? QStringLiteral("без сообщений") : lastOutput;
            stop(tail, State::Failed);
            if (crashesInRow >= 3) {
                // Три падения подряд — это не «не повезло», а неподходящий план.
                // Крутить его дальше значит трижды в секунду грузить драйвер.
                NekoGui::dataStore->dpi_module_enabled = false;
                setState(State::Failed,
                         QStringLiteral("winws трижды подряд не удержался — модуль выключен: %1").arg(tail));
            }
        });

        proc->Start();
        if (!proc->waitForStarted(5000)) {
            const auto err = proc->errorString();
            stopProcess();
            setState(State::Failed, err);
            return;
        }
        driverTouched = true;
        assignToJob(proc->processId());

        // «Запустился» — это не «вернулся из CreateProcess». winws поднимает
        // драйвер и падает на этом, если ему не дали; секунда ожидания отличает
        // рабочий запуск от такого падения, и человек видит правду сразу.
        QTimer::singleShot(1200, this, [this] {
            if (st != State::Starting) return;
            if (proc == nullptr || proc->state() != QProcess::Running) return;
            crashesInRow = 0;
            NekoGui::dataStore->dpi_module_active = true;
            const auto *strategy = findStrategy(NekoGui::dataStore->dpi_module_strategy);
            setState(State::Running, strategy != nullptr ? strategy->title : QStringLiteral("обход включён"));
            guard->start();
            emit coreRestartWanted(); // ярус ядра должен уйти: правило 4
        });
#endif
    }

    void DpiModule::assignToJob(qint64 pid) {
#ifdef Q_OS_WIN
        // ПРАВИЛО 6. Клиент упал — winws уходит вместе с ним. Без этого остаётся
        // процесс с загруженным драйвером и без хозяина, и найти его человеку
        // нечем: в списке задач это строка «winws».
        if (job == nullptr) {
            job = CreateJobObjectW(nullptr, nullptr);
            if (job == nullptr) return;
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(static_cast<HANDLE>(job), JobObjectExtendedLimitInformation, &limits,
                                    sizeof(limits));
        }
        HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
        if (h == nullptr) return;
        AssignProcessToJobObject(static_cast<HANDLE>(job), h);
        CloseHandle(h);
#else
        Q_UNUSED(pid)
#endif
    }

    void DpiModule::stopProcess() {
        if (proc == nullptr) return;
        stopping = true;
        proc->disconnect(this);
        proc->Kill();
        proc->waitForFinished(2000);
        proc->deleteLater();
        proc = nullptr;
        stopping = false;
    }

    void DpiModule::unloadDriver() {
        // Выгружаем только то, что поднимали. Без этой отметки состояние
        // Blocked («идёт игра») запускало бы пару sc.exe каждые пять секунд —
        // на машине, где как раз идёт игра.
        if (!driverTouched) return;
        driverTouched = false;
#ifdef Q_OS_WIN
        // ПРАВИЛО 1: античиты смотрят на резидентный WinDivert, а не на winws.
        // Останавливаем и снимаем службу — ту самую, чей путь проверяет
        // foreignInterceptor. Чужую сюда не заносит: при чужой мы не стартуем.
        for (const auto &verb: {QStringLiteral("stop"), QStringLiteral("delete")}) {
            QProcess sc;
            sc.setStandardOutputFile(QProcess::nullDevice());
            sc.setStandardErrorFile(QProcess::nullDevice());
            sc.start(QStringLiteral("sc.exe"), {verb, QStringLiteral("WinDivert")});
            sc.waitForFinished(5000);
        }
#endif
    }

    void DpiModule::stop(const QString &reason, State next) {
        const bool wasActive = NekoGui::dataStore->dpi_module_active;
        guard->stop();
        stopProcess();
        unloadDriver();
        planHash.clear();
        ifIndexForPlan = 0;
        NekoGui::dataStore->dpi_module_active = false;
        setState(next, reason);
        if (wasActive) emit coreRestartWanted();
    }

    void DpiModule::tick() {
        if (!wanted || st == State::Installing) return;
        // Повторный вход настоящий, а не гипотетический: setState шлёт сигнал,
        // окно на нём обновляет строку состояния, а обновление зовёт reconcile.
        // Без этого замка start() входил бы сам в себя посреди запуска.
        if (inTick) return;
        inTick = true;
        const auto leave = qScopeGuard([this] { inTick = false; });

        if (st == State::Running || st == State::Starting) {
            const auto ac = antiCheatsRunning(runningPrograms());
            if (!ac.isEmpty()) {
                // ПРАВИЛО 2. Появился античит — гаснем, не спрашивая. Бан
                // ложится на аккаунт человека и не отменяется.
                stop(QStringLiteral("запущен %1").arg(ac.first()), State::Blocked);
                return;
            }
            // Сменился физический адаптер (Wi-Fi ↔ провод) — план устарел:
            // winws смотрит в интерфейс, которого больше нет на пути наружу.
            const int idx = physicalIfIndex();
            if (idx > 0 && idx != ifIndexForPlan) {
                start(wantedHost);
                return;
            }
            if (wantedHost != serverForPlan) start(wantedHost);
            return;
        }

        // Из отказа сами не выходим. Повторять неудачу раз в пять секунд значит
        // раз в пять секунд грузить и выгружать драйвер — ровно то, за чем
        // античиты и следят. Выход из Failed, NoBundle, NoAdmin — смена сервера
        // или рука человека (выключил и включил: это проход через Off).
        if (st == State::Blocked || st == State::Foreign || st == State::Off) {
            start(wantedHost);
            return;
        }
        if (wantedHost != serverForPlan) start(wantedHost);
    }

    void DpiModule::reconcile(bool enabled, const QString &serverHost) {
        const bool changed = wanted != enabled || wantedHost != serverHost;
        wanted = enabled;
        wantedHost = serverHost;
        if (!enabled) {
            guard->stop();
            if (st != State::Off) stop(QString(), State::Off);
            return;
        }
        if (!guard->isActive()) guard->start();
        // Тикаем только на ПЕРЕМЕНАХ. reconcile зовётся из refresh_status, то
        // есть раз в пару секунд; перечислять процессы машины с такой частотой
        // незачем — для повторяющихся проверок есть сторож с его пятью
        // секундами.
        if (changed || st == State::Off) tick();
    }

    void DpiModule::shutdown() {
        wanted = false;
        guard->stop();
        stopProcess();
        unloadDriver();
        NekoGui::dataStore->dpi_module_active = false;
        st = State::Off;
        why.clear();
    }

    bool DpiModule::install(QString *error) {
        setState(State::Installing);
        const auto r = installBundle(bundleDir(), [this](const QString &s) { setState(State::Installing, s); });
        if (!r.ok) {
            if (error != nullptr) *error = r.error;
            setState(State::NoBundle, r.error);
            return false;
        }
        setState(State::Off);
        return true;
    }

} // namespace GreenRhythm::Dpi
