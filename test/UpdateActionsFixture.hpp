#pragma once
#include <QCoreApplication>
#include "3rdparty/QThreadCreateThread.hpp"
#include "ui/UpdateNotice.hpp"
#include "go/grpc_server/gen/libcore.pb.h"
#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressDialog>
#include <QPointer>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>
#include <QWidget>
#include <atomic>
#include <functional>

inline int fixtureWarnings = 0, fixtureInfos = 0;
inline QString fixtureLastWarning;
inline int MessageBoxWarning(const QString &, const QString &text) { ++fixtureWarnings; fixtureLastWarning = text; return 0; }
inline int MessageBoxInfo(const QString &, const QString &) { ++fixtureInfos; return 0; }
inline std::atomic<int> fixtureWorkers{0};
inline void runOnNewThread(const std::function<void()> &work) {
    ++fixtureWorkers;
    createQThread([work] { work(); --fixtureWorkers; })->start();
}
namespace NekoGui {
struct FixtureStore { bool check_include_pre = false, flag_use_appdata = false; };
inline FixtureStore *dataStore = nullptr;
}
namespace NekoGui_rpc {
struct FixtureClient {
    std::atomic<int> checks{0}, downloads{0};
    bool checkOK = true, downloadOK = true;
    libcore::UpdateResp checked, downloaded;
    std::function<void()> onDownload;
    libcore::UpdateResp Update(bool *ok, const libcore::UpdateReq &request) {
        if (request.action() == libcore::UpdateAction::Check) { ++checks; *ok = checkOK; return checked; }
        ++downloads; if (onDownload) onDownload(); *ok = downloadOK; return downloaded;
    }
};
inline FixtureClient *defaultClient = nullptr;
}
namespace GreenRhythm {
struct MainShell {
    bool available = false;
    QString version;
    void setUpdateAvailable(bool value, const QString &found = {}) { available = value; version = found; }
};
}
class MainWindow : public QWidget {
public:
    GreenRhythm::MainShell fixtureShell;
    GreenRhythm::MainShell *shell = &fixtureShell;
    int exit_reason = 0, exits = 0;
    MainWindow() = default;
    ~MainWindow() override = default;
    void CheckUpdate(bool quiet = false);
    void on_menu_exit_triggered() { ++exits; }
};
