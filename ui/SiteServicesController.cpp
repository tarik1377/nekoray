#include "SiteServicesController.hpp"
#include "SitesPage.hpp"
#include "main/SiteServices.hpp"
#include <QCryptographicHash>
#include <QPointer>
namespace GreenRhythm {
    namespace {
        QString message(const QString &error) {
            if (error == "browser_missing") return QObject::tr("Для анализа JavaScript-зависимостей нужен установленный Edge, Chrome или Chromium. Обычные маршруты продолжают работать.");
            if (error == "connect_a_profile_first") return QObject::tr("Подключите профиль, чтобы сравнить его с прямым путём. Приложение само подключение не переключает.");
            if (error == "profile_changed" || error == "scan_missing") return QObject::tr("Профиль изменился во время проверки. Повторите анализ.");
            if (error == "scan_busy") return QObject::tr("Другой анализ уже выполняется. Дождитесь его или отмените.");
        if (error == "browser_capture_failed") return QObject::tr("Не удалось запустить отдельную сессию браузера. Проверьте установленный Edge/Chrome и ограничения его запуска. Маршруты не изменены.");
            if (error == "scan_cancelled") return QObject::tr("Анализ отменён. Сайты и маршруты сохранены.");
            if (error == "full_update_required" || error.startsWith("site_tools_")) return QObject::tr("Работающее ядро не поддерживает эту функцию. Нужна полная новая сборка; соединение оставлено работающим.");
            return QObject::tr("Анализ или применение не завершены (%1). Текущее подключение не переключалось.").arg(error.left(120));
        }
        QString policyText(const QJsonObject &library) { return SiteServices::encode(SiteServices::policy(library)); }
    } // namespace
    SiteServicesController::SiteServicesController(SitesPage *page, Rpc rpc, Read read, Save save, std::function<bool()> connected, std::function<void(const QJsonObject &)> changed, QObject *parent)
        : QObject(parent), page(page), rpc(std::move(rpc)), read(std::move(read)), save(std::move(save)), connected(std::move(connected)), changed(std::move(changed)) {
        connect(page, &SitesPage::aboutToClose, this, [this] {shuttingDown=true;++generation;scanTimer.stop();runtimeTimer.stop();if(!job.isEmpty())this->rpc("cancel",{{"id",job}},[](const QJsonObject &,const QString &){});job.clear(); });
        scanTimer.setInterval(500);
        runtimeTimer.setInterval(10000);
        connect(&scanTimer, &QTimer::timeout, this, [this] { poll(); });
        connect(&runtimeTimer, &QTimer::timeout, this, [this] {if(this->page->isVisible()&&!busy)refreshRuntime(); });
        runtimeTimer.start();
        connect(page, &SitesPage::analyzeRequested, this, [this](const QString &url) { analyze(url); });
        connect(page, &SitesPage::cancelRequested, this, [this] { cancel(); });
        connect(page, &SitesPage::modeRequested, this, [this](const QString &id, const QString &mode) { chooseMode(id, mode); });
        connect(page, &SitesPage::rescanRequested, this, [this](const QString &id) {for(const auto &value:library.value("sites").toArray())if(value.toObject().value("id")==id)analyze(value.toObject().value("url").toString()); });
        connect(page, &SitesPage::removeRequested, this, [this](const QString &id) {if(busy)return;refresh();auto next=library;auto sites=next.value("sites").toArray();for(int i=sites.size()-1;i>=0;--i)if(sites.at(i).toObject().value("id")==id)sites.removeAt(i);next["sites"]=sites;QString error;if(!persist(next,&error)){this->page->setFeedback(error);return;}apply(); });
        refresh();
    }
    void SiteServicesController::request(const QString &action, const QJsonObject &payload, Done done) {
        if (shuttingDown) return;
        QPointer<SiteServicesController> guard(this);
        rpc(action, payload, [guard, done = std::move(done)](const QJsonObject &result, const QString &error) {if(guard&&!guard->shuttingDown)done(result,error); });
    }
    void SiteServicesController::refresh() {
        raw = read();
        QString error;
        QJsonObject parsed;
        if (!SiteServices::parse(raw, &parsed, &error)) {
            page->setFeedback(error);
            return;
        }
        library = parsed;
        page->setLibrary(library);
        if (changed) changed(library);
    }
    bool SiteServicesController::persist(const QJsonObject &next, QString *error) {
        if (!save(raw, next, error)) return false;
        library = next;
        raw = SiteServices::encode(next);
        page->setLibrary(next);
        page->setRuntime({});
        if (changed) changed(next);
        return true;
    }
    void SiteServicesController::refreshRuntime() {
        if (runtimePending) return;
        if (!connected()) {
            page->setRuntime({});
            return;
        }
        runtimePending = true;
        request("status", {}, [this](QJsonObject result, const QString &error) {
            runtimePending = false;
            if (!error.isEmpty()) {
                page->setRuntime({});
                return;
            }
            const auto expected = QString::fromLatin1(QCryptographicHash::hash(policyText(library).toUtf8(), QCryptographicHash::Sha256).toHex());
            result["pending"] = result.value("configuration_hash").toString() != expected;
            page->setRuntime(result);
        });
    }
    void SiteServicesController::apply(const QString &scanID) {
        if (applying) {
            applyAgain = true;
            if (!scanID.isEmpty()) queuedScanID = scanID;
            return;
        }
        if (!connected()) {
            page->setRuntime({});
            page->setFeedback(tr("Сохранено с резервной копией. Набор загрузится при следующем подключении профиля."));
            return;
        }
        const auto desired = policyText(library);
        const auto capturedRaw = raw;
        applying = true;
        request("status", {}, [this, desired, capturedRaw, scanID](const QJsonObject &state, const QString &error) {
            if (raw != capturedRaw) {
                finishApply();
                return;
            }
            if (!error.isEmpty()) {
                page->setRuntime({});
                page->setFeedback(tr("Сохранено, но пока не применено. ") + message(error));
                finishApply();
                return;
            }
            request("apply", {{"scope", state.value("scope")}, {"library", desired}, {"scan_id", scanID}}, [this, capturedRaw](const QJsonObject &, const QString &error) {
                if (raw != capturedRaw) {
                    finishApply();
                    return;
                }
                if (!error.isEmpty()) {
                    page->setRuntime({});
                    page->setFeedback(tr("Сохранено, но пока не применено. ") + message(error));
                    finishApply();
                    return;
                }
                page->setFeedback(tr("Сохранено и применено к новым соединениям. Текущее подключение и открытые соединения не перезапускались."));
                refreshRuntime();
                finishApply();
            });
        });
    }
    void SiteServicesController::finishApply() {
        applying = false;
        if (applyAgain) {
            applyAgain = false;
            const auto scanID = queuedScanID;
            queuedScanID.clear();
            apply(scanID);
        }
    }
    void SiteServicesController::chooseMode(const QString &id, const QString &mode) {
        refresh();
        auto next = library;
        QString error;
        if (!SiteServices::changeMode(&next, id, mode, &error) || !persist(next, &error)) {
            page->setFeedback(error);
            return;
        }
        page->selectSite(id);
        apply();
    }
    void SiteServicesController::analyze(const QString &url) {
        if (busy) {
            page->setFeedback(tr("Дождитесь текущего анализа или отмените его."));
            return;
        }
        QString navigation, origin, error;
        if (!SiteServices::url(url, &navigation, &origin, &error)) {
            page->setFeedback(error);
            return;
        }
        if (!connected()) {
            page->setFeedback(message("connect_a_profile_first"));
            return;
        }
        busy = true;
        const auto attempt = ++generation;
        page->setBusy(true, tr("Готовлю отдельную публичную загрузку без аккаунта…"));
        request("status", {}, [this, attempt, navigation](const QJsonObject &state, const QString &error) {
            if (attempt != generation) return;
            if (!error.isEmpty()) {
                finish(message(error));
                return;
            }
            scope = state.value("scope").toString();
            request("scan", {{"scope", scope}, {"url", navigation}}, [this, attempt](const QJsonObject &state, const QString &error) {if(attempt!=generation){if(!state.value("id").toString().isEmpty())request("cancel",{{"id",state.value("id")}},[](const QJsonObject &,const QString &){});return;}if(!error.isEmpty()){finish(message(error));return;}job=state.value("id").toString();scanTimer.start();poll(); });
        });
    }
    void SiteServicesController::poll() {
        if (!busy || polling || job.isEmpty()) return;
        polling = true;
        const auto attempt = generation;
        request("scan_status", {{"id", job}}, [this, attempt](const QJsonObject &state, const QString &error) {
            polling = false;
            if (attempt != generation) return;
            if (!error.isEmpty()) {
                finish(message(error));
                return;
            }
            if (state.value("scope").toString() != scope) {
                finish(message("profile_changed"));
                return;
            }
            const auto phase = state.value("state").toString();
            if (phase == "direct" || phase == "proxy" || phase == "starting") {
                page->setBusy(true, phase == "direct" ? tr("Проверяю публичную загрузку напрямую и собираю зависимости…") : phase == "proxy" ? tr("Проверяю ту же страницу через VPN и сравниваю зависимости…")
                                                                                                                                             : tr("Запускаю отдельную временную сессию…"));
                return;
            }
            if (phase != "completed") {
                finish(message(state.value("error").toString().isEmpty() ? "scan_failed" : state.value("error").toString()));
                return;
            }
            scanTimer.stop();
            refresh();
            auto next = library;
            QString id, errorText;
            if (!SiteServices::recordScan(&next, state.value("report").toObject(), &id, &errorText) || !persist(next, &errorText)) {
                finish(errorText);
                return;
            }
            const auto completedJob = job;
            finish(tr("Карточка сохранена. Применяю найденный набор…"));
            page->selectSite(id);
            apply(completedJob);
        });
    }
    void SiteServicesController::finish(const QString &text) {
        busy = false;
        polling = false;
        scanTimer.stop();
        const auto previous = job;
        job.clear();
        if (!previous.isEmpty()) request("cancel", {{"id", previous}}, [](const QJsonObject &, const QString &) {});
        page->setBusy(false);
        page->setFeedback(text);
    }
    void SiteServicesController::cancel() {
        ++generation;
        const auto id = job;
        finish(message("scan_cancelled"));
        if (!id.isEmpty()) request("cancel", {{"id", id}}, [](const QJsonObject &, const QString &) {});
    }
} // namespace GreenRhythm
