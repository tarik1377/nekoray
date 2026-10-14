#pragma once

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryFile>
#include <QTimer>
#include <functional>

namespace GreenRhythm::Rollback {
struct Status {
    bool available = false;
    QString version;
    QString provenance;
    QString error;
};
inline Status parseStatus(const QByteArray &data) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {false, {}, {}, QObject::tr("Не удалось проверить предыдущую версию.")};
    const auto object = document.object();
    const auto version = object.value("version").toString();
    const auto provenance = object.value("provenance").toString();
    const bool supported = provenance == "signed-release" || provenance == "explicit-verified-local";
    const bool available = object.value("available").toBool() && !version.isEmpty() &&
                           object.value("platform").toString() == "windows-x64" && supported && object.value("files").toInt() >= 10;
    return {available, version, provenance, object.value("error").toString()};
}
inline void showStatus(QPushButton *button, QLabel *label, const Status &status) {
    button->setEnabled(status.available);
    button->setProperty("rollbackVersion", status.version);
    button->setText(status.available ? QObject::tr("Вернуть %1…").arg(status.version) : QObject::tr("Вернуть предыдущую…"));
    label->setText(status.available ? QObject::tr("Можно вернуть %1 · %2").arg(status.version,
        status.provenance == "signed-release" ? QObject::tr("подписанный выпуск") : QObject::tr("проверенная локальная сборка")) :
        (status.error.isEmpty() ? QObject::tr("Проверенная предыдущая версия пока не сохранена.") : status.error));
}
inline void refreshStatus(QWidget *parent, QPushButton *button, QLabel *label, const QString &directory) {
    button->setEnabled(false);
#ifndef Q_OS_WIN
    label->setText(QObject::tr("Откат версии пока поддерживается на Windows."));
    button->hide();
#else
    label->setText(QObject::tr("Проверяем сохранённую версию…"));
    auto *process = new QProcess(parent);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    QObject::connect(timeout, &QTimer::timeout, process, [=] {
        process->kill();
        showStatus(button, label, {false, {}, {}, QObject::tr("Проверка предыдущей версии не завершилась. Откройте настройки ещё раз.")});
    });
    QObject::connect(process, &QProcess::errorOccurred, parent, [=](QProcess::ProcessError) {
        timeout->stop();
        showStatus(button, label, {false, {}, {}, QObject::tr("Не удалось запустить проверку предыдущей версии.")});
        process->deleteLater();
    });
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), parent,
                     [=](int code, QProcess::ExitStatus exit) {
        timeout->stop();
        const auto status = code == 0 && exit == QProcess::NormalExit ? parseStatus(process->readAllStandardOutput()) :
            Status{false, {}, {}, QObject::tr("Не удалось проверить предыдущую версию.")};
        showStatus(button, label, status);
        process->deleteLater();
    });
    process->setWorkingDirectory(directory);
    process->start(directory + "/updater.exe", {"--rollback-info"});
    timeout->start(10000);
#endif
}
using Launch = std::function<bool(const QString &, const QStringList &, const QString &)>;
// Prepare and verify a distinct executable before closing a working client.
// The helper runs outside the installation, so its own previous version can be
// restored along with the GUI and core.
inline bool launch(const QString &directory, const std::function<void()> &close,
                   Launch start = [](const QString &file, const QStringList &args, const QString &cwd) {
                       return QProcess::startDetached(file, args, cwd);
                   }) {
#ifndef Q_OS_WIN
    Q_UNUSED(directory); Q_UNUSED(close); Q_UNUSED(start);
    return false;
#else
    QFile source(directory + "/updater.exe");
    if (!source.open(QIODevice::ReadOnly)) return false;
    const auto bytes = source.readAll();
    if (bytes.isEmpty() || source.error() != QFileDevice::NoError) return false;
    QTemporaryFile helper(QDir::tempPath() + "/greenrhythm-updater-XXXXXX.exe");
    if (!helper.open() || helper.write(bytes) != bytes.size() || !helper.flush()) return false;
    const auto name = helper.fileName();
    helper.close();
    QFile verify(name);
    if (!verify.open(QIODevice::ReadOnly) || QCryptographicHash::hash(verify.readAll(), QCryptographicHash::Sha256) !=
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)) return false;
    verify.close();
    if (!start(name, {"--install-dir", directory, "--rollback"}, directory)) return false;
    helper.setAutoRemove(false);
    close();
    return true;
#endif
}
}
