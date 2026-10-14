#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressDialog>

namespace GreenRhythm::Update {
inline void refreshProgress(QProgressDialog *dialog, const QString &archive) {
    QFile file(archive + ".progress.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto status = QJsonDocument::fromJson(file.readAll()).object();
    const auto stage = status.value("stage").toString();
    const auto bytes = status.value("bytes").toDouble();
    const auto total = status.value("total").toDouble();
    if (stage == "download") {
        dialog->setLabelText(QObject::tr("1/4 · Скачивание обновления: %1 из %2 МБ\nПрограмма продолжает работать.")
            .arg(bytes / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1));
        dialog->setRange(0, total > 0 ? 100 : 0);
        if (total > 0) dialog->setValue(qBound(0, int(bytes * 100 / total), 100));
    } else if (stage == "verify" || stage == "prepare" || stage == "ready") {
        dialog->setRange(0, 0);
        dialog->setLabelText(QObject::tr("2/4 · Проверка пакета и подготовка установки…\nПрограмма продолжает работать."));
    }
}
}
