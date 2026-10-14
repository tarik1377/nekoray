#include "ui/MainShell.hpp"
#include "ui/MaintenancePanel.hpp"
#include "ui/RollbackAction.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QThread>
#include <QWindow>
#include <array>
#include <cstdio>
#include <cstring>

namespace {
int checks = 0, failures = 0;
void check(const QString &name, bool ok) {
    ++checks; failures += !ok;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.toUtf8().constData());
}
const QByteArray verifiedStatus = R"({"available":true,"version":"v1.9.2.6","platform":"windows-x64","provenance":"explicit-verified-local","files":43})";

enum Action { Defaults, OwnRules, RestoreRules, GeoRefresh, Automatic, Rollback, Updates, ActionCount };
struct Fixture {
    GreenRhythm::MainShell shell;
    std::array<int, ActionCount> fired{};
    QList<bool> automaticValues;
    int shown = 0;
    QWidget *panel = nullptr;
    QScrollArea *scroll = nullptr;
    explicit Fixture(bool showMaintenance = true) {
        shell.adopt(new QWidget, new QWidget);
        QObject::connect(&shell, &GreenRhythm::MainShell::routeDefaultsRequested, [&] { ++fired[Defaults]; });
        QObject::connect(&shell, &GreenRhythm::MainShell::routesRequested, [&] { ++fired[OwnRules]; });
        QObject::connect(&shell, &GreenRhythm::MainShell::restoreRoutesRequested, [&] { ++fired[RestoreRules]; });
        QObject::connect(&shell, &GreenRhythm::MainShell::geoDataRefreshRequested, [&] { ++fired[GeoRefresh]; });
        QObject::connect(&shell, &GreenRhythm::MainShell::geoDataAutomaticToggled,
                         [&](bool enabled) { ++fired[Automatic]; automaticValues.append(enabled); });
        QObject::connect(&shell, &GreenRhythm::MainShell::rollbackRequested, [&] { ++fired[Rollback]; });
        QObject::connect(&shell, &GreenRhythm::MainShell::checkUpdateRequested, [&] { ++fired[Updates]; });
        QObject::connect(&shell, &GreenRhythm::MainShell::maintenanceShown, [&] { ++shown; });
        panel = shell.findChild<QWidget *>("grMaintenancePanel");
        shell.resize(1000, 710);
        shell.show();
        if (showMaintenance) open();
        for (QWidget *parent = panel; parent; parent = parent->parentWidget()) {
            if ((scroll = qobject_cast<QScrollArea *>(parent))) break;
        }
        settle();
    }
    static void settle() {
        for (int i = 0; i < 5; ++i) { QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents(); }
    }
    void open() { shell.showPage(3); settle(); }
    QPushButton *button(const char *name) const { return shell.findChild<QPushButton *>(name); }
    QLabel *label(const char *name) const { return shell.findChild<QLabel *>(name); }
    void click(QPushButton *button) {
        if (!button) return;
        const auto point = button->mapTo(&shell, button->rect().center());
        const QPointF local(point), global(shell.mapToGlobal(point));
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(shell.windowHandle(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(shell.windowHandle(), &release);
        settle();
    }
    bool oneAction(const std::array<int, ActionCount> &before, Action intended) const {
        for (int i = 0; i < ActionCount; ++i)
            if (fired[i] != before[i] + (i == intended ? 1 : 0)) return false;
        return true;
    }
};

void hydrationAndEvents() {
    Fixture f(false);
    check("maintenance is hidden on the connection page", f.panel && !f.shell.maintenanceVisible() && f.shown == 0);
    if (!f.panel) return;
    const auto before = f.fired;
    for (int i = 0; i < 100; ++i) {
        f.shell.setMaintenanceRoute(i % 2 ? QStringLiteral("Россия") : QStringLiteral("Мои правила"), i % 3 == 0);
        f.shell.setMaintenanceGeo(i % 2, true, QStringLiteral("Последняя проверка: 15.10.2026 00:20. Диапазонов РФ: 25171."), false, false);
        f.shell.setAppVersion(QStringLiteral("1.9.2.6"));
        f.shell.setUpdateAvailable(i % 2, QStringLiteral("1.9.2.7"));
        GreenRhythm::Rollback::showStatus(f.button("grRollbackVersion"), f.label("grRollbackStatus"),
                                        GreenRhythm::Rollback::parseStatus(verifiedStatus));
    }
    check("100 status hydrations emit no action or show signals", f.fired == before && f.shown == 0);
    f.open();
    check("opening settings exposes maintenance and emits one show event", f.shell.maintenanceVisible() && f.shown == 1);
    f.open();
    check("reselecting the visible settings page does not reemit show", f.shown == 1);
    f.shell.setMaintenanceRoute(QStringLiteral("Россия"), true);
    f.shell.setMaintenanceGeo(false, true, QStringLiteral("Автообновление выключено; проверка доступна вручную."), false, false);
    f.shell.setUpdateAvailable(false, {});
    Fixture::settle();
    for (const auto &[id, intended] : QList<QPair<const char *, Action>>{
             {"grRouteDefaults", Defaults}, {"grOwnRoutes", OwnRules}, {"grRestoreRoutes", RestoreRules},
             {"grGeoDataRefresh", GeoRefresh}, {"grRollbackVersion", Rollback}, {"grCheckUpdates", Updates}}) {
        const auto previous = f.fired;
        auto *button = f.button(id);
        check(QStringLiteral("%1 exists and is enabled").arg(id), button && button->isEnabled() && button->isVisible());
        f.click(button);
        check(QStringLiteral("%1 mouse click emits exactly its intended action").arg(id), f.oneAction(previous, intended));
    }
    auto previous = f.fired;
    f.click(f.button("grGeoDataAutomatic"));
    check("automatic click emits one true value without unrelated actions", f.oneAction(previous, Automatic)
          && f.automaticValues == QList<bool>{true});
    previous = f.fired;
    f.click(f.button("grGeoDataAutomatic"));
    check("second automatic click emits one false value", f.oneAction(previous, Automatic)
          && f.automaticValues == QList<bool>{true, false});
    const auto liveCounters = f.fired;
    for (int i = 0; i < 100; ++i) {
        f.shell.setMaintenanceGeo(i % 2, true, QStringLiteral("Последняя проверка выполнена."), false, false);
        f.shell.setMaintenanceRoute(QStringLiteral("Россия"), i % 2);
    }
    check("visible polling remains silent and cannot rerun commands", f.fired == liveCounters && f.shown == 1);
    f.shell.showPage(0);
    Fixture::settle();
    check("leaving settings hides maintenance", !f.shell.maintenanceVisible());
    f.open();
    check("a real reentry emits one new show event", f.shown == 2);
}

void stateAndCooldown() {
    Fixture f;
    auto *refresh = f.button("grGeoDataRefresh");
    auto *automatic = f.button("grGeoDataAutomatic");
    auto *restore = f.button("grRestoreRoutes");
    if (!refresh || !automatic || !restore) { check("state controls exist", false); return; }
    for (const auto &state : QList<std::array<bool, 4>>{{true, false, false, false}, {true, true, false, true}, {true, true, true, false}}) {
        f.shell.setMaintenanceGeo(state[0], state[1], QStringLiteral("Статус базы"), state[2], state[3]);
        const auto before = f.fired;
        f.click(refresh);
        check("offline/unused/checking disables manual refresh without emitting", !refresh->isEnabled() && f.fired == before);
    }
    f.shell.setMaintenanceGeo(false, true, QStringLiteral("Автообновление выключено."), false, false);
    check("automatic disabled still permits explicit manual refresh", !automatic->isChecked() && refresh->isEnabled());
    f.shell.setMaintenanceRoute(QStringLiteral("Россия"), false);
    Fixture::settle();
    check("restore action is hidden when no route backup exists", restore->isHidden());
    f.shell.setMaintenanceRoute(QStringLiteral("Россия"), true);
    Fixture::settle();
    check("restore action reappears only with an available route backup", restore->isVisible());
    auto *rollback = f.button("grRollbackVersion");
    auto *status = f.label("grRollbackStatus");
    GreenRhythm::Rollback::showStatus(rollback, status, {false, {}, {}, QStringLiteral("Нет проверенной копии.")});
    const auto before = f.fired;
    f.click(rollback);
    check("disabled rollback cannot emit an action", !rollback->isEnabled() && f.fired == before && f.shell.rollbackVersion().isEmpty());

    f.shell.markGeoRefreshRequested();
    const auto requested = QDateTime::currentDateTimeUtc();
    check("request immediately disables refresh and reports pending work", !refresh->isEnabled()
          && f.label("grGeoDataStatus")->text().contains(QStringLiteral("Запрос отправлен")));
    // A real timer boundary, not a copied boolean rule: keep rendering fresh
    // status while the production widget's 30-second deadline expires.
    QElapsedTimer elapsed;
    elapsed.start();
    bool enabledTooSoon = false;
    while (elapsed.elapsed() < 30500) {
        f.shell.setMaintenanceGeo(false, true, QStringLiteral("Проверка завершена, статус обновлён опросом."), false, false);
        if (QDateTime::currentDateTimeUtc() < requested.addMSecs(29900) && refresh->isEnabled()) enabledTooSoon = true;
        if (elapsed.elapsed() >= 30100 && refresh->isEnabled()) break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(25);
    }
    check("polling cannot clear the pending request cooldown early", !enabledTooSoon);
    check("polling does not extend the original cooldown forever", refresh->isEnabled() && elapsed.elapsed() < 30500);
}

void geometry() {
    Fixture f;
    f.shell.setMaintenanceRoute(QStringLiteral("Россия · мои рабочие исключения"), true);
    f.shell.setMaintenanceGeo(true, true, QStringLiteral("Рабочая база: 15.10.2026 00:20. Диапазонов РФ: 25171.\n"
        "Автоматическая проверка раз в сутки; последняя проверка выполнена."), false, false);
    f.shell.setAppVersion("1.9.2.6");
    GreenRhythm::Rollback::showStatus(f.button("grRollbackVersion"), f.label("grRollbackStatus"),
                                    GreenRhythm::Rollback::parseStatus(verifiedStatus));
    Fixture::settle();
    check("maintenance uses the requested compact 1000 by 710 window", f.shell.size() == QSize(1000, 710));
    check("settings starts at the first viewport", f.scroll && f.scroll->verticalScrollBar()->value() == 0);
    if (!f.scroll || !f.panel) return;
    auto *viewport = f.scroll->viewport();
    for (const auto *id : {"grRouteDefaults", "grOwnRoutes", "grRestoreRoutes", "grGeoDataAutomatic", "grGeoDataRefresh", "grRollbackVersion", "grCheckUpdates"}) {
        const auto *button = f.button(id);
        const QRect position(button->mapTo(viewport, QPoint()), button->size());
        check(QStringLiteral("%1 is fully visible in the first viewport").arg(id), button->isVisible()
              && viewport->rect().contains(position));
        QStyleOptionButton option;
        option.initFrom(button);
        const QRect content = button->style()->subElementRect(QStyle::SE_PushButtonContents, &option, button);
        check(QStringLiteral("%1 text fits without clipping").arg(id), button->fontMetrics().horizontalAdvance(button->text()) <= content.width()
              && button->fontMetrics().height() <= content.height());
    }
    for (auto *label : f.panel->findChildren<QLabel *>()) {
        if (!label->isVisible() || label->text().isEmpty()) continue;
        const QRect position(label->mapTo(viewport, QPoint()), label->size());
        const int flags = Qt::TextShowMnemonic | (label->wordWrap() ? Qt::TextWordWrap : Qt::TextSingleLine);
        const auto required = label->fontMetrics().boundingRect(QRect(0, 0, label->contentsRect().width(), 10000), flags, label->text());
        if (required.width() > label->contentsRect().width() || required.height() > label->contentsRect().height())
            std::printf("LABEL %s contents %dx%d required %dx%d heightForWidth=%d sizeHint=%dx%d font=%s\n", label->objectName().toUtf8().constData(),
                        label->contentsRect().width(), label->contentsRect().height(), required.width(), required.height(), label->heightForWidth(label->width()),
                        label->sizeHint().width(), label->sizeHint().height(), label->font().toString().toUtf8().constData());
        check(QStringLiteral("label %1 is visible and its text is not clipped").arg(label->objectName()), viewport->rect().contains(position)
              && required.width() <= label->contentsRect().width() && required.height() <= label->contentsRect().height());
    }
    check("maintenance never forces horizontal scrolling", f.scroll->horizontalScrollBar()->maximum() == 0);
}

void actualRollbackHelper() {
#ifdef Q_OS_WIN
    Fixture f;
    QTemporaryDir directory;
    check("isolated rollback helper directory exists", directory.isValid());
    if (!directory.isValid()) return;
    check("synthetic helper is copied outside the installed application", QFile::copy(QCoreApplication::applicationFilePath(), directory.filePath("updater.exe")));
    f.shell.refreshRollbackStatus(directory.path());
    check("rollback is disabled while the actual asynchronous helper runs", !f.button("grRollbackVersion")->isEnabled());
    QElapsedTimer timer;
    timer.start();
    while (!f.button("grRollbackVersion")->isEnabled() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    check("verified helper status enables rollback and supplies its version", f.button("grRollbackVersion")->isEnabled()
          && f.shell.rollbackVersion() == "v1.9.2.6" && f.label("grRollbackStatus")->text().contains(QStringLiteral("локальная")));
    const auto before = f.fired;
    f.click(f.button("grRollbackVersion"));
    check("verified rollback click emits exactly one rollback request", f.oneAction(before, Rollback));
#endif
}
}

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--rollback-info") == 0) {
        std::fwrite(verifiedStatus.constData(), 1, verifiedStatus.size(), stdout);
        return 0;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    // Match the shipped 10 pt profile from main/AppFont.cpp when measuring the
    // real widgets; the platform's generic 9 pt default hides compact clipping.
    QFont font(QFontDatabase::families().contains("Segoe UI Variable Text") ? "Segoe UI Variable Text" : "Segoe UI", 10);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);
#endif
    if (argc == 2 && std::strcmp(argv[1], "--geometry-only") == 0) {
        geometry();
        return failures ? 1 : 0;
    }
    hydrationAndEvents();
    geometry();
    actualRollbackHelper();
    stateAndCooldown();
    std::printf("Maintenance panel: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
