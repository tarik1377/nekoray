#include "dpi/DpiBundle.hpp"

#include "main/HTTPRequestHelper.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

namespace GreenRhythm::Dpi {

    namespace {
        constexpr auto kVersion = "zapret v72.13";
        constexpr auto kUrl = "https://github.com/bol-van/zapret/releases/download/v72.13/zapret-v72.13.zip";
        constexpr qint64 kZipSize = 7959200;
        constexpr qint64 kZipCeiling = 16 * 1024 * 1024;

        QString sha256Of(const QString &path) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return {};
            QCryptographicHash h(QCryptographicHash::Sha256);
            if (!h.addData(&f)) return {};
            return QString::fromLatin1(h.result().toHex());
        }
    } // namespace

    const QList<PinnedFile> &pinnedFiles() {
        // Четыре бинаря — суммы из sha256sum.txt релиза v72.13 (строки
        // binaries/windows-x86_64/). Заготовки и лицензия — суммы, снятые с того
        // же архива после сверки бинарей. Менять только парой «версия + суммы» и
        // только после проверки на живой машине.
        static const QList<PinnedFile> files{
            {"winws.exe", "zapret-v72.13/binaries/windows-x86_64/winws.exe",
             "a14bff1df6234ea555d2e0c61b589f0707c0b12d6c9b7eeccda76012154996e8", 223232},
            {"cygwin1.dll", "zapret-v72.13/binaries/windows-x86_64/cygwin1.dll",
             "103104a52e5293ce418944725df19e2bf81ad9269b9a120d71d39028e821499b", 2954293},
            {"WinDivert.dll", "zapret-v72.13/binaries/windows-x86_64/WinDivert.dll",
             "c1e060ee19444a259b2162f8af0f3fe8c4428a1c6f694dce20de194ac8d7d9a2", 47616},
            {"WinDivert64.sys", "zapret-v72.13/binaries/windows-x86_64/WinDivert64.sys",
             "8da085332782708d8767bcace5327a6ec7283c17cfb85e40b03cd2323a90ddc2", 94144},
            {"tls_clienthello_www_google_com.bin", "zapret-v72.13/files/fake/tls_clienthello_www_google_com.bin",
             "936c2bee4cfb80aa3c426b2dcbcc834b3fbcd1adb17172959dc569c73a14275c", 681},
            {"quic_initial_www_google_com.bin", "zapret-v72.13/files/fake/quic_initial_www_google_com.bin",
             "f4589c57749f956bb30538197a521d7005f8b0a8723b4707e72405e51ddac50a", 1200},
            {"LICENSE-zapret.txt", "zapret-v72.13/docs/LICENSE.txt",
             "dcf5abd3e5d876c1065982871c0cec7368c0e61fc795c541798729516bb6b54f", 1069},
        };
        return files;
    }

    QString bundleVersion() { return QString::fromLatin1(kVersion); }
    QString bundleUrl() { return QString::fromLatin1(kUrl); }

    QString bundleDir() { return QCoreApplication::applicationDirPath() + QStringLiteral("/dpi"); }

    QStringList bundleProblems(const QString &dir) {
        QStringList problems;
        for (const auto &p: pinnedFiles()) {
            const QFileInfo fi(QDir(dir).filePath(QString::fromLatin1(p.name)));
            if (!fi.exists()) {
                problems << QStringLiteral("нет файла %1").arg(QString::fromLatin1(p.name));
            } else if (fi.size() != p.size) {
                problems << QStringLiteral("%1: размер %2, ожидался %3")
                                .arg(QString::fromLatin1(p.name))
                                .arg(fi.size())
                                .arg(p.size);
            }
        }
        return problems;
    }

    InstallResult installBundle(const QString &dir, const std::function<void(const QString &)> &progress) {
        InstallResult r;
#ifndef Q_OS_WIN
        Q_UNUSED(dir)
        Q_UNUSED(progress)
        r.error = QStringLiteral("модуль обхода есть только под Windows");
        return r;
#else
        auto say = [&](const QString &s) { if (progress) progress(s); };

        QTemporaryDir work;
        if (!work.isValid()) {
            r.error = QStringLiteral("нет доступа к временной папке");
            return r;
        }
        const QString zipPath = work.filePath(QStringLiteral("zapret.zip"));

        say(QStringLiteral("Скачиваем %1 (7,6 МБ)…").arg(bundleVersion()));
        {
            QFile sink(zipPath);
            if (!sink.open(QIODevice::WriteOnly)) {
                r.error = QStringLiteral("не удалось создать временный файл");
                return r;
            }
            const auto resp = NetworkRequestHelper::HttpDownload(QUrl(bundleUrl()), &sink, kZipCeiling);
            sink.close();
            if (!resp.error.isEmpty()) {
                r.error = QStringLiteral("не скачался: %1").arg(resp.error);
                return r;
            }
            if (QFileInfo(zipPath).size() != kZipSize) {
                r.error = QStringLiteral("архив другого размера (%1, ожидалось %2) — отказ")
                              .arg(QFileInfo(zipPath).size())
                              .arg(kZipSize);
                return r;
            }
        }

        // Распаковка системным bsdtar по ПОЛНОМУ пути: в PATH у человека может
        // стоять GNU tar из Git, а он zip не понимает и молча пропускает записи.
        say(QStringLiteral("Распаковываем…"));
        const QString tar = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
                                .filePath(QStringLiteral("System32/tar.exe"));
        QStringList args{QStringLiteral("-xf"), QDir::toNativeSeparators(zipPath),
                         QStringLiteral("-C"), QDir::toNativeSeparators(work.path())};
        for (const auto &p: pinnedFiles()) args << QString::fromLatin1(p.zipEntry);
        QProcess untar;
        untar.start(tar, args);
        if (!untar.waitForFinished(120000) || untar.exitCode() != 0) {
            r.error = QStringLiteral("распаковка не удалась: %1")
                          .arg(QString::fromLocal8Bit(untar.readAllStandardError()).trimmed().left(200));
            return r;
        }

        // Сверка сумм — здесь и только здесь. Дальше файлы считаются нашими.
        say(QStringLiteral("Проверяем суммы…"));
        for (const auto &p: pinnedFiles()) {
            const QString got = sha256Of(work.filePath(QString::fromLatin1(p.zipEntry)));
            if (got != QString::fromLatin1(p.sha256)) {
                r.error = QStringLiteral("%1: сумма не совпала с закреплённой — файл не тот, отказ")
                              .arg(QString::fromLatin1(p.name));
                return r;
            }
        }

        QDir().mkpath(dir);
        for (const auto &p: pinnedFiles()) {
            const QString dst = QDir(dir).filePath(QString::fromLatin1(p.name));
            if (QFile::exists(dst) && !QFile::remove(dst)) {
                // Загруженный драйвер держит .sys. Честно: обновится после перезагрузки.
                r.error = QStringLiteral("%1 занят системой — выключите модуль и перезагрузите компьютер")
                              .arg(QString::fromLatin1(p.name));
                return r;
            }
            if (!QFile::copy(work.filePath(QString::fromLatin1(p.zipEntry)), dst)) {
                r.error = QStringLiteral("не удалось записать %1").arg(QString::fromLatin1(p.name));
                return r;
            }
        }
        r.ok = true;
        say(QStringLiteral("Готово: %1").arg(bundleVersion()));
        return r;
#endif
    }

} // namespace GreenRhythm::Dpi
