#include "HTTPRequestHelper.hpp"

#include <QByteArray>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QMetaEnum>
#include <QTimer>
#include <QIODevice>

#include "main/NekoGui.hpp"

namespace NekoGui_network {

    NekoHTTPResponse NetworkRequestHelper::HttpGet(const QUrl &url) {
        QNetworkRequest request;
        QNetworkAccessManager accessManager;
        request.setUrl(url);
        // Set proxy
        if (NekoGui::dataStore->sub_use_proxy) {
            QNetworkProxy p;
            // Note: sing-box mixed socks5 protocol error
            p.setType(QNetworkProxy::HttpProxy);
            p.setHostName("127.0.0.1");
            p.setPort(NekoGui::dataStore->inbound_socks_port);
            if (NekoGui::dataStore->inbound_auth->NeedAuth()) {
                p.setUser(NekoGui::dataStore->inbound_auth->username);
                p.setPassword(NekoGui::dataStore->inbound_auth->password);
            }
            accessManager.setProxy(p);
            if (NekoGui::dataStore->started_id < 0) {
                return NekoHTTPResponse{QObject::tr("Request with proxy but no profile started.")};
            }
        }
        if (accessManager.proxy().type() == QNetworkProxy::Socks5Proxy) {
            auto cap = accessManager.proxy().capabilities();
            accessManager.proxy().setCapabilities(cap | QNetworkProxy::HostNameLookupCapability);
        }
        // Set attribute
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
        request.setHeader(QNetworkRequest::KnownHeaders::UserAgentHeader, NekoGui::dataStore->GetUserAgent());
        if (NekoGui::dataStore->sub_insecure) {
            QSslConfiguration c;
            c.setPeerVerifyMode(QSslSocket::PeerVerifyMode::VerifyNone);
            request.setSslConfiguration(c);
        }
        //
        auto _reply = accessManager.get(request);
        connect(_reply, &QNetworkReply::sslErrors, _reply, [](const QList<QSslError> &errors) {
            QStringList error_str;
            for (const auto &err: errors) {
                error_str << err.errorString();
            }
            MW_show_log(QStringLiteral("SSL Errors: %1 %2").arg(error_str.join(","), NekoGui::dataStore->sub_insecure ? "(Ignored)" : ""));
        });
        // Wait for response
        auto abortTimer = new QTimer;
        abortTimer->setSingleShot(true);
        abortTimer->setInterval(10000);
        QObject::connect(abortTimer, &QTimer::timeout, _reply, &QNetworkReply::abort);
        abortTimer->start();
        {
            QEventLoop loop;
            QObject::connect(_reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
        }
        if (abortTimer != nullptr) {
            abortTimer->stop();
            abortTimer->deleteLater();
        }
        //
        auto result = NekoHTTPResponse{_reply->error() == QNetworkReply::NetworkError::NoError ? "" : _reply->errorString(),
                                       _reply->readAll(), _reply->rawHeaderPairs()};
        _reply->deleteLater();
        return result;
    }

    NekoHTTPResponse NetworkRequestHelper::HttpPost(const QUrl &url, const QByteArray &body,
                                                    const QList<QPair<QByteArray, QByteArray>> &headers,
                                                    bool bypassProxy) {
        QNetworkRequest request;
        QNetworkAccessManager accessManager;
        request.setUrl(url);

        // Прокси ставится ровно так же, как в HttpGet, — кроме случая, когда
        // вызывающий сказал идти мимо. Расходиться этим двум путям нельзя:
        // подписка, идущая через туннель, и активация, идущая мимо, должны
        // отличаться одним признаком, а не двумя разными реализациями.
        if (!bypassProxy && NekoGui::dataStore->sub_use_proxy) {
            QNetworkProxy p;
            p.setType(QNetworkProxy::HttpProxy);
            p.setHostName("127.0.0.1");
            p.setPort(NekoGui::dataStore->inbound_socks_port);
            if (NekoGui::dataStore->inbound_auth->NeedAuth()) {
                p.setUser(NekoGui::dataStore->inbound_auth->username);
                p.setPassword(NekoGui::dataStore->inbound_auth->password);
            }
            accessManager.setProxy(p);
            if (NekoGui::dataStore->started_id < 0) {
                return NekoHTTPResponse{QObject::tr("Request with proxy but no profile started.")};
            }
        } else if (bypassProxy) {
            // Явно, а не «по умолчанию»: у QNetworkAccessManager есть ещё и
            // системный прокси, и на машине с настроенным корпоративным прокси
            // запрос ушёл бы туда молча.
            accessManager.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        }

#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
        request.setHeader(QNetworkRequest::KnownHeaders::UserAgentHeader, NekoGui::dataStore->GetUserAgent());
        request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader, "application/json");
        for (const auto &h: headers) request.setRawHeader(h.first, h.second);

        // sub_insecure сюда НЕ распространяется. Он существует ради чужих
        // подписок с самоподписанными сертификатами; активация ходит на наш
        // домен, и ослабить проверку здесь значило бы отдать токен тому, кто
        // сумел встать посередине.
        auto _reply = accessManager.post(request, body);

        auto abortTimer = new QTimer;
        abortTimer->setSingleShot(true);
        abortTimer->setInterval(10000);
        QObject::connect(abortTimer, &QTimer::timeout, _reply, &QNetworkReply::abort);
        abortTimer->start();
        {
            QEventLoop loop;
            QObject::connect(_reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
        }
        abortTimer->stop();
        abortTimer->deleteLater();

        // Тело читается ДАЖЕ при ошибке: сайт отвечает 402 и 409 с внятным
        // объяснением внутри, и выбросить его значило бы показать человеку
        // «ошибка сети» там, где написано «продлите подписку».
        auto result = NekoHTTPResponse{_reply->error() == QNetworkReply::NetworkError::NoError ? "" : _reply->errorString(),
                                       _reply->readAll(), _reply->rawHeaderPairs()};
        result.status = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        _reply->deleteLater();
        return result;
    }

    NekoHTTPResponse NetworkRequestHelper::HttpDownload(const QUrl &url, QIODevice *sink, qint64 maxBytes,
                                                        const QList<QPair<QByteArray, QByteArray>> &headers,
                                                        const std::function<void(qint64, qint64)> &progress) {
        if (sink == nullptr || !sink->isWritable()) {
            return NekoHTTPResponse{QObject::tr("Nowhere to write the download")};
        }

        QNetworkRequest request;
        QNetworkAccessManager accessManager;
        request.setUrl(url);
        // Мимо прокси — всегда, и явно. Причина в шапке объявления; явно
        // потому, что иначе запрос молча уйдёт в системный прокси машины.
        accessManager.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));

#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif
        request.setHeader(QNetworkRequest::KnownHeaders::UserAgentHeader, NekoGui::dataStore->GetUserAgent());
        for (const auto &h: headers) request.setRawHeader(h.first, h.second);
        // sub_insecure сюда не распространяется — по той же причине, что и в
        // HttpPost: это наш домен, и ослабление проверки здесь означало бы
        // принять исполняемый файл от того, кто встал посередине.

