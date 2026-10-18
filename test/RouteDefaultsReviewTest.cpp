#include "ui/RouteDefaultsReview.hpp"
#include "ui_dialog_manage_routes.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWindow>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QScrollBar>
#include <cstdio>
static int checks = 0, failures = 0;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
static void click(QWidget *widget, const QPoint &position) {
    auto *window = widget->window();
    const QPointF local(widget->mapTo(window, position));
    const QPointF global(widget->mapToGlobal(position));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window->windowHandle(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window->windowHandle(), &release);
    QCoreApplication::processEvents();
}
static QByteArray read(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QDialog shell;
    Ui::DialogManageRoutes ui;
    ui.setupUi(&shell);
    check(ui.defaultsApply->text() == QString::fromUtf8("Применить стандартные маршруты")
          && ui.defaultsKeep->text() == QString::fromUtf8("Оставить мои"), "two explicit route choices in real generated dialog");
    check(!ui.defaultsRestore->openExternalLinks() && ui.defaultsRestore->text().contains("restore"), "restore link is local action");
    shell.resize(800, 650); shell.show(); app.processEvents();
    check(ui.defaultsApply->isVisible() && ui.defaultsKeep->isVisible()
          && ui.defaultsApply->geometry().width() >= ui.defaultsApply->sizeHint().width(), "compact route choices fit original dialog width");
    GreenRhythm::RouteDefaultsPolicy::Preview safe, reset;
    safe.changedFields = {"direct_dns", "dns_final_out"}; safe.addedRules = 3;
    safe.after = {{"direct_dns", "fixture-dns"}, {"untouched", QJsonObject{{"nested", true}}}};
    reset.mode = GreenRhythm::RouteDefaultsPolicy::Mode::ResetRoutes;
    reset.changedFields = {"custom", "direct_domain"}; reset.removedRules = 5;
    GreenRhythm::RouteDefaultsReview review(safe, reset);
    auto *apply = review.findChild<QPushButton*>("routeDefaultsApplyConfirmed");
    auto *resetButton = review.findChild<QRadioButton*>("routeDefaultsReset");
    auto *confirm = review.findChild<QCheckBox*>("routeDefaultsResetConfirm");
    auto *summary = review.findChild<QLabel*>("routeDefaultsPreview");
    auto *safeButton = review.findChild<QRadioButton*>("routeDefaultsSafe");
    auto *fields = review.findChild<QLabel*>("routeDefaultsChangedFields");
    auto *leave = review.findChild<QPushButton*>("routeDefaultsLeaveUnchanged");
    int approvals = 0;
    QObject::connect(&review, &QDialog::accepted, [&] { ++approvals; });
    review.show(); app.processEvents();
    check(approvals == 0 && safe.after.value("untouched").toObject().value("nested").toBool(), "opening review neither approves a commit nor changes supplied configuration");
    check(review.mode() == GreenRhythm::RouteDefaultsPolicy::Mode::SafeUpgrade && apply->isEnabled(), "review defaults to preserving custom rules");
    check(summary->text().contains("2") && summary->text().contains("3"), "review displays actual changed-field and rule counts");
    check(fields && fields->text().contains(QString::fromUtf8("DNS для сайтов напрямую"))
          && fields->text().contains(QString::fromUtf8("остальные DNS-запросы"))
          && !fields->text().contains("direct_dns") && !fields->text().contains("dns_final_out"), "preview names affected DNS settings in human language rather than raw keys");
    check(apply->text() == QString::fromUtf8("Применить изменения") && leave
          && leave->text() == QString::fromUtf8("Оставить как есть") && leave->isDefault()
          && !apply->autoDefault(), "explicit choices default to leaving current routes unchanged");
    resetButton->setChecked(true);
    check(review.mode() == GreenRhythm::RouteDefaultsPolicy::Mode::ResetRoutes && !apply->isEnabled()
          && summary->text().contains(QString::fromUtf8("заменены")), "full reset clearly warns and requires explicit acknowledgement");
    review.accept();
    check(approvals == 0, "programmatic accept cannot bypass full reset acknowledgement");
    confirm->setChecked(true);
    check(apply->isEnabled(), "reset becomes available only after acknowledgement");
    safeButton->setChecked(true); resetButton->setChecked(true);
    check(!confirm->isChecked() && !apply->isEnabled(), "returning to reset requires a fresh explicit acknowledgement");
    check(approvals == 0, "changing review mode and acknowledgement does not approve any write");
    confirm->setChecked(true);
    apply->click();
    check(review.result() == QDialog::Accepted && approvals == 1, "explicit apply accepts the selected review mode once");
    GreenRhythm::RouteDefaultsReview cancelled(safe, reset);
    int cancelApprovals = 0, cancelResults = 0;
    QObject::connect(&cancelled, &QDialog::accepted, [&] { ++cancelApprovals; });
    QObject::connect(&cancelled, &QDialog::finished, [&](int result) { if (result == QDialog::Rejected) ++cancelResults; });
    cancelled.show(); app.processEvents();
    cancelled.findChild<QPushButton*>("routeDefaultsLeaveUnchanged")->click();
    check(cancelled.result() == QDialog::Rejected && cancelApprovals == 0 && cancelResults == 1,
          "leave unchanged rejects review and cannot trigger the caller's apply branch");

    GreenRhythm::RouteDefaultsReview keyboardCancel(safe, reset);
    int keyboardApprovals = 0;
    QObject::connect(&keyboardCancel, &QDialog::accepted, [&] { ++keyboardApprovals; });
    keyboardCancel.show(); app.processEvents();
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&keyboardCancel, &enter);
    check(keyboardCancel.result() == QDialog::Rejected && keyboardApprovals == 0, "Enter on the default choice leaves routes unchanged");

    GreenRhythm::RouteDefaultsPolicy::Preview empty;
    QTemporaryDir storage;
    QDir().mkpath(storage.filePath("routes_box")); QDir().mkpath(storage.filePath("groups"));
    const auto routePath = storage.filePath("routes_box/Default");
    const auto settingsPath = storage.filePath("groups/greenrhythm.json");
    const QByteArray routeBytes = "{\"direct_domain\":\"domain:ru\"}\r\n";
    const QByteArray settingsBytes = "{\"routing_defaults_choice\":\"upgrade\",\"vpn_impl\":1}\r\n";
    for (const auto &entry : {qMakePair(routePath, routeBytes), qMakePair(settingsPath, settingsBytes)}) {
        QFile file(entry.first);
        check(file.open(QIODevice::WriteOnly) && file.write(entry.second) == entry.second.size(), "private no-change storage fixture written");
    }
    empty.after = QJsonDocument::fromJson(routeBytes).object();
    for (bool resetMode : {false, true}) {
        GreenRhythm::RouteDefaultsReview unchanged(empty, empty);
        int emptyApprovals = 0, closed = 0;
        QObject::connect(&unchanged, &QDialog::accepted, [&] {
            ++emptyApprovals;
            GreenRhythm::RouteDefaultsPolicy::apply(routePath, settingsPath, empty, routeBytes);
        });
        QObject::connect(&unchanged, &QDialog::rejected, [&] { ++closed; });
        unchanged.show(); app.processEvents();
        if (resetMode) {
            auto *resetChoice = unchanged.findChild<QRadioButton*>("routeDefaultsReset");
            click(resetChoice, resetChoice->rect().center());
        }
        auto *done = unchanged.findChild<QPushButton*>("routeDefaultsApplyConfirmed");
        auto *explanation = unchanged.findChild<QLabel*>("routeDefaultsNoChanges");
        check(done->isEnabled() && done->isDefault() && done->text() == QString::fromUtf8("Готово"),
              "unchanged safe/reset choice offers a working Done action");
        check(explanation->isVisible()
              && !unchanged.findChild<QCheckBox*>("routeDefaultsResetConfirm")->isVisible()
              && !unchanged.findChild<QLabel*>("routeDefaultsPreview")->isVisible()
              && !unchanged.findChild<QPushButton*>("routeDefaultsLeaveUnchanged")->isVisible(),
              "unchanged choice has no disabled consent, destructive summary or duplicate close button");
        if (resetMode) check(explanation->text().contains(QString::fromUtf8("стандартные маршруты")),
              "unchanged reset explicitly explains that the standard routes are already installed");
        click(done, done->rect().center());
        check(!unchanged.isVisible() && unchanged.result() == QDialog::Rejected && emptyApprovals == 0 && closed == 1,
              "real mouse click on Done closes without entering the caller's apply branch");
        check(read(routePath) == routeBytes && read(settingsPath) == settingsBytes
              && !GreenRhythm::RouteDefaultsPolicy::canRestore(routePath, QJsonDocument::fromJson(read(settingsPath)).object()),
              "Done preserves exact route/settings bytes and creates no restore backup");
    }

    GreenRhythm::RouteDefaultsReview optionalReset(empty, reset);
    optionalReset.show(); app.processEvents();
    auto *optionalApply = optionalReset.findChild<QPushButton*>("routeDefaultsApplyConfirmed");
    check(optionalApply->isEnabled() && optionalApply->text() == QString::fromUtf8("Готово"),
          "unchanged safe plan closes normally without selecting destructive reset");
    auto *optionalChoice = optionalReset.findChild<QRadioButton*>("routeDefaultsReset");
    click(optionalChoice, optionalChoice->rect().center());
    auto *optionalConfirm = optionalReset.findChild<QCheckBox*>("routeDefaultsResetConfirm");
    check(optionalConfirm->isVisible() && optionalConfirm->isEnabled() && !optionalConfirm->isChecked()
          && !optionalApply->isEnabled() && optionalApply->text() == QString::fromUtf8("Применить изменения")
          && optionalReset.findChild<QPushButton*>("routeDefaultsLeaveUnchanged")->isDefault(),
          "switching from no-change to real reset restores the full explicit consent guard");
    optionalReset.accept();
    check(optionalReset.isVisible(), "programmatic acceptance still cannot bypass consent after mode switch");
    click(optionalConfirm, optionalConfirm->rect().center());
    check(optionalConfirm->isChecked() && optionalApply->isEnabled(), "real mouse click on confirmation text enables a real reset");
    click(optionalApply, optionalApply->rect().center());
    check(optionalReset.result() == QDialog::Accepted, "confirmed real reset remains usable through mouse input");

    GreenRhythm::RouteDefaultsPolicy::Preview large = safe;
    for (int i = 0; i < 90; ++i) large.changedFields.append(QStringLiteral("future-private-field-%1").arg(i));
    GreenRhythm::RouteDefaultsReview many(large, reset);
    many.show(); app.processEvents();
    auto *scroll = many.findChild<QScrollArea*>("routeDefaultsChangesScroll");
    auto *manyFields = many.findChild<QLabel*>("routeDefaultsChangedFields");
    check(scroll && scroll->height() <= 220 && scroll->verticalScrollBar()->maximum() > 0,
          "many changes stay in a bounded vertically scrollable preview");
    check(manyFields->textFormat() == Qt::PlainText && !manyFields->text().contains("future-private-field")
          && manyFields->text().contains(QString::fromUtf8("Дополнительный параметр схемы")), "unknown settings remain visible without exposing internal keys or rendering markup");
    check(safe.changedFields == QStringList{"direct_dns", "dns_final_out"}
          && safe.after.value("direct_dns").toString() == "fixture-dns", "review interactions preserve the input plan");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
