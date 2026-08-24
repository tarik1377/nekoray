/**
 * Снимок экрана выбора режима — без мака, без ядра, без настроек.
 *
 * Экран показывается только на macOS, а посмотреть на него надо здесь: иначе
 * вёрстка проверяется впервые у человека. Сам класс собирается на всех
 * платформах именно ради этого — под #ifdef он не компилировался бы нигде,
 * кроме своей, и любая ошибка в нём доезжала бы до выпуска непрочитанной.
 *
 * Настройки подменены заглушкой: диалог трогает dataStore только чтобы
 * запомнить «объяснение показано», и тащить сюда весь слой ради одного поля
 * незачем.
 *
 * В выпуск не входит: EXCLUDE_FROM_ALL.
 */

#include "ui/dialog_macos_mode.h"

#include <QApplication>
#include <QFile>
#include <QPixmap>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("macos-mode.png");

    DialogMacosMode d;

    // Тема приложения — чтобы смотреть на то, что увидит человек, а не на
    // системную серость. Файл тот же, что грузит ThemeManager.
    QFile qss(QStringLiteral(":/neko/theme/feiyangqingyun/qss/modern.css"));
    if (!qss.open(QIODevice::ReadOnly)) {
        qss.setFileName(QStringLiteral("../res/theme/feiyangqingyun/qss/modern.css"));
        qss.open(QIODevice::ReadOnly);
    }
    if (qss.isOpen()) d.setStyleSheet(QString::fromUtf8(qss.readAll()));

    d.resize(760, 420);
    d.show();
    app.processEvents();

    d.grab().save(out);
    return 0;
}
