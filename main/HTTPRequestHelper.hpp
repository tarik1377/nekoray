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

        /**
         * Скачать большой файл в подставленный приёмник.
         *
         * ЗАЧЕМ ОТДЕЛЬНАЯ, А НЕ ФЛАГ У HttpGet. Расходятся три вещи, и каждая
         * поодиночке ломает скачивание.
         *
         * СРОК СЧИТАЕТСЯ ОТ ПОСЛЕДНЕГО ПРИШЕДШЕГО КУСКА, а не от начала. У
         * HttpGet срок общий — десять секунд на весь ответ. Для ответа сайта
         * это верно; для файла в восемь мегабайт это отказ на любой линии
         * медленнее семи мегабит, причём отказ по таймауту, неотличимый от
         * «сервер не ответил». Здесь же обрывается только молчание: качается
         * медленно, но качается — значит, ждём.
         *
         * ТЕЛО ПИШЕТСЯ ПО МЕРЕ ПРИХОДА, а не копится в памяти. readAll() на
         * готовом ответе держал бы файл в памяти дважды — в буфере ответа и в
         * возвращённом массиве.
         *
         * ПОТОЛОК ОБЯЗАТЕЛЕН. Ответ длиннее ожидаемого обрывается: без потолка
         * сломанный или подменённый ответ пишется на диск, пока тот не кончится.
         *
         * Идёт ВСЕГДА мимо прокси. Модуль скачивается тогда же, когда выдаются
         * реквизиты, — когда канала ещё нет; заворачивать его в туннель значит
         * требовать того, ради чего он и качается.
         */
        static NekoHTTPResponse HttpDownload(const QUrl &url, QIODevice *sink, qint64 maxBytes,
                                             const QList<QPair<QByteArray, QByteArray>> &headers = {},
                                             const std::function<void(qint64, qint64)> &progress = {});

        static QString GetHeader(const QList<QPair<QByteArray, QByteArray>> &header, const QString &name);
    };
} // namespace NekoGui_network

using namespace NekoGui_network;
