#pragma once

#include <QString>
#include <QStringList>
#include <functional>

/**
 * Поставка модуля обхода: что кладём в <appdir>/dpi и как проверяем.
 *
 * ПО ТРЕБОВАНИЮ, НЕ В ОБЩИЙ ПАКЕТ. Антивирусы кладут WinDivert в карантин как
 * RiskTool.Multi.WinDivert — в общем архиве это ударило бы по всем, включая
 * тех, кому модуль не нужен. Загруженный драйвер держит файл, и обновление
 * поверх каталога упёрлось бы в него. И 3,3 МБ не нужны ни маку, ни тем, у
 * кого нет игр.
 *
 * ОДИН ИСТОЧНИК. zapret-v72.13.zip с релиза bol-van несёт winws.exe,
 * cygwin1.dll, WinDivert.dll, WinDivert64.sys, заготовки и лицензию. Суммы
 * четырёх бинарей лежат в sha256sum.txt того же релиза, и ровно они закреплены
 * в DpiBundle.cpp; заготовки и лицензия — суммами, снятыми с того же архива.
 * Бинари не модифицируются: LGPL требует поставлять библиотеку как есть.
 *
 * РАСПАКОВКА — СИСТЕМНЫМ tar.exe (bsdtar, есть в Windows 10 с 1803). Своей
 * zip-библиотеки в клиенте нет, и заводить её ради одного архива незачем.
 *
 * ПРОВЕРКА СУММ — ТОЛЬКО ПРИ УСТАНОВКЕ, НЕ ПРИ КАЖДОМ ЗАПУСКЕ. LGPLv3 §4(d)
 * требует, чтобы работа шла и с изменённой совместимой версией библиотеки;
 * гейтить запуск хешем WinDivert.dll значило бы нарушить это. На старте
 * проверяется только наличие и размер.
 */
namespace GreenRhythm::Dpi {

    struct PinnedFile {
        const char *name;     ///< имя в каталоге модуля
        const char *zipEntry; ///< путь внутри архива
        const char *sha256;   ///< закреплённая сумма
        qint64 size;          ///< закреплённый размер
    };

    /** Закреплённые файлы. Порядок — порядок распаковки. */
    const QList<PinnedFile> &pinnedFiles();

    /** Версия и адрес источника — для панели и уведомлений. */
    QString bundleVersion();
    QString bundleUrl();

    /** <appdir>/dpi — тот же путь, который исключает NetworkRepair. */
    QString bundleDir();

    /** Что не так с каталогом: пусто — всё на месте. Проверяет наличие и размер. */
    QStringList bundleProblems(const QString &dir);

    struct InstallResult {
        bool ok = false;
        QString error;
    };

    /**
     * Скачать архив, проверить, распаковать нужное в dir.
     *
     * Блокирующая: звать из рабочего потока. progress получает короткие фразы
     * для панели. Мимо прокси: модуль качают, когда канала может и не быть.
     */
    InstallResult installBundle(const QString &dir, const std::function<void(const QString &)> &progress);

} // namespace GreenRhythm::Dpi
