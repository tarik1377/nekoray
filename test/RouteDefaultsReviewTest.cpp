#include "ui/RouteDefaultsReview.hpp"
#include "ui_dialog_manage_routes.h"
#include <QApplication>
#include <cstdio>
static int checks = 0, failures = 0;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
int main(int argc, char **argv) {
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
    reset.mode = GreenRhythm::RouteDefaultsPolicy::Mode::ResetRoutes;
    reset.changedFields = {"custom", "direct_domain"}; reset.removedRules = 5;
    GreenRhythm::RouteDefaultsReview review(safe, reset);
    auto *apply = review.findChild<QPushButton*>("routeDefaultsApplyConfirmed");
    auto *resetButton = review.findChild<QRadioButton*>("routeDefaultsReset");
    auto *confirm = review.findChild<QCheckBox*>("routeDefaultsResetConfirm");
    auto *summary = review.findChild<QLabel*>("routeDefaultsPreview");
    check(review.mode() == GreenRhythm::RouteDefaultsPolicy::Mode::SafeUpgrade && apply->isEnabled(), "review defaults to preserving custom rules");
    check(summary->text().contains("2") && summary->text().contains("3"), "review displays actual changed-field and rule counts");
    resetButton->setChecked(true);
    check(review.mode() == GreenRhythm::RouteDefaultsPolicy::Mode::ResetRoutes && !apply->isEnabled()
          && summary->text().contains(QString::fromUtf8("заменены")), "full reset clearly warns and requires explicit acknowledgement");
    confirm->setChecked(true);
    check(apply->isEnabled(), "reset becomes available only after acknowledgement");
    apply->click();
    check(review.result() == QDialog::Accepted, "explicit apply accepts the selected review mode");
    GreenRhythm::RouteDefaultsReview cancelled(safe, reset);
    cancelled.reject();
    check(cancelled.result() == QDialog::Rejected, "cancel produces no approval");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
