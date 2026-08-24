#include "PacServer.hpp"

#include "main/NekoGui_Utils.hpp"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace NekoGui_sys {

    namespace {
        /**
         * Сколько байт запроса вообще читать.
         *
         * Нам нужна первая строка. Читать без предела то, что прислали на
         * открытый порт, нельзя ни при каких обстоятельствах: одна программа,
         * льющая в нас поток, съела бы память приложения.
         */
        constexpr qint64 kMaxRequestBytes = 8 * 1024;

        /** Сколько ждать первую строку. Свой же браузер укладывается мгновенно. */
        constexpr int kReadTimeoutMs = 3000;
    } // namespace

    class PacServer::Impl : public QTcpServer {
    public:
        QString path;    ///< «/proxy-<случайное>.pac», с ведущей косой
        QByteArray body; ///< сам текст автонастройки

        explicit Impl(QObject *parent) : QTcpServer(parent) {}

    protected:
        void incomingConnection(qintptr handle) override {
            auto *sock = new QTcpSocket(this);
            if (!sock->setSocketDescriptor(handle)) {
                sock->deleteLater();
                return;
            }

            connect(sock, &QTcpSocket::readyRead, sock, [this, sock] { serve(sock); });
            connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);

            // Молчащее соединение закрывается само. Без этого достаточно
            // открыть десяток соединений и не писать в них ничего, чтобы
            // занять сервер навсегда.
            QTimer::singleShot(kReadTimeoutMs, sock, [sock] {
                if (sock->state() != QAbstractSocket::UnconnectedState) sock->disconnectFromHost();
            });
        }

    private:
        void serve(QTcpSocket *sock) {
            if (sock->bytesAvailable() > kMaxRequestBytes) {
                sock->disconnectFromHost();
                return;
            }
            // Ждём конца первой строки — по ней всё и решается.
            if (!sock->peek(kMaxRequestBytes).contains('\n')) return;

            const auto line = QString::fromLatin1(sock->readLine(kMaxRequestBytes)).trimmed();
            const auto parts = line.split(' ');

            const bool ok = parts.size() >= 2 && parts[0] == "GET" && parts[1] == path;
            if (!ok) {
                // Ни слова о том, что здесь что-то есть: сервер слушает на
                // 127.0.0.1 и виден любой программе этого компьютера.
                sock->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                sock->disconnectFromHost();
                return;
            }

            QByteArray head;
            head += "HTTP/1.1 200 OK\r\n";
            // Тип обязателен именно этот: по другому macOS файл не примет.
            head += "Content-Type: application/x-ns-proxy-autoconfig\r\n";
            head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            head += "Cache-Control: no-store\r\n";
            head += "Connection: close\r\n\r\n";
            sock->write(head);
            sock->write(body);
            sock->disconnectFromHost();
        }
    };

    PacServer::PacServer(QObject *parent) : QObject(parent) {}

    PacServer::~PacServer() { Stop(); }

    QString PacServer::Start(const QString &pacText) {
        if (impl == nullptr) impl = new Impl(this);
        impl->body = pacText.toUtf8();

        if (impl->isListening()) return url; // адрес не меняем — см. шапку

        // Путь со случайным хвостом. Он же — единственное, что отличает наш
        // адрес от угадываемого.
        impl->path = "/proxy-" + GetRandomString(16) + ".pac";

        // Порт у ядра: закреплённый и занятый чужой программой превратил бы
        // включение прокси в необъяснимый отказ.
        if (!impl->listen(QHostAddress::LocalHost, 0)) {
            impl->deleteLater();
            impl = nullptr;
            return {};
        }

        url = QString("http://127.0.0.1:%1%2").arg(impl->serverPort()).arg(impl->path);
        return url;
    }

    void PacServer::Stop() {
        if (impl == nullptr) return;
        impl->close();
        impl->deleteLater();
        impl = nullptr;
        url.clear();
    }

} // namespace NekoGui_sys
