#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "sys/windows/WindowsProxyApi.hpp"
#endif
#include "BrowserNetworkHints.hpp"
#include <QAuthenticator>
#include "WindowsProxyController.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QSslError>
#include <QTimer>
#include <algorithm>

namespace GreenRhythm {
namespace {
bool isOurProxyEndpoint(QString endpoint, quint16 port) {
    endpoint = endpoint.trimmed();
    if (!endpoint.contains(QStringLiteral("://"))) endpoint.prepend(QStringLiteral("http://"));
    const QUrl url(endpoint);
    return url.isValid() && url.userInfo().isEmpty()
        && (url.host() == QStringLiteral("127.0.0.1") || url.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
        && url.port() == port;
}
bool isOurProxyServer(const QString &server, quint16 port) {
    if (!server.contains('=')) return isOurProxyEndpoint(server, port);
    bool http = false, https = false;
    for (const auto &entry : server.split(';', Qt::SkipEmptyParts)) {
        const int separator = entry.indexOf('=');
        if (separator < 0) continue;
        const auto scheme = entry.left(separator).trimmed().toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) continue;
        if (!isOurProxyEndpoint(entry.mid(separator + 1), port)) return false;
        if (scheme == QStringLiteral("http")) http = true;
        else https = true;
    }
    return http && https;
}
WebCheckKind errorKind(QNetworkReply::NetworkError error, bool tlsError) {
    if (tlsError || error == QNetworkReply::SslHandshakeFailedError) return WebCheckKind::Tls;
    if (error == QNetworkReply::HostNotFoundError || error == QNetworkReply::ProxyNotFoundError) return WebCheckKind::Dns;
    if (error == QNetworkReply::TimeoutError || error == QNetworkReply::ProxyTimeoutError) return WebCheckKind::Timeout;
    if (error == QNetworkReply::ConnectionRefusedError || error == QNetworkReply::RemoteHostClosedError
        || error == QNetworkReply::ProxyConnectionRefusedError || error == QNetworkReply::ProxyConnectionClosedError)
        return WebCheckKind::Tcp;
    return WebCheckKind::Network;
}
} // namespace

SystemProxyHint parseSystemProxyHint(quint32 flags, const QString &server, quint16 greenRhythmPort,
                                    bool querySucceeded) {
    using namespace WindowsProxy;
    constexpr auto known = Direct | Manual | Pac | AutoDetect;
    if (!querySucceeded || !flags || (flags & ~known)) return SystemProxyHint::Unavailable;
    if (flags & (Pac | AutoDetect)) return SystemProxyHint::Automatic;
    if (flags & Manual) return isOurProxyServer(server, greenRhythmPort)
        ? SystemProxyHint::GreenRhythm : SystemProxyHint::Other;
    return SystemProxyHint::Disabled;
}

BrowserDnsHint parseBrowserDnsHint(const QString &browser, const QByteArray &localState) {
    BrowserDnsHint hint{browser, {}, false};
    if (localState.size() > 8 * 1024 * 1024) return hint;
    const auto root = QJsonDocument::fromJson(localState).object();
    const auto dns = root.value(QStringLiteral("dns_over_https")).toObject();
    const auto mode = dns.value(QStringLiteral("mode")).toString();
    if (mode == QStringLiteral("secure") || mode == QStringLiteral("automatic") || mode == QStringLiteral("off")) hint.mode = mode;
    hint.customResolver = !dns.value(QStringLiteral("templates")).toString().trimmed().isEmpty();
    return hint;
}

BrowserDnsHint applyBrowserDnsPolicies(BrowserDnsHint saved, const BrowserDnsPolicyValue &machine,
                                     const BrowserDnsPolicyValue &user) {
    const auto *policy = machine.present ? &machine : user.present ? &user : nullptr;
    if (!policy) return saved;
    saved.source = machine.present ? BrowserDnsHintSource::MachinePolicy : BrowserDnsHintSource::UserPolicy;
    saved.mode.clear();
    // Policy templates are not read. Saved resolver details cannot establish
    // the resolver used with a mandatory policy.
    saved.customResolver = false;
    if (policy->value.userType() == QMetaType::QString) {
        const auto mode = policy->value.toString();
        if (mode == QStringLiteral("secure") || mode == QStringLiteral("automatic") || mode == QStringLiteral("off")) saved.mode = mode;
    }
    return saved;
}

BrowserNetworkHints readBrowserNetworkHints(quint16 greenRhythmPort) {
    BrowserNetworkHints hints;
#ifdef Q_OS_WIN
    const auto localPath = qEnvironmentVariable("LOCALAPPDATA");
    const QDir local(localPath);
    if (!localPath.isEmpty()) {
        struct BrowserPath { QString name, localState, policy; };
        const QList<BrowserPath> browserPaths{
            {QStringLiteral("Яндекс Браузер"), QStringLiteral("Yandex/YandexBrowser/User Data/Local State"), QStringLiteral("SOFTWARE\\Policies\\YandexBrowser")},
            {QStringLiteral("Chrome"), QStringLiteral("Google/Chrome/User Data/Local State"), QStringLiteral("SOFTWARE\\Policies\\Google\\Chrome")},
            {QStringLiteral("Edge"), QStringLiteral("Microsoft/Edge/User Data/Local State"), QStringLiteral("SOFTWARE\\Policies\\Microsoft\\Edge")}};
        for (const auto &browser : browserPaths) {
            const auto readPolicy = [&](const QString &hive) {
                QSettings policy(hive + QLatin1Char('\\') + browser.policy, QSettings::NativeFormat);
                const auto key = QStringLiteral("DnsOverHttpsMode");
                const bool present = policy.contains(key);
                const auto value = present ? policy.value(key) : QVariant{};
                // A registry read failure is unknown, not proof of absence.
                return BrowserDnsPolicyValue{present || policy.status() != QSettings::NoError, value};
            };
            const auto machine = readPolicy(QStringLiteral("HKEY_LOCAL_MACHINE"));
            const auto user = readPolicy(QStringLiteral("HKEY_CURRENT_USER"));
            QFile file(local.filePath(browser.localState));
            const bool readable = file.open(QIODevice::ReadOnly) && file.size() <= 8 * 1024 * 1024;
            if (!readable && !machine.present && !user.present) continue;
            const auto saved = parseBrowserDnsHint(browser.name, readable ? file.read(8 * 1024 * 1024 + 1) : QByteArray{});
            hints.browsers.append(applyBrowserDnsPolicies(saved, machine, user));
        }
    }
    // Query active LAN flags, never infer an active PAC from its saved URL.
    // The verified backend may preserve that URL while selecting Manual.
    WindowsProxy::WinInetApi proxyApi;
    WindowsProxy::State proxyState;
    QString queryError;
    const bool proxyReadable = proxyApi.query(proxyState, queryError);
    hints.systemProxy = parseSystemProxyHint(proxyState.flags, proxyState.server, greenRhythmPort, proxyReadable);
#else
    Q_UNUSED(greenRhythmPort)
#endif
    return hints;
}

QString browserNetworkHintsText(const BrowserNetworkHints &hints) {
    QStringList text;
    for (const auto &browser : hints.browsers) {
        if (browser.source != BrowserDnsHintSource::LocalState) {
            const auto origin = browser.source == BrowserDnsHintSource::MachinePolicy
                ? QObject::tr("политика компьютера") : QObject::tr("политика пользователя");
            if (browser.mode == QStringLiteral("off"))
                text.append(QObject::tr("%1: %2 задаёт отключение защищённого DNS. Перечитывание политики браузером не проверялось.").arg(browser.browser, origin));
            else if (browser.mode == QStringLiteral("secure"))
                text.append(QObject::tr("%1: %2 задаёт отдельный защищённый DNS; он может не знать имена ресурсов рабочего VPN. Перечитывание политики браузером не проверялось.").arg(browser.browser, origin));
            else if (browser.mode == QStringLiteral("automatic"))
                text.append(QObject::tr("%1: %2 задаёт автоматический защищённый DNS. Перечитывание политики браузером не проверялось.").arg(browser.browser, origin));
            else
                text.append(QObject::tr("%1: %2 содержит нераспознанное значение защищённого DNS. Действующий режим браузера не установлен.").arg(browser.browser, origin));
            continue;
        }
        if (browser.mode == QStringLiteral("secure"))
            text.append(QObject::tr("%1: в сохранённых настройках выбран отдельный защищённый DNS. Он может не знать имена ресурсов рабочего VPN; действующий режим браузера не проверялся.").arg(browser.browser));
        else if (browser.mode == QStringLiteral("automatic"))
            text.append(QObject::tr("%1: защищённый DNS работает автоматически; фактический выбор зависит от браузера.").arg(browser.browser));
    }
    switch (hints.systemProxy) {
    case SystemProxyHint::Disabled: text.append(QObject::tr("Системный прокси выключен. Браузер использует туннель и собственные настройки DNS.")); break;
    case SystemProxyHint::GreenRhythm: text.append(QObject::tr("Системный прокси направлен в GreenRhythm.")); break;
    case SystemProxyHint::Other: text.append(QObject::tr("Системный прокси направлен в другое приложение. Подключение браузеров заменит этот выбор.")); break;
    case SystemProxyHint::Automatic: text.append(QObject::tr("Системный прокси выбирается автоматически: сценарием или автоопределением. Его выбор может отличаться для разных сайтов.")); break;
    case SystemProxyHint::Unavailable: text.append(QObject::tr("Состояние системного прокси определить не удалось.")); break;
    }
    return text.join(QStringLiteral("\n"));
}

bool parseWebCheckUrl(const QString &text, QUrl *url, QString *reason) {
    QString value = text.trimmed();
    if (!value.contains(QStringLiteral("://"))) value.prepend(QStringLiteral("https://"));
    const QUrl parsed(value, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty()
        || (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))) {
        if (reason) *reason = QObject::tr("Введите адрес сайта с http:// или https://.");
        return false;
    }
    if (!parsed.userInfo().isEmpty()) {
        if (reason) *reason = QObject::tr("Адрес с логином или паролем не подходит для проверки.");
        return false;
    }
    if (url) { *url = parsed; url->setFragment({}); }
    if (reason) reason->clear();
    return true;
}

