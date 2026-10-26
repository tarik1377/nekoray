// Renders the real settings widgets with fixture values. No MainWindow, core,
// user files or network handlers are constructed.
#include "ui/MainShell.hpp"
#include "ui/RollbackAction.hpp"
#include "main/AppFont.hpp"
#include "test/WidgetTestFonts.hpp"
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    registerWidgetTestFonts();
    GreenRhythm::applyAppFont(app);
    if (argc < 2) return 2;
    QFile style("res/theme/feiyangqingyun/qss/modern.css");
    if (!style.open(QIODevice::ReadOnly)) return 3;
    app.setStyleSheet(QString::fromUtf8(style.readAll()));
    GreenRhythm::MainShell shell;
    shell.adopt(new QWidget, new QWidget);
    shell.setConnectionState(true, QStringLiteral("Turkey-admin"), QStringLiteral("42 мс"));
    shell.setModes(true, false, true, false);
    shell.setAppOptions(true, false, 120);
    shell.setAppVersion(QStringLiteral("2.1.4"));
    shell.setUpdateAvailable(false, {});
    shell.setConnectionOptions(true, true, false, QStringLiteral("192.168.1.10"), 2080);
    shell.setMaintenanceRoute(QStringLiteral("Default"), false);
    shell.setMaintenanceGeo(true, true, QStringLiteral("Проверено: 26.10.2026 14:31 · диапазонов РФ: 25107.\nПроверяется раз в сутки, без перезапуска туннеля."), false, false);
    GreenRhythm::Rollback::showStatus(shell.findChild<QPushButton *>("grRollbackVersion"),
        shell.findChild<QLabel *>("grRollbackStatus"), GreenRhythm::Rollback::parseStatus(
            QByteArrayLiteral("{\"available\":true,\"version\":\"2.1.3\",\"provenance\":\"signed-release\",\"platform\":\"windows-x64\",\"files\":43}")));
    shell.resize(argc > 2 ? QByteArray(argv[2]).toInt() : 1180, argc > 3 ? QByteArray(argv[3]).toInt() : 820);
    shell.showPage(3);
    shell.show();
    for (int i = 0; i < 8; ++i) { app.sendPostedEvents(); app.processEvents(); }
    if (argc > 4) shell.findChild<QLineEdit *>("grSettingsSearch")->setText(QString::fromLocal8Bit(argv[4]));
    for (int i = 0; i < 8; ++i) { app.sendPostedEvents(); app.processEvents(); }
    if (argc > 5 && QByteArray(argv[5]) == "end") {
        auto *scroll = shell.findChild<QScrollArea *>("grSettingsScroll");
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        app.processEvents();
    }
    return shell.grab().save(QString::fromLocal8Bit(argv[1])) ? 0 : 4;
}
