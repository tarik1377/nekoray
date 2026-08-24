#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <functional>

namespace NekoGui_network {
    struct NekoHTTPResponse {
        QString error;
        QByteArray data;
        QList<QPair<QByteArray, QByteArray>> header;

        /**
         * Код ответа HTTP. Ноль — до сервера не дошли вовсе.
         *
         * Заполняет только HttpPost: активации код нужен по существу — 402 это
         * «подписка кончилась», 409 «лимит устройств», и путать их с «сеть не
         * ответила» нельзя. Первое требует стереть реквизиты, последнее —
         * оставить всё как было.
         */
        int status = 0;
    };

    class NetworkRequestHelper : QObject {
        Q_OBJECT

        explicit NetworkRequestHelper(QObject *parent) : QObject(parent){};

        ~NetworkRequestHelper() override = default;
        ;

    public:
        static NekoHTTPResponse HttpGet(const QUrl &url);

        /**
         * POST с телом JSON.
         *
         * ПОЧЕМУ ОТДЕЛЬНЫЙ ПРИЗНАК «МИМО ПРОКСИ». HttpGet при sub_use_proxy
         * заворачивается в локальный прокси и ОТКАЗЫВАЕТ, если ни один профиль
         * не запущен. Для подписок это верно. Для активации — тупик: канал
         * нужен, чтобы подключиться, а подключиться нельзя без канала.
         * Поэтому вызывающий говорит явно, и активация всегда говорит true.
         *
         * Заголовки — парами «имя, значение». Значения сюда попадают секретные
         * (Bearer), поэтому ни сам запрос, ни заголовки не логируются.
         */
        static NekoHTTPResponse HttpPost(const QUrl &url, const QByteArray &body,
                                         const QList<QPair<QByteArray, QByteArray>> &headers = {},
                                         bool bypassProxy = false);

        static QString GetHeader(const QList<QPair<QByteArray, QByteArray>> &header, const QString &name);
    };
} // namespace NekoGui_network

using namespace NekoGui_network;
