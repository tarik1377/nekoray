#include "ui/UpdateProgress.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir dir;
    const auto archive = dir.path() + "/greenrhythm.zip";
    QProgressDialog dialog;
    dialog.setAutoClose(false);
    dialog.setAutoReset(false);
    const auto write = [&](const QByteArray &data) {
        QFile status(archive + ".progress.json");
        if (!status.open(QIODevice::WriteOnly)) return false;
        return status.write(data) == data.size();
    };
    if (!write(R"({"stage":"download","bytes":5242880,"total":10485760})")) return 1;
    GreenRhythm::Update::refreshProgress(&dialog, archive);
    if (dialog.value() != 50 || dialog.maximum() != 100 || !dialog.labelText().contains("1/4")) return 2;
    for (const auto &stage : {"verify", "prepare", "ready"}) {
        if (!write(QByteArray("{\"stage\":\"") + stage + "\"}")) return 3;
        GreenRhythm::Update::refreshProgress(&dialog, archive);
        if (dialog.maximum() != 0 || !dialog.labelText().contains("2/4")) return 4;
    }
    const auto label = dialog.labelText();
    if (!write("{partial")) return 5;
    GreenRhythm::Update::refreshProgress(&dialog, archive);
    if (dialog.labelText() != label) return 6;
    std::cout << "Download percentage, verification, preparation and partial status: passed\n";
    return 0;
}
