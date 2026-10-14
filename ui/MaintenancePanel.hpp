#pragma once

#include "ui/Palette.hpp"
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

namespace GreenRhythm {
// Values come from MainWindow; rendering never saves settings or routes.
class MaintenancePanel : public QWidget {
public:
    struct Actions {
        std::function<void()> defaults, rules, restoreRules, refreshGeo, rollback, updates;
        std::function<void(bool)> automaticGeo;
        std::function<void()> shown;
    };
    explicit MaintenancePanel(Actions actions, QWidget *parent = nullptr) : QWidget(parent), onShown(actions.shown) {
        setObjectName("grMaintenancePanel");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        auto card = [&](const QString &title, const QString &name, QLabel **detail) {
            auto *widget = new QWidget(this);
            widget->setObjectName(name);
            widget->setStyleSheet(QStringLiteral("QWidget#%1 { background:%2; border-radius:14px; }")
                                 .arg(name, Palette::kSurfaceUp));
            auto *box = new QVBoxLayout(widget);
            box->setContentsMargins(16, 12, 16, 12);
            box->setSpacing(8);
            auto *heading = new QLabel(title, widget);
            auto font = heading->font(); font.setBold(true); heading->setFont(font);
            box->addWidget(heading);
            *detail = new QLabel(widget);
            (*detail)->setTextFormat(Qt::PlainText);
            (*detail)->setWordWrap(true);
            box->addWidget(*detail);
            layout->addWidget(widget);
            return box;
        };
        auto button = [&](QBoxLayout *row, const QString &name, const QString &title,
                          std::function<void()> action) {
            auto *result = new QPushButton(title, this);
            result->setObjectName(name);
            result->setAccessibleName(title);
            result->setMinimumHeight(34);
            result->setCursor(Qt::PointingHandCursor);
            result->setStyleSheet(QStringLiteral("QPushButton { padding:0 12px; background:%1; color:%2; "
                "border:1px solid transparent; border-radius:8px; } QPushButton:hover,QPushButton:focus "
                "{ border-color:%3; } QPushButton:disabled { color:%4; }")
                .arg(Palette::kSurface, Palette::kText, Palette::kAccent, Palette::kMuted));
            if (action) connect(result, &QPushButton::clicked, this, [action] { action(); });
            row->addWidget(result);
            return result;
        };

        auto *routes = card(tr("Маршруты"), "grMaintenanceRoutes", &routeDetail);
        routeDetail->setObjectName("grCurrentRoutesDetail");
        auto *routeActions = new QHBoxLayout;
        routeActions->setSpacing(8);
        button(routeActions, "grRouteDefaults", tr("Стандартные маршруты…"), actions.defaults);
        button(routeActions, "grOwnRoutes", tr("Мои правила…"), actions.rules);
        routeActions->addStretch(); routes->addLayout(routeActions);
        restoreRulesButton = new QPushButton(tr("Вернуть прежние правила…"), this);
        restoreRulesButton->setObjectName("grRestoreRoutes");
        restoreRulesButton->setFlat(true);
        restoreRulesButton->setMinimumHeight(30);
        restoreRulesButton->setCursor(Qt::PointingHandCursor);
        restoreRulesButton->setToolTip(tr("Вернуть копию перед последним применением стандартных маршрутов. Сначала появится подтверждение."));
        connect(restoreRulesButton, &QPushButton::clicked, this, [actions] { if (actions.restoreRules) actions.restoreRules(); });
        routes->addWidget(restoreRulesButton, 0, Qt::AlignLeft);

        auto *geo = card(tr("База адресов РФ · GeoIP"), "grMaintenanceGeo", &geoDetail);
        geoDetail->setObjectName("grGeoDataStatus");
        auto *geoActions = new QHBoxLayout;
        geoActions->setSpacing(8);
        automatic = button(geoActions, "grGeoDataAutomatic", tr("Автообновление"), {});
        automatic->setCheckable(true);
        automatic->setAccessibleName(tr("Автоматически обновлять базу адресов РФ"));
        connect(automatic, &QPushButton::clicked, this, [actions](bool enabled) { if (actions.automaticGeo) actions.automaticGeo(enabled); });
        refreshGeoButton = button(geoActions, "grGeoDataRefresh", tr("Проверить сейчас"), actions.refreshGeo);
        refreshGeoButton->setAccessibleName(tr("Проверить обновление базы адресов РФ"));
        geoActions->addStretch(); geo->addLayout(geoActions);

        auto *versions = card(tr("Версия программы и восстановление"), "grMaintenanceVersion", &versionDetail);
        versionDetail->setObjectName("grCheckUpdatesDetail");
        rollbackDetail = new QLabel(tr("Проверяем сохранённую версию…"), this);
        rollbackDetail->setObjectName("grRollbackStatus");
        rollbackDetail->setWordWrap(true);
        rollbackDetail->setTextFormat(Qt::PlainText);
        versions->addWidget(rollbackDetail);
        auto *versionActions = new QHBoxLayout;
        versionActions->setSpacing(8);
        updateButton = button(versionActions, "grCheckUpdates", tr("Проверить обновления"), actions.updates);
        rollbackButton = button(versionActions, "grRollbackVersion", tr("Вернуть предыдущую…"), actions.rollback);
        rollbackButton->setEnabled(false);
        rollbackButton->setToolTip(tr("Потребуется перезапуск GreenRhythm. Серверы и настройки сохранятся."));
        versionActions->addStretch(); versions->addLayout(versionActions);
        setRoute({}, false);
        setGeo(true, false, tr("Для проверки подключитесь к VPN."), false, false);
    }
    void setRoute(const QString &name, bool canRestore) {
        routeDetail->setText(tr("Текущая схема: %1. Просмотр стандартных маршрутов ничего не применяет.")
                            .arg(name.isEmpty() ? tr("не выбрана") : name));
        restoreRulesButton->setVisible(canRestore);
    }
    void setGeo(bool enabled, bool online, const QString &text, bool checking, bool unused) {
        automatic->setChecked(enabled); // clicked is only emitted by a person
        automatic->setText(enabled ? tr("Автообновление: вкл") : tr("Автообновление: выкл"));
        geoDetail->setText(text);
        refreshGeoButton->setEnabled(online && !checking && !unused && QDateTime::currentDateTimeUtc() >= retryAfter);
    }
    void markGeoRequested() {
        retryAfter = QDateTime::currentDateTimeUtc().addSecs(30);
        refreshGeoButton->setEnabled(false);
        geoDetail->setText(tr("Запрос отправлен. Проверяем обновление базы…"));
    }
    QLabel *versionDetail = nullptr;
    QLabel *rollbackDetail = nullptr;
    QPushButton *updateButton = nullptr;
    QPushButton *rollbackButton = nullptr;
private:
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        if (onShown) onShown();
    }
    std::function<void()> onShown;
    QLabel *routeDetail = nullptr;
    QLabel *geoDetail = nullptr;
    QPushButton *automatic = nullptr;
    QPushButton *refreshGeoButton = nullptr;
    QPushButton *restoreRulesButton = nullptr;
    QDateTime retryAfter;
};
} // namespace GreenRhythm