        auto _reply = accessManager.get(request);

        // Срок молчания, а не общий срок. Таймер перезаводится на каждом
        // пришедшем куске: рвётся только тишина, а медленная линия дожидается.
        auto idleTimer = new QTimer;
        idleTimer->setSingleShot(true);
        idleTimer->setInterval(20000);
        QObject::connect(idleTimer, &QTimer::timeout, _reply, &QNetworkReply::abort);
        idleTimer->start();

        qint64 written = 0;
        bool overflow = false;
        bool writeFailed = false;

        QObject::connect(_reply, &QNetworkReply::readyRead, _reply, [&] {
            idleTimer->start();
            const auto chunk = _reply->readAll();
            if (chunk.isEmpty()) return;
            // Потолок проверяется ДО записи. Проверка после означала бы, что
            // лишнее уже на диске.
            if (written + chunk.size() > maxBytes) {
                overflow = true;
                _reply->abort();
                return;
            }
            if (sink->write(chunk) != chunk.size()) {
                writeFailed = true;
                _reply->abort();
                return;
            }
            written += chunk.size();
        });

        if (progress) {
            QObject::connect(_reply, &QNetworkReply::downloadProgress, _reply,
                             [&](qint64 got, qint64 total) { progress(got, total); });
        }

        {
            QEventLoop loop;
            QObject::connect(_reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
        }
        idleTimer->stop();
        idleTimer->deleteLater();

        NekoHTTPResponse result;
        result.status = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Порядок разбора важен: свои причины называются раньше сетевой.
        // Прерванный нами запрос выставляет OperationCanceledError, и без этого
        // «файл длиннее обещанного» доехало бы до человека как «сеть подвела» —
        // то есть как повод повторить попытку, которая обязана провалиться так же.
        if (overflow) {
            result.error = QObject::tr("The file is longer than announced");
        } else if (writeFailed) {
            result.error = QObject::tr("Could not write to disk");
        } else if (_reply->error() != QNetworkReply::NetworkError::NoError) {
            result.error = _reply->errorString();
        }
        // При отказе тело НЕ отдаётся: у отказа оно короткое и осмысленное
        // (JSON сайта), но оно уже утекло в приёмник, и класть его сюда второй
        // раз незачем. Вызывающий смотрит на код и на приёмник.
        _reply->deleteLater();
        return result;
    }

    QString NetworkRequestHelper::GetHeader(const QList<QPair<QByteArray, QByteArray>> &header, const QString &name) {
        for (const auto &p: header) {
            if (QString(p.first).toLower() == name.toLower()) return p.second;
        }
        return "";
    }

} // namespace NekoGui_network
