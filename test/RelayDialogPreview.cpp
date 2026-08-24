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
#include <QPixmap>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("dialog.png");
    const bool activated = argc > 2 && QString::fromLocal8Bit(argv[2]) == "activated";

    QDialog d;
    Ui::DialogRelayActivate ui;
    ui.setupUi(&d);

    if (activated) {
        ui.state->setText(QStringLiteral("Подключено к вашей подписке."));
        ui.activate->setText(QStringLiteral("Активировать заново"));
        ui.result->setText(QStringLiteral("Готово. Резервное подключение активировано."));
        ui.action->setVisible(false);
    } else {
        ui.state->setText(QStringLiteral("Не активировано на этом устройстве."));
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
