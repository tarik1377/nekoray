/**
 * Снимок главного окна — без сети, без ядра, без настроек.
 *
 * ЗАЧЕМ. Главное окно нельзя посмотреть иначе как запустив клиент, а на машине
 * владельца поднят рабочий туннель: второй экземпляр ради проверки отступов —
 * плохой размен. Из-за этого правки внешнего вида шли вслепую, и владелец
 * справедливо сказал, что окно так и осталось прежним.
 *
 * Поднимается только сгенерированная вёрстка (ui_mainwindow.h), заполняется
 * теми же данными, что человек видит на своей машине, и сохраняется png.
 * Логики главного окна здесь нет и быть не должно: она тянет за собой ядро,
 * настройки и сеть.
 *
 * В выпуск не входит: EXCLUDE_FROM_ALL.
 *   ninja mainwindow_preview && ./mainwindow_preview main.png
 */

#include "ui_mainwindow.h"

#include "ui/MainShell.hpp"
#include "ui/ServerCardDelegate.hpp"

#include <QApplication>
#include <QFile>
#include <QHeaderView>
#include <QMainWindow>
#include <QMenuBar>
#include <QPixmap>
#include <QTranslator>
#include <QTableWidgetItem>

namespace {

    /** Строка списка серверов ровно в том виде, в каком её видит человек. */
    void addServer(QTableWidget *t, const QString &type, const QString &address,
                   const QString &name, const QString &latency) {
        const int row = t->rowCount();
        t->insertRow(row);
        t->setItem(row, 0, new QTableWidgetItem(type));
        t->setItem(row, 1, new QTableWidgetItem(address));
        t->setItem(row, 2, new QTableWidgetItem(name));
        t->setItem(row, 3, new QTableWidgetItem(latency));
    }

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("main.png");

    // ПЕРЕВОД ОБЯЗАТЕЛЕН. Без него окно показывает английские заготовки из .ui,
    // и снимок врёт: человек видит русские подписи, а мы правили бы по чужим.
    // Файл берём собранный, рядом с исполняемым — тот же, что попадает в ресурс.
    QTranslator ru;
    if (ru.load(QStringLiteral("ru_RU.qm"), QCoreApplication::applicationDirPath())) {
        QCoreApplication::installTranslator(&ru);
    } else {
        qWarning("перевод не загрузился — подписи будут английскими");
    }

    QMainWindow w;
    Ui::MainWindow ui;
    ui.setupUi(&w);

    // Тема приложения — иначе смотрим на системную серость вместо того, что
    // увидит человек. Файл тот же, что грузит ThemeManager.
    QFile qss(QStringLiteral("../res/theme/feiyangqingyun/qss/modern.css"));
    if (!qss.open(QIODevice::ReadOnly)) {
        qss.setFileName(QStringLiteral("res/theme/feiyangqingyun/qss/modern.css"));
        qss.open(QIODevice::ReadOnly);
    }
    if (qss.isOpen()) w.setStyleSheet(QString::fromUtf8(qss.readAll()));

    // Данные — настоящие, с машины владельца: те же имена и адреса. Выдуманные
    // «Server 1, Server 2» скрыли бы ровно то, на что он жалуется: имя теряется
    // между протоколом и адресом.
    addServer(ui.proxyListTable, "VLESS", "203.0.113.30:443", "Germanyyy-admin", "");
    addServer(ui.proxyListTable, "VLESS", "203.0.113.20:443", "elvin", "");
    addServer(ui.proxyListTable, "VLESS", "203.0.113.10:443", "lockdowner", "");
    addServer(ui.proxyListTable, "VLESS", "31.77.129.38:443", "Germany-admin", "");
    addServer(ui.proxyListTable, "VLESS", "192.124.181.171:443", "tarik", "32 ms");
    addServer(ui.proxyListTable, "VLESS", "orsana.adshkola.ru:443", "orsana-admin", "106 ms");

    ui.label_running->setText(QStringLiteral("[По умолчанию] Germanyyy-admin"));
    ui.label_inbound->setText(QStringLiteral("Mixed: 127.0.0.1:2080"));
    ui.label_speed->setText(QStringLiteral("Через прокси: 38.80 KiB↑ 72.84 MiB↓\n"
                                           "Напрямую: 653.00 B↑ 332.00 B↓"));

    // Новая оболочка: забирает существующие виджеты и раскладывает по страницам.
    // Именно так это происходит и в живом окне — предпросмотр не изображает
    // отдельную вёрстку, иначе он врал бы.
    // То же скрытие колонок, что делает главное окно: снимок обязан повторять
    // его, иначе он показывает вёрстку, которой не существует.
    ui.proxyListTable->setColumnHidden(0, true);
    ui.proxyListTable->setColumnHidden(1, true);
    ui.proxyListTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui.proxyListTable->setColumnHidden(3, true);
    ui.proxyListTable->setColumnHidden(4, true);
    ui.proxyListTable->setItemDelegateForColumn(
        2, new GreenRhythm::ServerCardDelegate(0, 1, 3, &w));
    ui.proxyListTable->verticalHeader()->setVisible(false);
    ui.proxyListTable->horizontalHeader()->setVisible(false);
    ui.proxyListTable->setShowGrid(false);
    ui.proxyListTable->verticalHeader()->setDefaultSectionSize(62);
    ui.proxyListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.proxyListTable->selectRow(0);
    ui.tabWidget->tabBar()->setVisible(ui.tabWidget->count() > 1);

    auto *shell = new GreenRhythm::MainShell(&w);
    shell->adopt(ui.tabWidget, ui.down_tab);
    shell->setConnectionState(true, QStringLiteral("Germanyyy-admin"), QStringLiteral("32 мс"));
    // Подписка — с настоящими числами, а не «Lorem»: по ним видно, влезает ли
    // строка в колонку и не переносится ли она посреди слова.
    // Живые числа с настоящего снимка владельца: 60 через VPN, 123 напрямую.
    // По ним видно и то, влезают ли столбцы, и то, читается ли соотношение.
    shell->setLive(60, 123, QStringLiteral("72,8 МБ"), QStringLiteral("38,8 КБ"));
    shell->setBypassCount(22);
    shell->setServerTags({QStringLiteral("VLESS"), QStringLiteral("TCP"),
                          QStringLiteral("REALITY")});
    // Полосу меню прячем и в снимке: иначе он показывает окно, которого у
    // человека на Windows уже нет.
    w.menuBar()->setVisible(false);
    shell->setSubscription(QStringLiteral("27 дн. · 84,3 ГБ"), false);
    // Режимы и список под кнопкой — тоже настоящие: снимок обязан показывать
    // то окно, которое увидит человек, а не то, что было до этих кнопок.
    shell->setModes(true, false, false, true);
    shell->setServers({{4, QStringLiteral("tarik"), QStringLiteral("32 мс")},
                       {1, QStringLiteral("Germany-admin"), QString()},
                       {5, QStringLiteral("orsana-admin"), QStringLiteral("106 мс")}},
                      4);
    w.setCentralWidget(shell);

    // Страница выбирается доводом: посмотреть надо каждую, а не только первую.
    if (argc > 2) shell->showPage(QString::fromLocal8Bit(argv[2]).toInt());

    w.resize(1180, 720);
    w.show();
    app.processEvents();

    QPixmap shot = w.grab();
    if (!shot.save(out)) {
        qWarning("не удалось сохранить %s", qPrintable(out));
        return 1;
    }
    qInfo("сохранено: %s (%dx%d)", qPrintable(out), shot.width(), shot.height());
    return 0;
}
