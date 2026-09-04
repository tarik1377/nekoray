/**
 * Снимок панели «Зелёный Ритм» — без сети, без ядра, без настроек.
 *
 * Панель собирается кодом, а не из .ui, поэтому предпросмотр вёрстки в редакторе
 * форм невозможен, и единственная альтернатива — запускать клиент. На машине
 * владельца поднят рабочий туннель, и второй экземпляр ради проверки отступов —
 * плохой размен. Отсюда эта цель.
 *
 * Панель ничего не знает о NekoGui::dataStore именно ради этого файла: сюда
 * линкуется один её .cpp, а не половина приложения.
 *
 * В выпуск не входит: EXCLUDE_FROM_ALL.
 *   ninja greenrhythm_panel_preview && ./greenrhythm_panel_preview panel.png
 */

#include "ui/dialog_greenrhythm.h"
#include "ui/dialog_whatbroke.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("panel.png");
    const bool connected = !(argc > 2 && QString::fromLocal8Bit(argv[2]) == "offline");

    // Режим «полоска»: знак кнопки в тех размерах, в которых его увидят.
    // Смотреть на иконку в 34 точки и делать вывод про 24 — самообман: именно
    // на малом размере тонкая линия и пропадает.
    if (argc > 2 && QString::fromLocal8Bit(argv[2]) == "icons") {
        const QIcon mark(QStringLiteral(":/icon/gr-panel.svg"));
        const QList<int> sizes{16, 24, 34, 48};
        int width = 24;
        for (int px: sizes) width += px + 24;
        QPixmap sheet(width, 96);
        sheet.fill(QColor(QStringLiteral("#2f343b")));
        QPainter p(&sheet);
        int x = 24;
        for (int px: sizes) {
            p.drawPixmap(x, 24, mark.pixmap(px, px));
            p.setPen(QColor(QStringLiteral("#9aa0a8")));
            p.drawText(QRect(x - 12, 74, px + 24, 16), Qt::AlignCenter, QString::number(px));
            x += px + 24;
        }
        p.end();
        return sheet.save(out) ? 0 : 1;
    }

    // Режим «приговор»: окно разбора с готовыми записями. Именно приговор и надо
    // читать глазами — числа и формулировки в нём человек будет принимать за
    // правду о своей машине.
    if (argc > 2 && QString::fromLocal8Bit(argv[2]) == "verdict") {
        DialogWhatBroke w;
        w.setAlreadyDirect({});
        const QString game = QStringLiteral("SquadGame-Win64-Shipping.exe");
        w.inspect(game);
        QList<GreenRhythm::Seen> batch;
        auto add = [&](const QString &p, const QString &tag, const QString &net,
                       const QString &dest, qint64 start) {
            GreenRhythm::Seen s;
            s.process = p; s.tag = tag; s.network = net; s.dest = dest; s.start = start;
            batch += s;
        };
        add(game, "bypass", "tcp", "104.16.0.1:443", 1);
        add(game, "bypass", "tcp", "104.16.0.2:443", 2);
        add(game, "proxy", "udp", "185.207.214.36:15020", 3);
        add(game, "proxy", "udp", "185.207.214.36:15021", 4);
        add("EpicOnlineServicesUserHelper.exe", "proxy", "tcp", "1.2.3.4:443", 5);
        w.feed(batch);
        w.conclude();
        w.resize(560, 520);
        w.show();
        app.processEvents();
        return w.grab().save(out) ? 0 : 1;
    }

    DialogGreenRhythm d;

    // Тема приложения — чтобы смотреть на то, что увидит человек, а не на
    // системную серость. Файл тот же, что грузит ThemeManager.
    QFile qss(QStringLiteral("../res/theme/feiyangqingyun/qss/modern.css"));
    if (!qss.open(QIODevice::ReadOnly)) {
        qss.setFileName(QStringLiteral("res/theme/feiyangqingyun/qss/modern.css"));
        qss.open(QIODevice::ReadOnly);
    }
    if (qss.isOpen()) d.setStyleSheet(QString::fromUtf8(qss.readAll()));

    d.setConnectionState(connected, connected ? QStringLiteral("Германия · 203.0.113.30") : QString());
    d.setAutopilot(true);
    d.setBypassList(QStringLiteral("SquadGame-Win64-Shipping.exe\nEasyAntiCheat_EOS.exe"));

    // Высота — аргументом: панель длиннее экрана, и снимок фиксированной высоты
    // оставил бы нижние разделы непроверенными. В клиенте она прокручивается,
    // здесь же нужна целиком, разом.
    const int height = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 1000;
    d.resize(600, height > 200 ? height : 1000);
    d.show();
    app.processEvents();

    QPixmap shot = d.grab();
    if (!shot.save(out)) {
        qWarning("не удалось сохранить %s", qPrintable(out));
        return 1;
    }
    qInfo("сохранено: %s  (%dx%d)", qPrintable(out), shot.width(), shot.height());
    return 0;
}