QString maskedWebCheckUrl(const QUrl &url) {
    QUrl safe;
    safe.setScheme(url.scheme()); safe.setHost(url.host()); safe.setPort(url.port());
    return safe.toString() + QStringLiteral("/…");
}

QString webCheckResultText(const WebCheckResult &result) {
    switch (result.kind) {
    case WebCheckKind::Http:
        if (result.httpStatus == 401) return QObject::tr("HTTP 401: сервер ответил, нужна авторизация. Проверка не использует вашу сессию.");
        if (result.httpStatus == 403) return QObject::tr("HTTP 403: сервер отказал проверочному запросу. Причина ошибки учётной записи браузера этим не установлена.");
        if (result.httpStatus >= 300 && result.httpStatus < 400) return QObject::tr("HTTP %1: сервер ответил перенаправлением. Переход не выполнялся.").arg(result.httpStatus);
        return QObject::tr("HTTP %1: сервер ответил. Это не проверка входа в учётную запись.").arg(result.httpStatus);
    case WebCheckKind::Dns: return QObject::tr("DNS: не удалось найти адрес сервера.");
    case WebCheckKind::Tcp: return QObject::tr("TCP: соединение отклонено или закрыто до ответа HTTP.");
    case WebCheckKind::Tls:
        if (result.tlsIssue == WebTlsIssue::Trust) return QObject::tr("TLS: сертификату или его издателю нет доверия. Для рабочего ресурса уточните доверенный сертификат у администратора; прокси не исправляет доверие сертификату.");
        if (result.tlsIssue == WebTlsIssue::Name) return QObject::tr("TLS: сертификат выпущен для другого имени сервера.");
        if (result.tlsIssue == WebTlsIssue::Validity) return QObject::tr("TLS: сертификат просрочен или ещё не действует. Проверьте дату компьютера.");
        return QObject::tr("TLS: защищённое соединение или проверка сертификата не прошла. Проверка сертификатов сохранена.");
    case WebCheckKind::Timeout: return QObject::tr("Ответ не получен за отведённое время.");
    case WebCheckKind::Cancelled: return QObject::tr("Проверка отменена.");
    case WebCheckKind::Unavailable: return QObject::tr("Подключите сервер GreenRhythm, чтобы проверить этот путь.");
    case WebCheckKind::Network: return QObject::tr("Сетевая ошибка до ответа HTTP.");
    }
    return {};
}

