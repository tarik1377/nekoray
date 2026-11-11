#pragma once
#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <functional>
namespace GreenRhythm {
    class SiteToolsRpc final : public QObject {
    public:
        using Done = std::function<void(const QJsonObject &, const QString &)>;
        explicit SiteToolsRpc(QObject *parent = nullptr);
        void request(int port, const QString &token, const QString &action, const QJsonObject &payload, Done done);

    private:
        QNetworkAccessManager network;
    };
} // namespace GreenRhythm
