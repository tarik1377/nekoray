#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/**
 * Каталог стратегий обхода для модуля winws.
 *
 * ЗАЧЕМ СВОЙ КАТАЛОГ, А НЕ .bat ИЗ СБОРКИ FLOWSEAL. Их файлы — код, не данные:
 * general.bat зовёт service.bat, тот раскрывает переменные и по дороге включает
 * системные метки времени TCP. Возить чужой код и исполнять его у клиента мы не
 * будем. Здесь лежат только аргументы --dpi-desync-*, переписанные из тех .bat
 * дословно, с указанием откуда. Всё, что про порты, списки и профили, строит
 * DpiPlan — стратегия про это не знает.
 *
 * ЗАГОТОВКИ ТОЛЬКО ИЗ ЗАКРЕПЛЁННОГО РЕЛИЗА bol-van. У Flowseal свои .bin
 * (tls_clienthello_max_ru, stun2), они не запинены, и стратегии на них сюда не
 * входят. Файл tls_clienthello_www_google_com.bin — 681 байт — лежит в
 * zapret-v72.13.zip, и ровно его использует профиль Google в general.bat.
 *
 * НИ ОДНОЙ СТРАТЕГИИ С МЕТКАМИ ВРЕМЕНИ. --dpi-desync-fooling=ts требует
 * netsh interface tcp set global timestamps=enabled — системной настройки,
 * которую Flowseal включает молча. У Windows по умолчанию «allowed», исходящие
 * идут без меток, и такая стратегия не бесполезна, а вредна: подделка дойдёт до
 * сервера. Пока модуль не умеет спрашивать и откатывать, их здесь нет.
 */
namespace GreenRhythm::Dpi {

    struct Strategy {
        QString id;
        QString title;   ///< коротко, для списка
        QString human;   ///< что делает, словами для человека
        QString source;  ///< откуда взяты аргументы

        /** Аргументы для TCP-профиля (порты 80/443/8443). {BIN} — каталог заготовок. */
        QStringList tcpDesync;
        /** Аргументы для QUIC-профиля (udp 443). Пусто — профиль не создаётся. */
        QStringList udpDesync;
        /** Файлы заготовок, которые обязаны лежать в каталоге модуля. */
        QStringList fakes;
        /** Проверена ли связка сообществом или это наша собственная сборка. */
        bool ours = false;
    };

    /** Все стратегии в порядке перебора: от самой мягкой к самой шумной. */
    const QList<Strategy> &catalog();

    /** Стратегия по идентификатору; nullptr — нет такой. */
    const Strategy *findStrategy(const QString &id);

    /** Подставить каталог заготовок вместо {BIN}. Слэши — прямые: winws под cygwin. */
    QStringList expandArgs(const QStringList &args, const QString &binDir);

} // namespace GreenRhythm::Dpi