WebTlsIssue webTlsIssue(const QList<QSslError> &errors) {
    for (const auto &error : errors) {
        if (error.error() == QSslError::HostNameMismatch) return WebTlsIssue::Name;
        if (error.error() == QSslError::CertificateExpired || error.error() == QSslError::CertificateNotYetValid) return WebTlsIssue::Validity;
    }
    for (const auto &error : errors) {
        if (error.error() == QSslError::SelfSignedCertificate || error.error() == QSslError::SelfSignedCertificateInChain
            || error.error() == QSslError::UnableToGetLocalIssuerCertificate || error.error() == QSslError::CertificateUntrusted
            || error.error() == QSslError::UnableToVerifyFirstCertificate) return WebTlsIssue::Trust;
    }
    return WebTlsIssue::Handshake;
}

QString compareWebCheckResults(const WebCheckResult &system, const WebCheckResult &greenRhythm) {
    if (system.kind != WebCheckKind::Http && greenRhythm.kind == WebCheckKind::Http)
        return QObject::tr("Через GreenRhythm сервер отвечает, а системная проверка не проходит. Можно подключить браузеры к локальному прокси и повторить открытие сайта.");
    if (system.kind == WebCheckKind::Http && greenRhythm.kind == WebCheckKind::Http)
        return QObject::tr("Оба пути получили ответ сервера. Ошибка браузерной сессии, расширения или отдельного запроса этой проверкой не определена.");
    if (greenRhythm.kind == WebCheckKind::Unavailable) return QObject::tr("Сравнение путей доступно после подключения GreenRhythm.");
    return QObject::tr("Проверка не подтверждает исправление браузера. Сравните результаты DNS, TCP и TLS; ошибки входа требуют отдельной проверки браузерных запросов.");
}

