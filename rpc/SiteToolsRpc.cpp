#include "SiteToolsRpc.hpp"
#include "go/grpc_server/gen/libcore.pb.h"
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTimer>
#include <QtEndian>
namespace GreenRhythm {
    SiteToolsRpc::SiteToolsRpc(QObject *parent) : QObject(parent), network(this) { network.setProxy(QNetworkProxy::NoProxy); }
    void SiteToolsRpc::request(int port, const QString &token, const QString &action, const QJsonObject &payload, Done done) {
        if (port <= 0 || port > 65535 || token.isEmpty()) {
            done({}, "site_tools_unavailable");
            return;
        }
        libcore::SiteToolsReq proto;
        proto.set_action(action.toStdString());
        proto.set_payload_json(QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString());
        std::string bytes;
        proto.SerializeToString(&bytes);
        QByteArray frame(5, '\0');
        qToBigEndian<quint32>(quint32(bytes.size()), reinterpret_cast<uchar *>(frame.data() + 1));
        frame += QByteArray::fromStdString(bytes);
        QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/libcore.LibcoreService/SiteTools").arg(port)));
        request.setAttribute(QNetworkRequest::Http2DirectAttribute, true);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/grpc");
        request.setRawHeader("TE", "trailers");
        request.setRawHeader("Cache-Control", "no-store");
        request.setRawHeader("nekoray_auth", token.toUtf8());
        auto *reply = network.post(request, frame);
        auto *timer = new QTimer(reply);
        timer->setSingleShot(true);
        timer->start(5000);
        connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
        connect(reply, &QNetworkReply::finished, this, [reply, timer, done = std::move(done)] {
            timer->stop();
            const auto data = reply->readAll();
            QString error;
            QJsonObject result;
            if (reply->error() != QNetworkReply::NoError || reply->rawHeader("grpc-status").toInt() != 0 || data.size() < 5 || data.size() > 2 * 1024 * 1024 || data.at(0) != 0)
                error = "site_tools_unavailable";
            else {
                const auto length = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + 1));
                libcore::SiteToolsResp response;
                if (length != quint32(data.size() - 5) || !response.ParseFromArray(data.constData() + 5, int(length)))
                    error = "site_tools_invalid_response";
                else if (!response.error().empty())
                    error = QString::fromStdString(response.error());
                else {
                    const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(response.result_json()));
                    if (!document.isObject())
                        error = "site_tools_invalid_response";
                    else
                        result = document.object();
                }
            }
            reply->deleteLater();
            done(result, error);
        });
    }
} // namespace GreenRhythm
