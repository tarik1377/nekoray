#pragma once

#include "ui/SettingsWidgets.hpp"
#include <QDateTime>
#include <QHBoxLayout>
#include <QGridLayout>
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
        grid = new QGridLayout(this);
        grid->setContentsMargins(0, 0, 0, 0); grid->setSpacing(12);
        routeCard = new SettingsCard(tr("Маршруты"), "gr-routes", this);
        routeCard->setObjectName("grMaintenanceRoutes");
        routeCard->detail->setText(tr("Выберите стандартные маршруты или настройте свои правила."));
        routeDetail = new QLabel(routeCard); routeDetail->setObjectName("grCurrentRoutesDetail");
        routeDetail->setTextFormat(Qt::PlainText); routeDetail->setWordWrap(true);
        routeDetail->setStyleSheet(QStringLiteral("color:%1; background:transparent; border:none;").arg(Palette::kAccent));
        auto font = routeDetail->font(); font.setPointSizeF(font.pointSizeF() * 0.88); routeDetail->setFont(font);
        routeDetail->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        routeCard->header->addWidget(routeDetail, 1);
        auto button = [&](SettingsCard *card, const QString &name, const QString &title,
                          std::function<void()> action, bool primary = false) {
            auto *result = card->addAction(name, title, primary);
            if (action) connect(result, &QPushButton::clicked, this, [action] { action(); });
            return result;
        };
        auto *defaults = button(routeCard, "grRouteDefaults", tr("Стандартные маршруты…"), actions.defaults, true);
        defaults->setToolTip(tr("Сначала откроется просмотр изменений. Маршруты применяются только после подтверждения."));
        button(routeCard, "grOwnRoutes", tr("Мои правила…"), actions.rules);
        restoreRulesButton = new QPushButton(tr("Вернуть прежние правила…"), routeCard);
        restoreRulesButton->setObjectName("grRestoreRoutes");
        restoreRulesButton->setFlat(true);
        restoreRulesButton->setMinimumHeight(30);
        restoreRulesButton->setCursor(Qt::PointingHandCursor);
        restoreRulesButton->setToolTip(tr("Вернуть копию перед последним применением стандартных маршрутов. Сначала появится подтверждение."));
        connect(restoreRulesButton, &QPushButton::clicked, this, [actions] { if (actions.restoreRules) actions.restoreRules(); });
        restoreRulesButton->setStyleSheet(QStringLiteral("QPushButton { color:%1; border:none; background:transparent; text-align:left; padding:0; } QPushButton:hover,QPushButton:focus { color:%2; }")
                                             .arg(Palette::kMuted, Palette::kAccent));
        routeCard->box->addWidget(restoreRulesButton, 0, Qt::AlignLeft);

        geoCard = new SettingsCard(tr("Адреса России · GeoIP"), "gr-refresh", this);
        geoCard->setObjectName("grMaintenanceGeo");
        geoDetail = geoCard->detail;
        geoDetail->setObjectName("grGeoDataStatus");
        automatic = button(geoCard, "grGeoDataAutomatic", tr("Автообновление"), {});
        automatic->setCheckable(true);
        automatic->setAccessibleName(tr("Автоматически обновлять базу адресов РФ"));
        connect(automatic, &QPushButton::clicked, this, [actions](bool enabled) { if (actions.automaticGeo) actions.automaticGeo(enabled); });
        refreshGeoButton = button(geoCard, "grGeoDataRefresh", tr("Проверить сейчас"), actions.refreshGeo);
        refreshGeoButton->setAccessibleName(tr("Проверить обновление базы адресов РФ"));

        versionCard = new SettingsCard(tr("Обновления и восстановление"), "gr-history", this);
        versionCard->setObjectName("grMaintenanceVersion");
        versionDetail = versionCard->detail;
        versionDetail->setObjectName("grCheckUpdatesDetail");
        rollbackDetail = new QLabel(tr("Проверяем сохранённую версию…"), this);
        rollbackDetail->setObjectName("grRollbackStatus");
        rollbackDetail->setWordWrap(true);
        rollbackDetail->setTextFormat(Qt::PlainText);
        rollbackDetail->setStyleSheet(QStringLiteral("color:%1;").arg(Palette::kMuted));
        font = rollbackDetail->font(); font.setPointSizeF(font.pointSizeF() * 0.9); rollbackDetail->setFont(font);
        versionCard->box->insertWidget(3, rollbackDetail);
        updateButton = button(versionCard, "grCheckUpdates", tr("Проверить обновления"), actions.updates);
        rollbackButton = button(versionCard, "grRollbackVersion", tr("Вернуть предыдущую…"), actions.rollback);
        rollbackButton->setEnabled(false);
        rollbackButton->setToolTip(tr("Потребуется перезапуск GreenRhythm. Серверы и настройки сохранятся."));
        layoutCards(false);
        setRoute({}, false);
        setGeo(true, false, tr("Для проверки подключитесь к VPN."), false, false);
    }
    void setRoute(const QString &name, bool canRestore) {
        routeDetail->setText(tr("Схема: %1")
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
    int filter(const QString &query) {
        int count = 0;
        for (auto *card : {routeCard, geoCard, versionCard}) if (card->filter(query)) ++count;
        // Keep the panel logically visible: filtering must not replay onShown.
        setMaximumHeight(count ? QWIDGETSIZE_MAX : 0);
        return count;
    }
    QLabel *versionDetail = nullptr;
    QLabel *rollbackDetail = nullptr;
    QPushButton *updateButton = nullptr;
    QPushButton *rollbackButton = nullptr;
private:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        const bool wide = width() >= 640;
        if (wide != twoColumns) layoutCards(wide);
    }
    void layoutCards(bool wide) {
        twoColumns = wide;
        for (auto *card : {routeCard, geoCard, versionCard}) grid->removeWidget(card);
        grid->addWidget(routeCard, 0, 0, 1, wide ? 2 : 1);
        grid->addWidget(geoCard, 1, 0);
        grid->addWidget(versionCard, wide ? 1 : 2, wide ? 1 : 0);
        grid->setColumnStretch(0, 1); grid->setColumnStretch(1, wide ? 1 : 0);
    }
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
    QGridLayout *grid = nullptr;
    SettingsCard *routeCard = nullptr, *geoCard = nullptr, *versionCard = nullptr;
    bool twoColumns = false;
};
} // namespace GreenRhythm