struct BrowserNetworkProbe::Job {
    WebCheckPath path;
    quint64 generation;
    bool done = false;
    bool tlsError = false;
    WebTlsIssue tlsIssue = WebTlsIssue::Handshake;
    QElapsedTimer elapsed;
    QPointer<QNetworkAccessManager> manager;
    QPointer<QNetworkReply> reply;
    QPointer<QTimer> timer;
};

BrowserNetworkProbe::BrowserNetworkProbe(QObject *parent) : QObject(parent) { qRegisterMetaType<WebCheckResult>(); }
BrowserNetworkProbe::~BrowserNetworkProbe() { abortPending(false); }

void BrowserNetworkProbe::abortPending(bool reportCancellation) {
    const auto old = jobs;
    if (reportCancellation) {
        for (const auto &job : old) finish(job, WebCheckKind::Cancelled);
    } else {
        ++generation; pending = 0; jobs.clear();
        for (const auto &job : old) {
            job->done = true;
            if (job->timer) job->timer->stop();
            if (job->reply) job->reply->abort();
            if (job->manager) job->manager->deleteLater();
        }
    }
}
void BrowserNetworkProbe::cancel() { abortPending(true); }

void BrowserNetworkProbe::finish(const std::shared_ptr<Job> &job, WebCheckKind kind, int status) {
    if (job->done || job->generation != generation) return;
    job->done = true;
    if (job->timer) job->timer->stop();
    if (job->reply) job->reply->abort();
    if (job->manager) job->manager->deleteLater();
    --pending;
    const WebCheckResult result{job->path, kind, status, job->elapsed.elapsed(), job->tlsIssue};
    emit pathFinished(result);
    if (pending == 0) { jobs.clear(); emit completed(); }
}

