#include "ServiceHealth.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUuid>
#include <QSysInfo>
#include <algorithm>
#include <QAuthenticator>

namespace GreenRhythm {
QList<HealthTarget> healthTargets(const QString &service) {
    QList<HealthTarget> targets{
        {"cloudflare", QObject::tr("Контроль Cloudflare"), QUrl("https://cp.cloudflare.com/generate_204"), 204, true},
        {"google", QObject::tr("Контроль Google"), QUrl("https://www.gstatic.com/generate_204"), 204, true},
        {"ip_control", QObject::tr("HTTPS по IP, без DNS назначения"), QUrl("https://1.1.1.1/cdn-cgi/trace"), 302, true}};
    if (service == "discord") targets.append({"discord", QObject::tr("Discord HTTPS"), QUrl("https://discord.com/api/v10/gateway"), 200, false});
    return targets;
}
QString healthPathName(WebCheckPath path) {
    return path == WebCheckPath::System ? QObject::tr("Без локального прокси") : QObject::tr("Через профиль и его правила");
}
namespace {
bool ok(const HealthSample &sample, const HealthTarget &target) {
    return sample.result.kind == WebCheckKind::Http && sample.result.httpStatus == target.expectedStatus;
}
QString kindName(WebCheckKind kind) {
    switch (kind) {
    case WebCheckKind::Http: return "http";
    case WebCheckKind::Dns: return "dns";
    case WebCheckKind::Tcp: return "tcp";
    case WebCheckKind::Tls: return "tls";
    case WebCheckKind::Timeout: return "timeout";
    case WebCheckKind::Cancelled: return "cancelled";
    case WebCheckKind::Unavailable: return "unavailable";
    case WebCheckKind::Network: return "network";
    }
    return "unknown";
}
QString documentText(const QString &state) {
    if (state == "not_checked") return QObject::tr("не проверено");
    if (state == "valid") return QObject::tr("ответ распознан, профили не импортировались");
    if (state == "unvalidated") return QObject::tr("ответ получен, содержимое не проверено");
    if (state == "invalid_content") return QObject::tr("ответ не распознан");
    if (state == "tls") return QObject::tr("ошибка защищённого соединения или сертификата");
    if (state == "timeout") return QObject::tr("время ожидания истекло");
    if (state == "denied") return QObject::tr("сервер отказал в доступе");
    if (state == "redirect") return QObject::tr("сервер перенаправляет запрос, переход не выполнялся");
    if (state == "too_large") return QObject::tr("ответ превышает предел проверки");
    if (state == "unsupported_url") return QObject::tr("для проверки нужен адрес HTTPS без логина и пароля");
    if (state == "no_incident_reported") return QObject::tr("о массовом инциденте не сообщается");
    if (state == "incident_reported") return QObject::tr("сервис сообщает об инциденте; его связь с вашей проблемой не установлена");
    return QObject::tr("ответ не получен");
}
}
ServiceHealth::ServiceHealth(QObject *parent) : QObject(parent) {
    next.setSingleShot(true);
    connect(&next, &QTimer::timeout, this, &ServiceHealth::nextRound);
    overall.setSingleShot(true);
    connect(&overall, &QTimer::timeout, this, [this] { deadlineExceeded = true; cancel(); });
    connect(&activeTransport, &DiscordTransport::completed, this, [this] {
        activeTransportDone = true; ++done; emit updated(done, total); maybeFinish();
    });
}
ServiceHealth::~ServiceHealth() { running = false; stopJobs(); }
bool ServiceHealth::start(const HealthContext &snapshot, const QString &requestedService, int requestedRounds,
                          const QList<HealthTarget> &custom, int intervalMs, int timeoutMs) {
    if (running) return false;
    stopJobs(); ++generation;
    context = snapshot; service = requestedService == "discord" ? "discord" : "general";
    targets = custom.isEmpty() ? healthTargets(service) : custom;
    if (targets.isEmpty() || targets.size() > 8) return false;
    for (const auto &target : targets) if (!parseWebCheckUrl(target.url.toString(), nullptr, nullptr)) return false;
    rounds = std::clamp(requestedRounds, 1, 10); round = 0; done = 0;
    samples.clear(); discordObservation = {}; cancelled = false; stale = false; pending = 0; documentPending = 0;
    fresh = {}; comparison = {}; comparisonDetails.clear(); candidates.clear(); comparisonStarted = false;
    comparisonDone = context.coreExecutable.isEmpty() || !context.profileRunning;
    activeTransportDone = true;
    deadlineExceeded = false;
    subscriptionState = "not_checked"; discordState = "not_checked"; subscriptionHttp = 0; subscriptionProfiles = 0;
    started = QDateTime::currentDateTimeUtc(); finished = {}; reportId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    interval = std::clamp(intervalMs, 0, 10000); timeout = std::clamp(timeoutMs, 50, 5000);
    total = rounds * targets.size() * 2;
    const bool sub = context.subscription.scheme() == "https" && !context.subscription.host().isEmpty()
        && context.subscription.userInfo().isEmpty();
    // Custom targets are used by offline tests; they never contact public APIs.
    const bool status = service == "discord" && custom.isEmpty();
    total += int(sub) + int(status) + int(!comparisonDone); documentPending = int(sub) + int(status);
    running = true;
    overall.start(140000);
    if (status && context.profileRunning) {
        activeTransportDone = false; ++total;
        DiscordProbeOptions options; options.proxy = context.profileProxy; activeTransport.start(options);
    }
    if (sub) fetchDocument(true);
    else if (!context.subscription.isEmpty()) subscriptionState = "unsupported_url";
    if (status) fetchDocument(false);
    emit updated(done, total);
    nextRound();
    return true;
}
void ServiceHealth::nextRound() {
    if (!running) return;
    ++round; pending = targets.size();
    const auto token = generation;
    for (const auto &target : targets) {
        auto *probe = new BrowserNetworkProbe(this); probes.append(probe);
        WebCheckOptions options;
        options.profileRunning = context.profileRunning; options.greenRhythmProxy = context.profileProxy;
        options.systemProxy = QNetworkProxy(QNetworkProxy::NoProxy); options.timeoutMs = timeout;
        const int sampleRound = round;
        connect(probe, &BrowserNetworkProbe::pathFinished, this, [this, token, target, sampleRound](const WebCheckResult &result) {
            if (!running || token != generation) return;
            samples.append({target.id, sampleRound, result, QDateTime::currentDateTimeUtc()}); ++done; emit updated(done, total);
        });
        connect(probe, &BrowserNetworkProbe::completed, this, [this, token, probe] {
            probes.removeAll(probe); probe->deleteLater();
            if (!running || token != generation) return;
            if (--pending == 0) {
                if (round < rounds) next.start(interval);
                else maybeFinish();
            }
        });
        probe->start(target.url, options);
    }
}
void ServiceHealth::fetchDocument(bool subscription) {
    const auto token = generation;
    auto *manager = new QNetworkAccessManager(this); documents.append(manager);
    const bool useProfile = subscription ? context.subscriptionViaProfile : context.profileRunning;
    manager->setProxy(useProfile ? context.profileProxy : QNetworkProxy(QNetworkProxy::NoProxy));
    if (useProfile) {
        auto supplied = std::make_shared<bool>(false);
        connect(manager, &QNetworkAccessManager::proxyAuthenticationRequired, manager,
            [proxy = context.profileProxy, supplied](const QNetworkProxy &requested, QAuthenticator *auth) {
                if (*supplied || requested.hostName() != proxy.hostName() || requested.port() != proxy.port()) return;
                *supplied = true; auth->setUser(proxy.user()); auth->setPassword(proxy.password());
            });
    }
    QNetworkRequest request(subscription ? context.subscription : QUrl("https://discordstatus.com/api/v2/status.json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    // Manager is new for this one document, with no site credential cache.
    request.setHeader(QNetworkRequest::UserAgentHeader, subscription && !context.subscriptionUserAgent.isEmpty()
        ? context.subscriptionUserAgent : QStringLiteral("GreenRhythm health check"));
    auto *reply = manager->get(request);
    const int limit = subscription ? 1024 * 1024 : 32768;
    reply->setReadBufferSize(limit + 1);
    auto data = std::make_shared<QByteArray>();
    auto expired = std::make_shared<bool>(false);
    auto oversized = std::make_shared<bool>(false);
    auto *timer = new QTimer(reply); timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply, expired] { *expired = true; reply->abort(); });
    connect(reply, &QNetworkReply::readyRead, reply, [reply, data, oversized, limit] {
        data->append(reply->read(limit + 1 - data->size()));
        if (data->size() > limit) { *oversized = true; reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, token, manager, reply, data, expired, oversized, subscription] {
        documents.removeAll(manager); manager->deleteLater();
        if (!running || token != generation) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString state = *oversized ? "too_large" : *expired ? "timeout" : reply->error() == QNetworkReply::SslHandshakeFailedError ? "tls"
            : status == 401 || status == 403 ? "denied" : status >= 300 && status < 400 ? "redirect"
            : reply->error() != QNetworkReply::NoError || status != 200 ? "unavailable" : "ok";
        if (subscription) {
            subscriptionHttp = status;
            if (state == "ok") {
                if (readSubscription) { fresh = readSubscription(*data); subscriptionProfiles = fresh.count; }
                else subscriptionProfiles = validateSubscription ? validateSubscription(*data) : 0;
                state = !readSubscription && !validateSubscription ? "unvalidated" : subscriptionProfiles > 0 ? "valid" : "invalid_content";
            }
            subscriptionState = state;
        } else {
            if (state == "ok") {
                const auto json = QJsonDocument::fromJson(*data).object();
                const auto indicator = json.value("status").toObject().value("indicator").toString();
                state = json.value("page").toObject().value("id").toString() != "srhpyqt94yxb" ? "invalid_content"
                    : indicator == "none" ? "no_incident_reported"
                    : QStringList{"minor", "major", "critical"}.contains(indicator) ? "incident_reported" : "invalid_content";
            }
            discordState = state;
        }
        data->fill('\0'); data->clear();
        --documentPending; ++done; emit updated(done, total); maybeFinish();
    });
    timer->start(timeout * 2);
    if (useProfile && !context.profileRunning) reply->abort();
}
void ServiceHealth::maybeFinish() {
    if (!running || pending != 0 || round < rounds || documentPending != 0 || !activeTransportDone) return;
    if (!comparisonDone) {
        if (!comparisonStarted) {
            comparisonStarted = true; candidates.append(context.currentCandidate);
            candidates.append(fresh.alternatives.mid(0, 2));
            total += candidates.size() - 1; nextCandidate();
        }
        return;
    }
    overall.stop(); running = false; finished = QDateTime::currentDateTimeUtc(); emit completed();
}
void ServiceHealth::nextCandidate() {
    if (!running) return;
    if (candidates.isEmpty()) { comparisonDone = true; maybeFinish(); return; }
    const bool current = comparison.isEmpty();
    auto *probe = new ProfileProbe(this); isolated = probe;
    QList<ProfileProbeTarget> checks;
    for (const auto &target : targets) checks.append({target.id, target.url, target.expectedStatus});
    connect(probe, &ProfileProbe::completed, this, [this, probe, current] {
        if (isolated == probe) isolated = nullptr;
        auto result = probe->report(); result["source"] = current ? "current_clean" : "fresh_alternative";
        result["alternative_number"] = current ? 0 : comparison.size();
        comparison.append(result);
        comparisonDetails.append((current ? tr("Текущий профиль без пользовательских правил")
            : tr("Альтернатива %1 из свежей подписки").arg(comparison.size() - 1)) + "\n" + probe->details());
        probe->deleteLater();
        if (!running) return;
        ++done; emit updated(done, total);
        QTimer::singleShot(0, this, [this, token = generation] { if (running && token == generation) nextCandidate(); });
    });
    const auto candidate = candidates.takeFirst();
    probe->start(candidate, context.coreExecutable, checks, service == "discord", timeout);
}
void ServiceHealth::stopJobs() {
    next.stop(); overall.stop();
    activeTransport.blockSignals(true); activeTransport.cancel(); activeTransport.blockSignals(false);
    if (isolated) { auto *probe = isolated; isolated = nullptr; probe->disconnect(this); probe->cancel(); probe->deleteLater(); }
    candidates.clear(); fresh.alternatives.clear(); context.currentCandidate.outbound = {};
    const auto old = probes; probes.clear();
    for (auto *probe : old) { probe->disconnect(this); probe->cancel(); probe->deleteLater(); }
    const auto docs = documents; documents.clear();
    for (auto *manager : docs) {
        for (auto *reply : manager->findChildren<QNetworkReply *>()) { reply->disconnect(this); reply->abort(); }
        manager->deleteLater();
    }
}
void ServiceHealth::cancel() {
    if (!running) return;
    running = false; cancelled = true; ++generation; stopJobs();
    finished = QDateTime::currentDateTimeUtc(); emit completed();
}
void ServiceHealth::invalidate() { stale = true; if (running) cancel(); else emit updated(done, total); }
void ServiceHealth::recordDiscordObservation(int viaTunnel, int direct, int udp, bool limited,
                                            const QDateTime &from, const QDateTime &to) {
    discordObservation = {{"via_profile", viaTunnel}, {"direct", direct}, {"udp_via_profile", udp}, {"limited", limited},
        {"started_utc", from.toUTC().toString(Qt::ISODateWithMs)}, {"finished_utc", to.toUTC().toString(Qt::ISODateWithMs)},
        {"delivery_verified", false}};
    emit updated(done, total);
}
QString ServiceHealth::summary() const {
    if (stale) return tr("Подключение изменилось во время проверки. Повторите её для текущего профиля.");
    if (deadlineExceeded) return tr("Достигнут предел времени проверки. Результаты неполные и не определяют причину сбоя.");
    if (cancelled) return tr("Проверка отменена. Неполные результаты не определяют причину сбоя.");
    if (running || !started.isValid()) return tr("Проверка ещё не завершена.");
    auto verdict = [this](const QString &connection) {
        const bool subscriptionFailed = !QStringList{"valid", "not_checked", "unvalidated", "unsupported_url"}.contains(subscriptionState);
        const auto compared = comparisonConclusion();
        const auto combined = connection + (compared.isEmpty() ? QString() : '\n' + compared);
        return subscriptionFailed ? tr("Подписка: %1.\n%2").arg(documentText(subscriptionState), combined) : combined;
    };
    if (!context.profileRunning) return verdict(tr("Профиль не подключён. Проверен только системный путь; работа VPN не установлена."));
    bool failedControl = false, anyControl = false, intermittent = false, failedService = false;
    for (const auto &target : targets) {
        int successes = 0, count = 0;
        for (const auto &sample : samples) if (sample.target == target.id && sample.result.path == WebCheckPath::GreenRhythm) {
            ++count; successes += int(ok(sample, target));
        }
        if (target.control) { anyControl |= successes > 0; failedControl |= successes == 0; }
        else failedService |= successes < count;
        intermittent |= successes > 0 && successes < count;
    }
    if (intermittent) return verdict(tr("Результаты повторных запросов через профиль различались. Некоторые адреса отвечали не каждый раз; причина ещё не установлена."));
    if (!anyControl) return verdict(tr("Контрольные HTTPS-запросы через профиль не прошли. Возможны проблемы сети, профиля или его маршрутов; виновник пока не определён."));
    if (failedService) return verdict(tr("Контрольные адреса через профиль доступны, но HTTPS выбранного сервиса не проходит. Сбой ограничен сервисом или путём к нему; это не доказательство вины провайдера."));
    if (failedControl) return verdict(tr("Результаты контрольных адресов различаются. Общую исправность подключения подтвердить нельзя."));
    if (service == "discord" && activeTransport.report().value("gateway_wss") != "not_checked") {
        const auto transport = activeTransport.report();
        if (transport.value("gateway_wss") != "hello")
            return verdict(tr("HTTPS Discord доступен, но проверка Gateway WebSocket не прошла. Причина и работа голосового сервера ещё не установлены; смотрите подробности транспорта."));
        if (transport.value("udp_control") != "replies")
            return verdict(tr("HTTPS и Gateway WebSocket Discord доступны, но не все контрольные UDP-ответы получены. Это отдельный UDP-путь, а не доказательство отказа голоса Discord."));
    }
    if (service == "discord" && !context.tunEnabled)
        return verdict(tr("Контрольные запросы прошли, но TUN выключен. Системный HTTP-прокси не перенаправляет голосовой UDP Discord. Если голос не подключается, выберите туннель в настройках и переподключитесь, когда это будет удобно. Контрольная UDP-проба использует прокси явно и не проверяет маршрут самой программы Discord."));
    return verdict(service == "discord"
        ? tr("HTTPS Discord и контрольные запросы прошли. Голосовое соединение не проверено. Результаты WebSocket и UDP приведены отдельно.")
        : tr("Контрольные HTTPS-запросы прошли в интервале проверки. Это не подтверждает работу всех приложений и голосовых соединений."));
}
QString ServiceHealth::comparisonConclusion() const {
    if (comparison.isEmpty()) return {};
    auto passed = [this](const QJsonObject &entry) {
        if (entry.value("state") != "complete") return false;
        const auto entries = entry.value("samples").toArray();
        if (entries.size() != targets.size() * 3) return false;
        for (const auto &sample : entries) if (!sample.toObject().value("ok").toBool()) return false;
        if (service == "discord") {
            const auto transport = entry.value("discord_transport").toObject();
            if (transport.value("gateway_wss") != "hello" || transport.value("udp_control") != "replies") return false;
        }
        return true;
    };
    const auto current = comparison.first().toObject();
    if (!passed(current)) {
        for (int i = 1; i < comparison.size(); ++i) if (passed(comparison[i].toObject())) {
            return (current.value("state") == "complete"
                ? tr("Альтернатива %1 прошла проверку, текущий профиль без правил — не все пробы. Причина различия между узлами ещё не установлена.")
                : tr("Альтернатива %1 прошла проверку. Проверка текущего профиля без правил не завершилась, сравнение неполное.")).arg(i)
                + ' ' + tr("Можно проверить альтернативу вручную. Подключение не переключалось.");
        }
        if (current.value("state") != "complete") return tr("Изолированная проверка текущего профиля не завершена. Причину сетевого сбоя по ней определить нельзя.");
        return tr("Сравнение не выявило полностью прошедшего проверку профиля. Это не доказывает общий отказ сервера или провайдера.");
    }
    bool activeFailed = false;
    for (const auto &sample : samples) if (sample.result.path == WebCheckPath::GreenRhythm)
        for (const auto &target : targets) if (sample.target == target.id && !ok(sample, target)) activeFailed = true;
    if (service == "discord") {
        const auto transport = activeTransport.report();
        activeFailed |= transport.value("gateway_wss") != "hello" || transport.value("udp_control") != "replies";
    }
    return activeFailed ? tr("Изолированный текущий профиль прошёл пробы, а рабочий путь с правилами — не все. Сравните маршруты, DNS и параметры ядра; последовательные пробы также могли застать краткий сбой. Автоматических изменений нет.")
        : tr("Текущий профиль прошёл проверку с правилами и без них. Доступность всех приложений и голоса этим не установлена.");
}
QString ServiceHealth::details() const {
    QStringList lines;
    lines.append(tr("Режим при запуске проверки: TUN — %1; системный прокси — %2.")
        .arg(context.tunEnabled ? tr("включён") : tr("выключен"), context.systemProxyEnabled ? tr("включён") : tr("выключен")));
    if (service == "discord" && !context.tunEnabled)
        lines.append(tr("Без TUN голосовой UDP приложения не перенаправляется системным HTTP-прокси. Другая программа или VPN могут управлять им отдельно. Наша контрольная UDP-проба обращается к прокси явно; её успех не подтверждает, что сам Discord использует этот путь."));
    for (const auto &target : targets) for (const auto path : {WebCheckPath::System, WebCheckPath::GreenRhythm}) {
        int success = 0, count = 0; QList<qint64> timings; QString last;
        for (const auto &sample : samples) if (sample.target == target.id && sample.result.path == path) {
            ++count; if (ok(sample, target)) { ++success; timings.append(sample.result.elapsedMs); }
            last = webCheckResultText(sample.result);
        }
        std::sort(timings.begin(), timings.end());
        const auto median = timings.isEmpty() ? 0 : timings.size() % 2 ? timings[timings.size()/2]
            : (timings[timings.size()/2-1] + timings[timings.size()/2])/2;
        lines.append(tr("%1 · %2: %3/%4 успешных запросов%5\n%6").arg(target.title, healthPathName(path))
            .arg(success).arg(count).arg(timings.isEmpty() ? QString() : tr(", медиана %1 мс").arg(median), last));
    }
    lines.append(tr("Подписка: %1; HTTP %2; профилей в ответе: %3. Сохранённых профилей: %4.")
        .arg(documentText(subscriptionState)).arg(subscriptionHttp).arg(subscriptionProfiles).arg(context.cachedProfiles));
    if (context.lastUpdateOutcome == 2) lines.append(tr("Последнее обновление подписки в клиенте не удалось. Это отдельный результат от проверки уже подключённого профиля."));
    if (service == "discord") lines.append(tr("Официальный статус Discord: %1. Отсутствие инцидента не исключает отдельный сбой.").arg(documentText(discordState)));
    if (service == "discord" && context.profileRunning && !activeTransportDone) lines.append(activeTransport.details());
    else if (service == "discord" && context.profileRunning && activeTransport.report().value("gateway_wss") != "not_checked") lines.append(activeTransport.details());
    if (comparisonStarted) {
        lines.append(tr("Изолированное сравнение: отдельные ядра, без TUN и пользовательских правил. Альтернативы только из полученного сейчас ответа; выбрано не более двух. Текущий профиль в свежей подписке: %1. Неподдерживаемых: %2.")
            .arg(fresh.currentPresence == "present" ? tr("есть") : fresh.currentPresence == "absent" ? tr("отсутствует") : tr("не установлено")).arg(fresh.unsupported));
        lines.append(comparisonDetails);
        if (isolated) lines.append(tr("Следующий профиль проверяется…"));
        if (fresh.alternatives.isEmpty()) lines.append(tr("Других поддерживаемых профилей в свежем ответе нет либо подписка недоступна. Сохранённые устаревшие профили не подставлялись."));
    }
    if (!discordObservation.isEmpty()) lines.append(tr("Наблюдение Discord: через профиль %1, напрямую %2, UDP через профиль %3. Доставка данных этим не подтверждена.")
        .arg(discordObservation.value("via_profile").toInt()).arg(discordObservation.value("direct").toInt()).arg(discordObservation.value("udp_via_profile").toInt()));
    lines.append(tr("Сравнение без локального прокси не доказывает прямой выход: TUN или другой VPN могут перехватывать этот трафик. Запросы через профиль следуют его маршрутам. Ошибки HTTPS не являются измерением потери пакетов."));
    return lines.join("\n\n");
}
QJsonObject ServiceHealth::report() const {
    QJsonArray results;
    for (const auto &sample : samples) results.append(QJsonObject{{"target", sample.target}, {"round", sample.round},
        {"path", sample.result.path == WebCheckPath::System ? "system_no_proxy" : "profile_rules"},
        {"kind", kindName(sample.result.kind)}, {"http_status", sample.result.httpStatus}, {"elapsed_ms", sample.result.elapsedMs},
        {"observed_utc", sample.observedUtc.toString(Qt::ISODateWithMs)}});
    QJsonArray catalog;
    for (const auto &target : targets) catalog.append(QJsonObject{{"id", target.id}, {"control", target.control}, {"expected_http", target.expectedStatus}});
    // Allowlist only: never serialize context, URLs, bodies, error strings,
    // usernames, proxy credentials, subscription keys, profile names or IPs.
    return {{"schema", 2}, {"report_id", reportId}, {"started_utc", started.toString(Qt::ISODateWithMs)},
        {"finished_utc", finished.toString(Qt::ISODateWithMs)}, {"version", context.version}, {"protocol", context.protocol},
        {"os", QSysInfo::prettyProductName()}, {"node_id", context.nodeId}, {"route_fingerprint", context.routeFingerprint},
        {"tun_stack", context.tunStack}, {"mtu", context.mtu},
        {"service", service}, {"profile_running", context.profileRunning}, {"tun_enabled", context.tunEnabled},
        {"system_proxy_enabled", context.systemProxyEnabled},
        {"independent_isp_baseline", false}, {"voice_verified", false}, {"cancelled", cancelled}, {"stale", stale},
        {"deadline_exceeded", deadlineExceeded},
        {"rounds_requested", rounds}, {"subscription_state", subscriptionState}, {"subscription_http", subscriptionHttp},
        {"subscription_profiles", subscriptionProfiles}, {"cached_profiles", context.cachedProfiles},
        {"last_subscription_update_outcome", context.lastUpdateOutcome}, {"last_subscription_update_utc", context.lastUpdateUtc},
        {"discord_status", discordState}, {"discord_observation", discordObservation},
        {"discord_transport", service == "discord" && context.profileRunning ? activeTransport.report() : QJsonObject()},
        {"current_in_fresh_subscription", fresh.currentPresence}, {"unsupported_fresh_profiles", fresh.unsupported},
        {"profile_comparison", comparison}, {"targets", catalog}, {"samples", results}, {"conclusion", summary()}};
}
} // namespace GreenRhythm
