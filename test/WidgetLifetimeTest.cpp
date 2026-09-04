/**
 * ПЕРЕЖИВАЮТ ЛИ ВИДЖЕТЫ ПОДМЕНУ ЦЕНТРАЛЬНОГО.
 *
 * ЖИВОЙ СЛУЧАЙ. Сборка macOS падала на запуске: SIGSEGV, а в отчёте о падении
 *
 *     #0  QtWidgets    QLabel::setText(QString const&)
 *     #2  greenrhythm  MainWindow::refresh_status(QString const&)
 *     EXC_BAD_ACCESS, KERN_INVALID_ADDRESS at 0x389
 *
 * Адрес 0x389 не выровнен по восьми — это подпись мусорного указателя, а не
 * нулевого, то есть проверкой на nullptr такое не ловится в принципе.
 *
 * ОТКУДА МУСОР. Оболочка ставится через setCentralWidget(shell), а прежний
 * центральный виджет Qt при этом удаляет. Вместе с ним умирает всё, что лежало
 * в нём и не было перенесено: строка состояния, поле поиска, разделитель, обе
 * галки, ряд кнопок. adopt() забирает только таблицу серверов и вкладки
 * журнала. Дальше refresh_status пишет в мёртвые метки по таймеру, on_commitDataRequest
 * читает мёртвый разделитель при КАЖДОМ штатном выходе, а обработчик Esc трогает
 * мёртвое поле поиска на любое нажатие.
 *
 * ПОЧЕМУ ПАДАЛО ТОЛЬКО НА МАКЕ. Обращение к освобождённой памяти — не отказ, а
 * лотерея: под Windows блок остаётся читаемым, и никто ничего не замечает.
 * Поэтому проверять надо не падение, а САМ ФАКТ УДАЛЕНИЯ, и он одинаков всюду.
 *
 * Набор повторяет боевую последовательность из ui/mainwindow.cpp: setupUi,
 * MainShell, adopt, setCentralWidget — и смотрит на QPointer. Обнулился —
 * объект удалён, и код, который в него пишет, работает с мусором.
 *
 * Запуск: ninja widget_lifetime_test && ./widget_lifetime_test
 */

#include "ui_mainwindow.h"

#include "ui/MainShell.hpp"

#include <QApplication>
#include <QFile>
#include <QMainWindow>
#include <QPointer>
#include <QWidget>

#include <cstdio>

static int checks = 0;
static int fails = 0;

static void is(const QString &what, bool ok) {
    checks++;
    if (!ok) fails++;
    std::fputs((QString(ok ? QStringLiteral("  ok    ") : QStringLiteral("  ПРОВАЛ "))
                + what + QStringLiteral("\n"))
                   .toUtf8()
                   .constData(),
               stdout);
}

static QString slurp(const QString &path) {
    for (const QString &prefix: {QStringLiteral(""), QStringLiteral("../"), QStringLiteral("../../")}) {
        QFile f(prefix + path);
        if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll());
    }
    return {};
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    // ---- ЧАСТЬ 1: КАК ВЕДЁТ СЕБЯ QT БЕЗ ЗАЩИТЫ ----
    //
    // Документируем поведение, ради которого правка и написана. Если Qt однажды
    // перестанет удалять прежнего центрального, эта часть покраснеет — и мы
    // узнаем об этом здесь, а не гадая, зачем в коде лишние строки.
    {
        QMainWindow w;
        Ui::MainWindow ui;
        ui.setupUi(&w);
        QPointer<QObject> speed = ui.label_speed;
        QPointer<QObject> splitter = ui.splitter;
        auto *shell = new GreenRhythm::MainShell(&w);
        shell->adopt(ui.tabWidget, ui.down_tab);
        QPointer<QObject> tabs = ui.tabWidget;
        w.setCentralWidget(shell);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        is(QStringLiteral("без защиты: строка состояния умирает"), speed.isNull());
        is(QStringLiteral("без защиты: разделитель умирает"), splitter.isNull());
        is(QStringLiteral("перенесённое adopt() выживает в любом случае"), !tabs.isNull());
    }

    // ---- ЧАСТЬ 2: КАК ВЕДЁТ СЕБЯ ЗАЩИЩЁННАЯ ПОСЛЕДОВАТЕЛЬНОСТЬ ----
    //
    // Ровно то, что делает ui/mainwindow.cpp: забрать владение, поставить
    // оболочку, отдать прежнего ей в родители и спрятать.
    {
        QMainWindow w;
        Ui::MainWindow ui;
        ui.setupUi(&w);

        struct Watched {
            const char *name;
            QPointer<QObject> ptr;
        };
        Watched watched[] = {
            {"label_speed", ui.label_speed},          // refresh_status
            {"label_conn_pill", ui.label_conn_pill},  // refresh_status
            {"label_running", ui.label_running},      // refresh_status
            {"label_inbound", ui.label_inbound},      // refresh_status
            {"label_sub_status", ui.label_sub_status},// refresh_subscription_status
            {"checkBox_VPN", ui.checkBox_VPN},        // refresh_status
            {"search", ui.search},                    // Ctrl+F и Esc
            {"splitter", ui.splitter},                // on_commitDataRequest, каждый выход
        };

        auto *shell = new GreenRhythm::MainShell(&w);
        shell->adopt(ui.tabWidget, ui.down_tab);

        QWidget *legacy = w.takeCentralWidget();
        w.setCentralWidget(shell);
        if (legacy != nullptr) {
            legacy->setParent(shell);
            legacy->hide();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        is(QStringLiteral("с защитой: прежний центральный забран, а не удалён"),
           legacy != nullptr);
        for (const auto &item : watched) {
            is(QStringLiteral("с защитой жив: ") + QString::fromUtf8(item.name),
               !item.ptr.isNull());
        }
    }

    // ---- ЧАСТЬ 3: СТОРОЖ САМОГО КОДА ----
    //
    // Части выше проверяют Qt и образец. Продукт они не сторожат: убери правку
    // из mainwindow.cpp — и они останутся зелёными. Поэтому читаем исходник и
    // проверяем ОТНОШЕНИЕ, как это сделано в test/RouteOrderTest.cpp: номера
    // строк — третья копия того же знания, и разъезжаются они молча.
    const QString src = slurp(QStringLiteral("ui/mainwindow.cpp"));
    is(QStringLiteral("исходник окна прочитан"), !src.isEmpty());
    if (!src.isEmpty()) {
        const int take = src.indexOf(QStringLiteral("takeCentralWidget()"));
        const int set = src.indexOf(QStringLiteral("setCentralWidget(shell)"));
        is(QStringLiteral("прежний центральный забирается"), take >= 0);
        is(QStringLiteral("оболочка ставится центральной"), set >= 0);
        // ГЛАВНОЕ ОТНОШЕНИЕ: забрать надо ДО подмены, иначе забирать уже нечего.
        is(QStringLiteral("забирается ДО подмены"), take >= 0 && set > take);
        // И забранному обязан найтись родитель, иначе он просто течёт.
        is(QStringLiteral("забранному назначается родитель"),
           take >= 0 && src.mid(take, 400).contains(QStringLiteral("setParent(shell)")));
    }

    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