bool BrowserNetworkProbe::start(const QUrl &url, const WebCheckOptions &options) {
    QUrl validated;
    if (!parseWebCheckUrl(url.toString(), &validated, nullptr)) return false;
    abortPending(false);
    pending = options.checkSystemPath ? 2 : 1;
    const auto session = generation;
    for (const auto path : {WebCheckPath::System, WebCheckPath::GreenRhythm}) {
        if (path == WebCheckPath::System && !options.checkSystemPath) continue;
        auto job = std::make_shared<Job>(); job->path = path; job->generation = session; job->elapsed.start(); jobs.append(job);
        if (path == WebCheckPath::GreenRhythm && !options.profileRunning) { finish(job, WebCheckKind::Unavailable); continue; }
        job->manager = new QNetworkAccessManager(this);
        if (path == WebCheckPath::GreenRhythm) {
            auto supplied = std::make_shared<bool>(false);
            connect(job->manager, &QNetworkAccessManager::proxyAuthenticationRequired, job->manager,
                [credentials = options.greenRhythmProxy, supplied](const QNetworkProxy &proxy, QAuthenticator *authenticator) {
                    if (*supplied || proxy.hostName() != credentials.hostName() || proxy.port() != credentials.port()) return;
                    *supplied = true; authenticator->setUser(credentials.user()); authenticator->setPassword(credentials.password());
                });
        }
        if (path == WebCheckPath::GreenRhythm) job->manager->setProxy(options.greenRhythmProxy);
        else if (options.systemProxy.type() != QNetworkProxy::DefaultProxy) job->manager->setProxy(options.systemProxy);
        else {
            const auto proxies = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(validated));
            job->manager->setProxy(proxies.isEmpty() ? QNetworkProxy(QNetworkProxy::NoProxy) : proxies.first());
        }
        QNetworkRequest request(validated);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        // A fresh manager has no site credential cache. Allow Qt's HTTP proxy
        // challenge retry; Manual also suppresses its Basic proxy header.
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GreenRhythm connectivity check"));
        // HEAD checks DNS/TCP/TLS/HTTP without requesting document bodies.
        // A GET 401 may be held by Qt until its body is complete while deciding
        // authentication; HEAD also avoids reporting that as a false timeout.
        job->reply = job->manager->head(request);
        job->reply->setReadBufferSize(4096);
        job->timer = new QTimer(job->reply); job->timer->setSingleShot(true);
        connect(job->timer, &QTimer::timeout, this, [this, job] { finish(job, WebCheckKind::Timeout); });
        connect(job->reply, &QNetworkReply::sslErrors, this, [job](const QList<QSslError> &errors) { job->tlsError = !errors.isEmpty(); job->tlsIssue = webTlsIssue(errors); });
        connect(job->reply, &QNetworkReply::metaDataChanged, this, [this, job] {
            const int status = job->reply ? job->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
            // 407 is the intermediate local-proxy authentication challenge.
            // Let Qt retry using the proxy credentials before classifying it.
            if (status > 0 && status != 407) finish(job, WebCheckKind::Http, status);
        });
        connect(job->reply, &QNetworkReply::finished, this, [this, job] {
            if (!job->reply) return;
            const int status = job->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            finish(job, status > 0 ? WebCheckKind::Http : errorKind(job->reply->error(), job->tlsError), status);
        });
        job->timer->start(std::clamp(options.timeoutMs, 50, 5000));
    }
    return true;
}
} // namespace GreenRhythm
