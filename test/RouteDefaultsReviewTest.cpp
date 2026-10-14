#include "ui/RouteDefaultsReview.hpp"
#include "ui_dialog_manage_routes.h"
#include <QApplication>
#include <QKeyEvent>
#include <QScrollBar>
#include <cstdio>
static int checks = 0, failures = 0;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
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
    GreenRhythm::RouteDefaultsReview unchanged(empty, empty);
    auto *emptyApply = unchanged.findChild<QPushButton*>("routeDefaultsApplyConfirmed");
    auto *emptyExplanation = unchanged.findChild<QLabel*>("routeDefaultsNoChanges");
    int emptyApprovals = 0;
    QObject::connect(&unchanged, &QDialog::accepted, [&] { ++emptyApprovals; });
    unchanged.show(); app.processEvents();
    check(!emptyApply->isEnabled() && emptyExplanation->isVisible()
          && emptyExplanation->text().contains(QString::fromUtf8("изменений нет")), "no changes disables apply and explains why");
    unchanged.accept();
    unchanged.findChild<QRadioButton*>("routeDefaultsReset")->setChecked(true);
    unchanged.findChild<QCheckBox*>("routeDefaultsResetConfirm")->setChecked(true);
    unchanged.accept();
    check(!emptyApply->isEnabled() && emptyApprovals == 0, "even acknowledged reset cannot approve an empty plan");

    GreenRhythm::RouteDefaultsReview optionalReset(empty, reset);
    check(!optionalReset.findChild<QPushButton*>("routeDefaultsApplyConfirmed")->isEnabled(), "unchanged safe plan does not silently select destructive reset");
    optionalReset.findChild<QRadioButton*>("routeDefaultsReset")->setChecked(true);
    check(!optionalReset.findChild<QPushButton*>("routeDefaultsApplyConfirmed")->isEnabled(), "available reset still requires acknowledgement when safe plan has no changes");

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
