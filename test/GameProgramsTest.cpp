#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "main/GameAppRules.hpp"
#include "main/GameVoiceRouting.hpp"
#include "ui/GameProgramsDialog.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

namespace Rules = GreenRhythm::GameAppRules;
using GreenRhythm::GameProgramsDialog;
static int failures = 0, checks = 0;
static void check(bool value, const char *text) { ++checks; if (!value) ++failures; std::printf("%s %s\n", value ? "PASS" : "FAIL", text); std::fflush(stdout); }
static QByteArray read(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
static void write(const QString &path, const QByteArray &bytes) { QFile file(path); file.open(QIODevice::WriteOnly); file.write(bytes); }
static QComboBox *choice(GameProgramsDialog &dialog, const QString &name) {
    auto *table = dialog.findChild<QTableWidget *>("gamePrograms");
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toStringList().contains(name, Qt::CaseInsensitive))
            return static_cast<QComboBox *>(table->cellWidget(row, 1));
    return nullptr;
}
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const auto empty = Rules::serialize({});
    QJsonObject programs; QString error;
    check(Rules::parse({}, &programs) && programs.isEmpty(), "missing preference does not change existing profiles");
    check(Rules::setMode(&programs, "dota2.exe", "bypass") && Rules::setMode(&programs, "Discord.exe", "proxy"), "game and voice can choose independent directions");
    check(Rules::setMode(&programs, "DOTA2.EXE", "proxy") && programs.size() == 2 && Rules::mode(programs, "dota2.exe") == "proxy", "case-insensitive edits replace exactly one program");
    const auto beforeVoice = programs.value("Discord.exe");
    check(Rules::setMode(&programs, "dota2.exe", "auto") && programs.size() == 1 && programs.value("Discord.exe") == beforeVoice, "automatic mode removes only that program override");
    for (const auto &name : {"", "*.exe", "bad/path.exe", "C:\\game.exe", "bad\n.exe", "not-executable.txt", ".exe"})
        check(!Rules::setMode(&programs, name, "bypass"), "invalid/wildcard/path choices are rejected");
    check(!Rules::parse("{\"schema\":2,\"programs\":{}}", &programs), "future schemas are preserved rather than guessed");
    check(!Rules::parse("{\"schema\":1,\"programs\":{\"dota2.exe\":\"bypass\",\"DOTA2.exe\":\"proxy\"}}", &programs), "ambiguous duplicate names are rejected");

    QTemporaryDir files;
    const auto settingsPath = files.filePath("settings.json"), routesPath = files.filePath("Default");
    const QJsonObject settings{{"unknown_future_setting", QJsonArray{1, 2}}, {"custom_route_global", "keep exactly"}, {"vpn_bypass_process", "old.exe"}};
    write(settingsPath, QJsonDocument(settings).toJson()); write(routesPath, "original route bytes\n");
    const auto routeBytes = read(routesPath);
    const auto changed = Rules::serialize({{"dota2.exe", "bypass"}, {"Discord.exe", "proxy"}});
    check(Rules::saveSetting(settingsPath, {}, changed, &error), "atomic preference save succeeds in isolated settings");
    auto after = QJsonDocument::fromJson(read(settingsPath)).object(); after.remove("game_app_rules");
    check(after == settings && read(routesPath) == routeBytes, "unknown settings, old process lists and route files are unchanged");
    const auto savedBytes = read(settingsPath);
    check(!Rules::saveSetting(settingsPath, {}, empty, &error) && read(settingsPath) == savedBytes, "stale dialog cannot overwrite a newer choice");
#ifdef _WIN32
    const auto native = settingsPath.toStdWString();
    HANDLE held = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(held != INVALID_HANDLE_VALUE, "Windows fixture holds the settings file against replacement");
    if (held != INVALID_HANDLE_VALUE) {
        check(!Rules::saveSetting(settingsPath, changed, empty, &error) && read(settingsPath) == savedBytes,
              "failed real atomic replacement preserves the previous preference and settings");
        CloseHandle(held);
        check(Rules::saveSetting(settingsPath, changed, empty, &error), "saving retries successfully after releasing the file lock");
    }
