/**
 * Снимок вёрстки диалога активации — без сети, без ядра, без настроек.
 *
 * Нужен ровно затем, чтобы на диалог можно было ПОСМОТРЕТЬ, не запуская
 * клиент: на машине разработчика поднят рабочий туннель, и второй экземпляр
 * приложения ради проверки вёрстки — плохой размен.
 *
 * Поднимает только сгенерированную вёрстку (ui_dialog_relay_activate.h),
 * заполняет подписи так, как их увидит человек, и сохраняет png.
 * В выпуск не входит: EXCLUDE_FROM_ALL.
 */

#include "ui_dialog_relay_activate.h"

#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QPixmap>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("dialog.png");
    const bool activated = argc > 2 && QString::fromLocal8Bit(argv[2]) == "activated";

    QDialog d;
    Ui::DialogRelayActivate ui;
    ui.setupUi(&d);

    // Тема приложения — чтобы смотреть на то, что увидит человек, а не на
    // системную серость. Файл тот же, что грузит ThemeManager.
    QFile qss(QStringLiteral(":/neko/theme/feiyangqingyun/qss/modern.css"));
    if (!qss.open(QIODevice::ReadOnly)) {
        qss.setFileName(QStringLiteral("../res/theme/feiyangqingyun/qss/modern.css"));
        qss.open(QIODevice::ReadOnly);
    }
    if (qss.isOpen()) d.setStyleSheet(QString::fromUtf8(qss.readAll()));

    // Та же типографика, что задаёт dressUp(): набор смотрит на вёрстку, но
    // повторять её вручную нельзя — разъедется. Повторено ровно то, что можно
    // повторить, не таща за собой весь диалог.
    {
        const auto base = d.font();
        QFont big = base; big.setPointSize(base.pointSize() + 3); big.setBold(true);
        ui.state->setFont(big);
        QFont small = base; small.setPointSize(qMax(base.pointSize() - 1, 7));
        ui.hint->setFont(small); ui.footnote->setFont(small); ui.getCode->setFont(small);
        auto fade = [](QWidget *w, qreal a) {
            auto *e = new QGraphicsOpacityEffect(w);
            e->setOpacity(a);
            w->setGraphicsEffect(e);
        };
        fade(ui.hint, 0.72);
        fade(ui.footnote, 0.62);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(base.pointSize() + 9); mono.setBold(true);
        mono.setLetterSpacing(QFont::AbsoluteSpacing, 6);
        ui.code->setFont(mono);
    }

    if (activated) {
        ui.state->setText(QStringLiteral("Подключено к вашей подписке"));
        ui.activate->setText(QStringLiteral("Активировать заново"));
        ui.result->setText(QStringLiteral("Готово. Резервное подключение активировано."));
        ui.action->setVisible(false);
    } else {
        ui.state->setText(QStringLiteral("Не активировано"));
        ui.forget->setEnabled(false);
        ui.result->setTextFormat(Qt::RichText);
        ui.result->setText(QStringLiteral(
            "<b>Подписка закончилась — продлите её, чтобы продолжить</b>"));
        ui.action->setText(QStringLiteral("Продлить"));
        ui.action->setVisible(true);
    }

    // Размер из .ui, а не sizeHint: смотреть надо на то, что увидит человек.
    d.show();
    app.processEvents();
    d.grab().save(out);
    return 0;
}
