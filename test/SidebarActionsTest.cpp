#include "ui/MainShell.hpp"
#include <QApplication>
#include <QPushButton>
#include <QStackedWidget>
#include <iostream>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    GreenRhythm::MainShell shell;
    shell.adopt(new QWidget, new QWidget);
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
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
