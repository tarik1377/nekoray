#include "ui/MainShell.hpp"
#include "main/BrandingConstants.hpp"
#include "main/AppFont.hpp"
#include <QApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QWindow>
#include <iostream>

static QString readSource(const QString &path) {
    for (const QString &prefix : {QString(), QStringLiteral("../"), QStringLiteral("../../")}) {
        QFile file(prefix + path);
        if (file.open(QIODevice::ReadOnly)) return QString::fromUtf8(file.readAll());
    }
    return {};
}

int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    GreenRhythm::applyAppFont(app);
    GreenRhythm::MainShell shell;
    const QString theme = readSource(QStringLiteral("res/theme/feiyangqingyun/qss/modern.css"));
    shell.setStyleSheet(theme);
    shell.adopt(new QWidget, new QWidget);
    shell.resize(1000, 640);
    shell.show();
    app.processEvents();
    auto *pages = shell.findChild<QStackedWidget *>("grPages");
    auto *logs = shell.findChild<QPushButton *>("grNav2");
    auto *routes = shell.findChild<QPushButton *>("grSidebarRoutes");
    auto *buy = shell.findChild<QPushButton *>("grSidebarBuy");
    int failures = 0, checks = 0, routeCalls = 0, buyCalls = 0;
    auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) ++failures;
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    };
    check(!theme.isEmpty(), "compact window uses production font and theme");
    check(pages && pages->count() == 4, "existing pages are retained");
    check(logs && routes && buy, "log, routing and purchase are available in sidebar");
    QObject::connect(&shell, &GreenRhythm::MainShell::routesRequested, [&] { ++routeCalls; });
    QObject::connect(&shell, &GreenRhythm::MainShell::buyRequested, [&] { ++buyCalls; });
    if (logs) logs->click();
    check(pages && pages->currentIndex() == 2 && logs && logs->isChecked(), "log button opens existing logs page");
    if (routes) routes->click();
    check(routeCalls == 1 && pages->currentIndex() == 2, "routing button requests existing routing dialog once");
    if (buy) buy->click();
    check(buyCalls == 1 && pages->currentIndex() == 2, "purchase button requests website without changing selected page");
    shell.showPage(0);
    check(logs && !logs->isChecked(), "navigation selection follows page changes");

    auto *power = shell.findChild<QPushButton *>("grPower");
    auto *status = shell.findChild<QLabel *>("grStatusTitle");
    int powerCalls = 0;
    QObject::connect(&shell, &GreenRhythm::MainShell::connectToggled, [&] { ++powerCalls; });
    shell.setConnectionState(true, QStringLiteral("Test server"), QStringLiteral("32 ms"));
    const QString connectedStatus = status ? status->text() : QString();
    for (int page : {1, 2, 3, 0}) {
        auto *button = shell.findChild<QPushButton *>(QStringLiteral("grNav%1").arg(page));
        if (button) button->click();
        int selected = 0;
        for (int i = 0; i < 4; ++i) {
            auto *nav = shell.findChild<QPushButton *>(QStringLiteral("grNav%1").arg(i));
            if (nav && nav->isChecked()) ++selected;
        }
        check(button && button->isChecked() && pages->currentIndex() == page && selected == 1,
              "each page keeps exactly one navigation selection");
    }
    if (routes) routes->click();
    if (buy) buy->click();
    check(powerCalls == 0 && status && status->text() == connectedStatus && power && power->isEnabled(),
          "navigation and actions preserve connected state without toggling VPN");
    shell.setState(GreenRhythm::MainShell::State::Connecting);
    if (logs) logs->click();
    check(shell.isConnecting() && power && !power->isEnabled(), "logs preserve pending connection attempt");
    shell.finishConnecting();
    for (auto *button : {logs, routes, buy}) {
        check(button && button->isVisible() && button->isEnabled() && !button->accessibleName().isEmpty()
                  && button->focusPolicy() != Qt::NoFocus && !button->icon().isNull(),
              "restored sidebar action is visible, labeled, focusable and has an icon");
    }
    check(routes && buy && !routes->isCheckable() && !buy->isCheckable(),
          "dialog and browser actions do not become page selections");
    check(GreenRhythm::kBuyUrl == QStringLiteral("https://verdantvibe.ru/subscriptions"),
          "purchase points to subscriptions website");
    const QString source = readSource(QStringLiteral("ui/mainwindow.cpp"));
    auto handler = [&](const QString &signal) {
        const int start = source.indexOf(QStringLiteral("connect(shell, &GreenRhythm::MainShell::") + signal);
        const int end = start >= 0 ? source.indexOf(QStringLiteral("});"), start) : -1;
        return end >= 0 ? source.mid(start, end - start) : QString();
    };
    check(!source.isEmpty() && handler(QStringLiteral("routesRequested")).contains(QStringLiteral("on_menu_routing_settings_triggered()")),
          "main window routes signal is wired to existing routing handler");
    check(!source.isEmpty() && handler(QStringLiteral("buyRequested")).contains(QStringLiteral("QDesktopServices::openUrl(QUrl(GreenRhythm::kBuyUrl))")),
          "main window purchase signal is wired to subscriptions URL");

    auto *sidebar = shell.findChild<QWidget *>("grSidebar");
    auto *scroll = shell.findChild<QScrollArea *>("grSidebarScroll");
    // The legacy server toolbar has its own minimum width. Exercise the actual
    // sidebar in a compact host without letting that unrelated page widen it.
    QWidget compact;
    auto *compactLayout = new QHBoxLayout(&compact);
    compactLayout->setContentsMargins(0, 0, 0, 0);
    if (scroll) compactLayout->addWidget(scroll);
    compactLayout->addWidget(new QWidget(&compact), 1);
    compact.resize(800, 600);
    compact.show();
    app.processEvents();
    for (const QSize size : {QSize(800, 600), QSize(1000, 640), QSize(1180, 720)}) {
        for (bool notices : {false, true}) {
            shell.setUpdateAvailable(notices, QStringLiteral("1.9.3"));
            shell.setSubscription(notices ? QStringLiteral("27 дн. · 84 ГБ") : QString(), false);
            compact.resize(size);
            app.processEvents();
            check(scroll && compact.height() <= size.height() && compact.width() <= size.width(),
                  "sidebar does not force compact window to grow");
            bool fits = sidebar && sidebar->layout();
            QRect previous;
            if (fits) for (int i = 0; i < sidebar->layout()->count(); ++i) {
                auto *widget = sidebar->layout()->itemAt(i)->widget();
                if (!widget || !widget->isVisible()) continue;
                const QRect rect = widget->geometry();
                fits &= sidebar->rect().contains(rect) && (!previous.isValid() || rect.top() > previous.bottom());
                previous = rect;
            }
            check(fits, "sidebar navigation, notices and status fit without overlap");
            bool reachable = sidebar;
            if (sidebar) for (auto *button : sidebar->findChildren<QPushButton *>()) {
                if (!button->isVisible()) continue;
                if (scroll) scroll->ensureWidgetVisible(button, 0, 0);
                app.processEvents();
                const QPoint center = button->mapTo(&compact, button->rect().center());
                reachable &= compact.rect().contains(center) && compact.childAt(center) == button;
            }
            check(reachable, "all sidebar actions remain reachable by mouse in compact windows");
            check(!scroll || (sidebar->width() <= scroll->viewport()->width()
                              && scroll->horizontalScrollBar()->maximum() == 0),
                  "sidebar has no clipped width or horizontal scrolling");
        }
    }
    shell.showPage(0);
    for (auto *button : {logs, routes, buy}) {
        if (!button) continue;
        if (scroll) scroll->ensureWidgetVisible(button, 0, 0);
        app.processEvents();
        const QPointF local(button->mapTo(&compact, button->rect().center()));
        const QPointF global(compact.mapToGlobal(local.toPoint()));
        const int beforeRoutes = routeCalls, beforeBuy = buyCalls;
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(compact.windowHandle(), &press);
        QCoreApplication::sendEvent(compact.windowHandle(), &release);
        app.processEvents();
        check(button == logs ? pages->currentIndex() == 2
                             : button == routes ? routeCalls == beforeRoutes + 1 : buyCalls == beforeBuy + 1,
              "restored sidebar action responds to mouse delivered through the window");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
