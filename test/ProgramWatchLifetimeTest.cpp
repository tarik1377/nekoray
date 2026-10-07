#include "main/ProgramTrouble.hpp"
#include "main/RunningPrograms.hpp"
#include "ui/dialog_whatbroke.h"

#include <QApplication>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <cstdio>

// Список процессов задаёт тест: клиент и ядро здесь не запускаются.
namespace GreenRhythm { QStringList runningPrograms() { return {"fixture.exe"}; } }

static int checks = 0, failures = 0;
static void check(const char *name, bool condition) {
    ++checks;
    if (!condition) ++failures;
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
}

static GreenRhythm::Seen connection(int id) {
    GreenRhythm::Seen seen;
    seen.process = "fixture.exe";
    seen.tag = "proxy";
    seen.network = "tcp";
    seen.dest = QString("fixture-%1.example:443").arg(id);
    seen.start = id + 1;
    return seen;
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    app.setFont(QFont("Segoe UI", 9));
#endif
    GreenRhythm::Watch watch;
    for (int batch = 0; batch < 1000; ++batch) {
        QList<GreenRhythm::Seen> input;
        for (int i = 0; i < 128; ++i) input += connection(batch * 128 + i);
        watch.add(input);
    }
    check("bounded history under 128000 unique connections", watch.total() <= 4096);
    check("incomplete history is explicitly marked", watch.truncated());
    watch.clear();
    check("clear releases the retained history", watch.total() == 0);
    check("clear resets the incomplete-history marker", !watch.truncated());
    watch.add({connection(0), connection(0)});
    check("deduplication resumes after clear", watch.total() == 1);

    DialogWhatBroke dialog;
    dialog.inspect("fixture.exe");
    check("inspect starts observation", dialog.watching() == "fixture.exe");
    dialog.feed({connection(0)});
    dialog.conclude();
    check("conclusion stops collection", dialog.watching().isEmpty());
    QStringList before;
    for (auto *label: dialog.findChildren<QLabel *>()) before += label->text();
    for (int i = 1; i <= 1000; ++i) dialog.feed({connection(i)});
    QStringList after;
    for (auto *label: dialog.findChildren<QLabel *>()) after += label->text();
    check("completed result ignores subsequent polls", before == after);
    QString requestedProgram;
    QObject::connect(&dialog, &DialogWhatBroke::fixRequested, &dialog,
                     [&](const QString &name) { requestedProgram = name; });
    for (auto *button: dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Пустить эту программу напрямую")) {
            button->click();
            break;
        }
    }
    check("completed result retains the program for a routing request", requestedProgram == "fixture.exe");
    dialog.inspect("fixture.exe");
    check("a new observation starts normally", !dialog.watching().isEmpty());
    dialog.reject();
    check("Escape/reject stops collection", dialog.watching().isEmpty());
    dialog.inspect("fixture.exe");
    dialog.accept();
    check("accept also releases observation", dialog.watching().isEmpty());

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
