/**
 * НАЖИМАЕТСЯ ЛИ КНОПКА ПИТАНИЯ МЫШЬЮ.
 *
 * ЖИВОЙ СЛУЧАЙ. 17.09.2026 владелец: «в подключении кнопка не работает
 * включения или выключения». Круглая кнопка на странице «Подключение» с 1.8.0
 * не отзывалась на мышь вовсе: ни подключить, ни отключить, и даже курсор над
 * ней оставался стрелкой. Подключение при этом поднималось — само при запуске
 * или из списка серверов, — поэтому поломка прожила три выпуска.
 *
 * ПОЧЕМУ. С 1.8.0 кнопка лежит внутри ореола (PowerGlow в ui/MainShell.cpp), и
 * ореолу поставили Qt::WA_TransparentForMouseEvents — «чтобы нажималась сама
 * кнопка». Но Qt снимает доставку мыши с виджета ВМЕСТЕ С ЕГО ДЕТЬМИ: поиск
 * виджета под курсором (QWidget::childAt) пропускает такой виджет целиком и в
 * детей не заходит. Нажатие получала страница под кнопкой и молча глотала.
 *
 * ПОЧЕМУ НАЖАТИЕ ОТДАЁТСЯ ОКНУ, А НЕ КНОПКЕ. power->click() и
 * QTest::mouseClick(кнопка) вручают событие прямо кнопке, мимо поиска под
 * курсором, — и были бы зелёными на сломанной кнопке. Здесь событие получает
 * окно в своих координатах, а получателя Qt ищет сам, как для настоящей мыши.
 *
 * Запуск: ninja power_button_test && ./power_button_test
 */

#include "ui/MainShell.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QWindow>

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

static int finish() {
    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}

// Нажать и отпустить левую кнопку в точке окна — тем путём, каким приходит мышь.
static void clickWindowAt(QWidget *top, const QPoint &at) {
    QWindow *window = top->windowHandle();
    const QPointF local(at);
    const QPointF global(top->mapToGlobal(at));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
    QCoreApplication::processEvents();
}

int main(int argc, char **argv) {
    // Окно обязано быть показанным: скрытых детей поиск под курсором не видит.
    // Рисовать его на экране незачем, а на сборочной машине экрана может не быть.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    GreenRhythm::MainShell shell;
    shell.resize(1100, 720);
    shell.show();
    QCoreApplication::processEvents();

    auto *power = shell.findChild<QPushButton *>(QStringLiteral("grPower"));
    is(QStringLiteral("кнопка питания найдена"), power != nullptr);
    is(QStringLiteral("у окна есть QWindow"), shell.windowHandle() != nullptr);
    if (power == nullptr || shell.windowHandle() == nullptr) return finish();

    int toggled = 0;
    QObject::connect(&shell, &GreenRhythm::MainShell::connectToggled, [&toggled] { toggled++; });

    // Центр считается заново после каждой смены состояния: подписи меняют
    // вёрстку, и кнопка могла бы сдвинуться.
    auto centerOfPower = [&] {
        QCoreApplication::processEvents();
        return power->mapTo(&shell, power->rect().center());
    };

    // НЕ ПОДКЛЮЧЕНО: нажатие подключает.
    shell.setConnectionState(false, QString(), QString());
    QPoint at = centerOfPower();
    is(QStringLiteral("не подключено: кнопка доступна"), power->isEnabled());
    is(QStringLiteral("не подключено: под центром кнопки мышь находит саму кнопку"),
       shell.childAt(at) == power);
    clickWindowAt(&shell, at);
    is(QStringLiteral("не подключено: нажатие мышью доходит до connectToggled"), toggled == 1);

    // ПОДКЛЮЧЕНО: то же нажатие отключает. Ореол в этом состоянии горит, а в
    // покое нет, — поэтому проверяются оба.
    shell.setConnectionState(true, QStringLiteral("Германия"), QStringLiteral("32 мс"));
    at = centerOfPower();
    is(QStringLiteral("подключено: кнопка доступна"), power->isEnabled());
    is(QStringLiteral("подключено: под центром кнопки мышь находит саму кнопку"),
       shell.childAt(at) == power);
    clickWindowAt(&shell, at);
    is(QStringLiteral("подключено: нажатие мышью доходит до connectToggled"), toggled == 2);

    return finish();
}
