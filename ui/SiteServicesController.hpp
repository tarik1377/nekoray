#pragma once
#include <QObject>
#include <QJsonObject>
#include <QTimer>
#include <functional>
namespace GreenRhythm {
    class SitesPage;
    class SiteServicesController final : public QObject {
    public:
        using Done = std::function<void(const QJsonObject &, const QString &)>;
        using Rpc = std::function<void(const QString &, const QJsonObject &, Done)>;
        using Read = std::function<QString()>;
        using Save = std::function<bool(const QString &, const QJsonObject &, QString *)>;
        SiteServicesController(SitesPage *page, Rpc rpc, Read read, Save save, std::function<bool()> connected, std::function<void(const QJsonObject &)> changed, QObject *parent = nullptr);
        void analyze(const QString &url);
        void chooseMode(const QString &id, const QString &mode);
        void refresh();
        void refreshRuntime();
        void cancel();

    private:
        void request(const QString &action, const QJsonObject &payload, Done done);
        bool persist(const QJsonObject &library, QString *error);
        void apply(const QString &scanID = {});
        void finishApply();
        void poll();
        void finish(const QString &text);
        SitesPage *page;
        Rpc rpc;
        Read read;
        Save save;
        std::function<bool()> connected;
        std::function<void(const QJsonObject &)> changed;
        QString raw, job, scope, queuedScanID;
        QJsonObject library;
        QTimer scanTimer, runtimeTimer;
        quint64 generation = 0;
        bool busy = false, polling = false, runtimePending = false, shuttingDown = false;
        bool applying = false, applyAgain = false;
    };
} // namespace GreenRhythm