#endif
    check(!Rules::saveSetting(files.path(), {}, changed, &error), "write/read failure is reported instead of claiming success");
    write(settingsPath, "not JSON");
    check(!Rules::saveSetting(settingsPath, {}, changed, &error) && read(settingsPath) == "not JSON", "damaged unrelated settings are not replaced");

    GameProgramsDialog unchanged({}, true);
    int commits = 0;
    QString inspected;
    unchanged.setInspector([&](const QString &name) { inspected=name; });
    unchanged.setCommitter([&](const QString &, QString *) { ++commits; return true; });
    unchanged.show(); app.processEvents();
    auto *table = unchanged.findChild<QTableWidget *>("gamePrograms");
    for (int row=0;row<table->rowCount();++row)
        if (table->item(row,0)->data(Qt::UserRole).toStringList().contains("dota2.exe")) table->selectRow(row);
    auto *inspect = unchanged.findChild<QPushButton *>("gameInspect");
    inspect->click();
    check(inspected=="dota2.exe" && commits==0 && !unchanged.changed() && unchanged.isVisible(),
          "selected Dota inspection starts without committing routes, closing the editor or reconnecting");
    auto *search = unchanged.findChild<QLineEdit *>("gameSearch"); search->setText("not-in-catalog");
    check(!inspect->isEnabled(), "filtered-out selection cannot silently inspect an invisible program");
    search->clear();
    check(inspect->isEnabled(), "clearing search restores observation for the selected game");
    auto *done = unchanged.findChild<QPushButton *>("gameSave");
    check(done && done->isEnabled() && done->text() == QString::fromUtf8("Готово"), "no-change dialog has a working Done button");
    check(unchanged.rect().contains(QRect(done->mapTo(&unchanged, QPoint{}), done->size())), "footer action stays inside the dialog");
    done->click();
    check(unchanged.result() == QDialog::Accepted && commits == 0, "Done closes without saving or applying a connection");

    GameProgramsDialog edited({}, true);
    edited.setCommitter([&](const QString &text, QString *failure) { ++commits; *failure = "fixture denied write"; return false; });
    check(edited.addExecutable("C:/Games/My Game.exe"), "file selection adds a game without executing it");
    check(edited.changed(), "adding a custom game creates a draft only");
    choice(edited, "Discord.exe")->setCurrentIndex(choice(edited, "Discord.exe")->findData("proxy"));
    choice(edited, "dota2.exe")->setCurrentIndex(choice(edited, "dota2.exe")->findData("bypass"));
    edited.setInspector([&](const QString &name) { inspected=name; });
    auto *editedTable=edited.findChild<QTableWidget *>("gamePrograms");
    for(int row=0;row<editedTable->rowCount();++row)
        if(editedTable->item(row,0)->data(Qt::UserRole).toStringList().contains("dota2.exe")) editedTable->selectRow(row);
    const auto draftBefore=edited.serializedRules(); const auto commitsBefore=commits;
    edited.findChild<QPushButton *>("gameInspect")->click();
    check(inspected=="dota2.exe" && commits==commitsBefore && edited.changed() && edited.serializedRules()==draftBefore,
          "inspection preserves an unsaved custom-game draft and never invokes its committer");
    edited.show(); app.processEvents(); edited.findChild<QPushButton *>("gameSave")->click();
    check(edited.isVisible() && edited.findChild<QLabel *>("gameStatus")->text() == "fixture denied write", "failed commit stays open with the real error");
    QJsonObject accepted;
    edited.setCommitter([&](const QString &text, QString *) { return Rules::parse(text, &accepted); });
    edited.findChild<QPushButton *>("gameSave")->click();
    check(edited.result() == QDialog::Accepted && Rules::mode(accepted, "My Game.exe") == "bypass" && Rules::mode(accepted, "Discord.exe") == "proxy", "Save commits the selected game and Discord directions together");
    const auto runtime = Rules::rules(accepted);
    check(runtime.size() == 2 && GreenRhythm::GameVoiceRouting::tunSniffRules(runtime, true, accepted.keys(), false).size() == 2,
          "custom game choices compile into real program routes with DNS-only UDP optimization");

    GameProgramsDialog cancelled(changed, true); const auto originalText = cancelled.serializedRules();
    choice(cancelled, "dota2.exe")->setCurrentIndex(choice(cancelled, "dota2.exe")->findData("proxy"));
    cancelled.setCommitter([&](const QString &, QString *) { ++commits; return true; }); const auto previousCommits = commits;
    cancelled.reject();
    check(cancelled.result() == QDialog::Rejected && commits == previousCommits && originalText == changed, "Cancel discards edits without applying or saving anything");

    GameProgramsDialog broken("bad JSON", true);
    check(!broken.changed(), "malformed saved policy is not silently cleared on open");
    broken.findChild<QPushButton *>("gameRepair")->click();
    check(broken.changed() && Rules::parse(broken.serializedRules(), &programs) && programs.isEmpty(), "explicit repair resets only the dedicated game preference");

    GameProgramsDialog picker({}, false);
    picker.setRunningProvider([] { return QStringList{"dota2", "DOTA2.EXE", "owned-running-game.exe", "bad/path.exe"}; });
    QTimer::singleShot(0, [&] {
        auto *window = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        auto *list = window ? window->findChild<QListWidget *>("gameRunningList") : nullptr;
        check(list && list->count() == 2, "running picker deduplicates names and ignores invalid entries");
        if (!list) return;
        auto *filter = window->findChild<QLineEdit *>(); filter->setText("owned-running");
        const auto hits = list->findItems("owned-running-game.exe", Qt::MatchExactly);
        if (hits.isEmpty()) { window->reject(); return; }
        list->setCurrentItem(hits.first());
        window->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    picker.findChild<QPushButton *>("gameRunning")->click();
    check(choice(picker, "owned-running-game.exe") && Rules::parse(picker.serializedRules(), &programs)
          && Rules::mode(programs, "owned-running-game.exe") == "bypass", "running-game button adds an actual selected executable directly");
    check(read(routesPath) == routeBytes, "all dialog interaction leaves the route file byte-identical");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
