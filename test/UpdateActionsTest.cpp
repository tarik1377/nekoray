#include "UpdateActionsFixture.hpp"
#include <QElapsedTimer>
#include <QEvent>
#include <QAbstractButton>
#include <QSemaphore>
#include <cstdio>

#include GR_UPDATE_METHOD

static int checks = 0, failures = 0;
static void check(const char *name, bool ok) { ++checks; failures += !ok; std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name); }
static void drain() {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 2000 && (timer.elapsed() < 80 || fixtureWorkers != 0)) {
        QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QThread::msleep(1);
    }
}
struct Fixture {
    NekoGui::FixtureStore store;
    NekoGui_rpc::FixtureClient client;
    MainWindow window;
    QTimer dialogs;
    int offered = 0, ready = 0;
    bool downloadDone = false;
    Fixture() {
        fixtureWarnings = fixtureInfos = 0; fixtureLastWarning.clear();
        NekoGui::dataStore = &store; NekoGui_rpc::defaultClient = &client;
        dialogs.setInterval(1);
        QObject::connect(&dialogs, &QTimer::timeout, &window, [this] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            if (box->text().contains("Update found:")) {
                ++offered;
                for (auto *button : box->buttons()) if (box->buttonRole(button) == QMessageBox::AcceptRole && button->text() == "Update") { button->click(); return; }
                box->reject();
            } else {
                ++ready;
                if (auto *yes = box->button(QMessageBox::Yes)) yes->click();
                else box->reject(); // Records a fake exit; launches nothing.
            }
        });
        dialogs.start();
    }
    void offer() { client.checked.set_download_url("https://fixture.invalid/update.zip"); client.checked.set_assets_name("GreenRhythm-v9.0.0-windows-x64.zip"); }
    void finishDownload() {
        QElapsedTimer timer; timer.start();
        do {
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QThread::msleep(1);
        } while (timer.elapsed() < 2000 &&
                 (client.downloads == 0 || !window.findChildren<QProgressDialog *>().isEmpty() || QApplication::activeModalWidget()));
    }
};
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    {
        Fixture f; f.offer(); f.client.downloadOK = false;
        f.window.CheckUpdate(); f.finishDownload();
        check("an RPC download failure reaches the actual download action", f.offered == 1 && f.client.downloads == 1);
        check("an RPC failure is never announced as a downloaded update", f.ready == 0);
        check("a failed update cannot request application exit", f.window.exits == 0 && f.window.exit_reason == 0);
        check("a failed RPC reports an error and releases progress UI", fixtureWarnings == 1 && f.window.findChildren<QProgressDialog *>().isEmpty());
    }
    {
        Fixture f; f.offer(); f.client.downloaded.set_error("fixture server unavailable: 503");
        f.window.CheckUpdate(); f.finishDownload();
        check("a server download error preserves the running application", f.ready == 0 && f.window.exits == 0);
        check("the server error is reported and the progress dialog is removed", fixtureWarnings == 1 && fixtureLastWarning.contains("503") && f.window.findChildren<QProgressDialog *>().isEmpty());
    }
    {
        Fixture f; f.client.checked.set_error("fixture DNS failure");
        f.window.CheckUpdate(); drain();
        check("a manual unavailable-site check reports its error", fixtureWarnings == 1 && fixtureLastWarning.contains("DNS"));
        check("an unavailable site is not reported as no updates", fixtureInfos == 0 && f.ready == 0 && f.client.downloads == 0);
    }
    {
        Fixture f; f.client.checked.set_error("fixture TLS error");
        f.window.CheckUpdate(true); drain();
        check("an automatic failed check remains quiet", fixtureWarnings == 0 && fixtureInfos == 0 && f.offered == 0);
        check("an automatic network error does not request an exit", f.window.exits == 0 && f.client.downloads == 0);
    }
    {
        Fixture f; f.client.checkOK = false; f.window.fixtureShell.available = true;
        f.window.CheckUpdate(); drain();
        check("an RPC check failure neither clears existing availability nor claims no update", f.window.fixtureShell.available && fixtureInfos == 0 && f.window.exits == 0);
        check("a manual transport failure offers a retryable error", fixtureWarnings == 1 && !fixtureLastWarning.isEmpty());
    }
    {
        Fixture f; f.offer();
        f.window.CheckUpdate(); f.finishDownload();
        check("a verified successful download still offers installation", f.ready == 1 && f.client.downloads == 1);
        check("only the successful download and explicit confirmation request an exit", f.window.exits == 1 && f.window.exit_reason == 1 && fixtureWarnings == 0);
    }
    {
        Fixture f; f.offer(); f.window.CheckUpdate(true); drain();
        check("an automatic successful check updates the version indication", f.window.fixtureShell.available && f.window.fixtureShell.version == "9.0.0");
        check("an automatic check never starts a download or opens a prompt", f.client.downloads == 0 && f.offered == 0 && f.ready == 0);
    }
    {
        Fixture f; f.window.fixtureShell.available = true; f.window.CheckUpdate(true); drain();
        check("a genuine no-update response clears availability quietly", !f.window.fixtureShell.available && fixtureInfos == 0 && fixtureWarnings == 0);
    }
    {
        Fixture f; NekoGui_rpc::defaultClient = nullptr; f.window.CheckUpdate(); drain();
        check("a missing RPC client fails safely on manual check", fixtureWarnings == 1 && f.client.checks == 0 && f.window.exits == 0);
        fixtureWarnings = 0; f.window.CheckUpdate(true); drain();
        check("a missing RPC client stays quiet on automatic check", fixtureWarnings == 0 && f.client.checks == 0);
    }
    {
        NekoGui::FixtureStore store; NekoGui_rpc::FixtureClient client;
        NekoGui::dataStore = &store; NekoGui_rpc::defaultClient = &client;
        fixtureWarnings = fixtureInfos = 0;
        client.checked.set_download_url("https://fixture.invalid/update.zip");
        client.checked.set_assets_name("fixture-v9.0.0.zip");
        QSemaphore gate; client.onDownload = [&] { gate.acquire(); };
        auto *window = new MainWindow;
        QTimer dialogs; dialogs.setInterval(1);
        QObject::connect(&dialogs, &QTimer::timeout, &app, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            for (auto *button : box->buttons()) if (button->text() == "Update") { button->click(); return; }
            box->reject();
        }); dialogs.start();
        window->CheckUpdate();
        QElapsedTimer timer; timer.start();
        while (client.downloads == 0 && timer.elapsed() < 2000) { QCoreApplication::processEvents(); QThread::msleep(1); }
        check("a controlled stalled download leaves GUI events available", client.downloads == 1 && !window->findChildren<QProgressDialog *>().isEmpty());
        delete window; gate.release(); drain();
        check("a late response after window deletion cannot show readiness or request exit", fixtureWorkers == 0 && !QApplication::activeModalWidget() && fixtureWarnings == 0);
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
