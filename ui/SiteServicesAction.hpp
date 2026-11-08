#pragma once
#include "ui/MainShell.hpp"
#include "ui/SitesPage.hpp"
#include "ui/SiteServicesController.hpp"
#include "ui/SiteRoutingAction.hpp"
#include "main/SiteServices.hpp"
#ifndef NKR_NO_GRPC
#include "rpc/SiteToolsRpc.hpp"
#endif
#include <QDesktopServices>
namespace GreenRhythm {
    inline void installSiteServices(MainShell *shell, QWidget *owner) {
        auto *settings = NekoGui::dataStore;
#ifndef NKR_NO_GRPC
        auto *transport = new SiteToolsRpc(owner);
        SiteServicesController::Rpc request = [settings, transport](const QString &action, const QJsonObject &payload, SiteServicesController::Done done) {if(!settings->core_running){done({},"site_tools_unavailable");return;}transport->request(settings->core_port,settings->core_token,action,payload,std::move(done)); };
#else
        SiteServicesController::Rpc request = [](const QString &, const QJsonObject &, SiteServicesController::Done done) { done({}, "site_tools_unavailable"); };
#endif
        auto *controller = new SiteServicesController(shell->sitesWidget(), std::move(request), [settings] { return settings->site_services; }, [settings](const QString &expected, const QJsonObject &library, QString *error) {if(settings->site_services!=expected){if(error)*error=QObject::tr("Сайты уже изменились. Повторите действие.");return false;}if(!SiteServices::save(settings->fn,expected,library,error))return false;settings->site_services=SiteServices::encode(library);return true; }, [settings] { return settings->core_running && settings->started_id >= 0; }, [shell](const QJsonObject &library) { shell->setSavedSites(library); }, owner);
        QObject::connect(shell, &MainShell::siteModeRequested, controller, [controller](const QString &id, const QString &mode) { controller->chooseMode(id, mode); });
        QObject::connect(shell->sitesWidget(), &SitesPage::advancedRequested, owner, [owner] { openSiteRouting(owner); });
        QObject::connect(shell->sitesWidget(), &SitesPage::openRequested, owner, [](const QString &url) {QString origin;if(SiteServices::url(url,nullptr,&origin,nullptr))QDesktopServices::openUrl(QUrl(origin)); });
    }
} // namespace GreenRhythm
