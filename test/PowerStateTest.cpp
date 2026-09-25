/**
 * ОПРОС СОСТОЯНИЯ НЕ ПЕРЕБИВАЕТ «ПОДКЛЮЧАЮСЬ…» И «НЕ УДАЛОСЬ».
 *
 * ЖИВОЙ СЛУЧАЙ. Окно раз в две секунды зовёт refresh_status, а тот —
 * MainShell::setConnectionState(подключено ли, …). Этот вызов всегда ставил
 * «подключено» или «не подключено», и два из четырёх состояний кнопки жили не
 * дольше двух секунд:
 *  - причина отказа («Не удалось подключиться: сервер не ответил») сменялась на
 *    «Нажмите для подключения» раньше, чем её успевали прочесть;
 *  - на медленном подключении «Подключаюсь…» сменялось покоем, кнопка снова
 *    нажималась, и второе нажатие упиралось в «Another profile is starting...».
 *
 * Первая часть проверяет саму оболочку, без окна и ядра: опрос не снимает
 * попытку и отказ, их снимают конец попытки, нажатие, выбор сервера и успешное
 * подключение, а сторож не даёт кнопке остаться выключенной навсегда. Нажатие
 * отдаётся окну, как в PowerButtonTest.cpp, — иначе проверялась бы не мышь;
 * сервер выбирается в настоящем окне выбора, как это делает человек.
 *
 * Вторая часть сторожит исходник окна, как в WidgetLifetimeTest.cpp: каждый
 * ранний выход из neko_start обязан закончить попытку сам. Иначе кнопка ждала
 * бы сторожа — минуту выключенной.
 *
 * Запуск: ninja power_state_test && ./power_state_test
 */

#include "ui/MainShell.hpp"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QWindow>

#include <cstdio>

using State = GreenRhythm::MainShell::State;

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

// Подпись под кнопкой в покое — у «лесного» вида (#38) она зовёт выбрать сервер.
static const QString kIdle = QStringLiteral("Выберите сервер и нажмите кнопку");
static const QString kConnecting = QStringLiteral("Подключаюсь…");
static const QString kFailed = QStringLiteral("Не удалось подключиться");
static const QString kReason = QStringLiteral("сервер не ответил");

/** Оболочка на экране и её кнопка, подпись под кнопкой и карточка выбора сервера. */
struct Shell {
    GreenRhythm::MainShell w;
    QPushButton *power = nullptr;
    QLabel *hint = nullptr;
    QPushButton *choice = nullptr;
    int toggled = 0;
    int chosen = -100;

    Shell() {
        w.resize(1100, 720);
        w.show();
        QCoreApplication::processEvents();
        power = w.findChild<QPushButton *>(QStringLiteral("grPower"));
        hint = w.findChild<QLabel *>(QStringLiteral("grPowerHint"));
        choice = w.findChild<QPushButton *>(QStringLiteral("grServerChoice"));
        QObject::connect(&w, &GreenRhythm::MainShell::connectToggled, [this] { toggled++; });
        QObject::connect(&w, &GreenRhythm::MainShell::serverChosen, [this](int id) { chosen = id; });
    }

    bool ready() const { return power != nullptr && hint != nullptr && choice != nullptr; }
    QString text() const { return hint->text(); }

    /** Опрос окна: так его делает refresh_status каждые две секунды. */
    void poll(bool connected, int times = 5) {
        for (int i = 0; i < times; i++) {
            w.setConnectionState(connected, connected ? QStringLiteral("Германия") : QString(),
                                 connected ? QStringLiteral("32 мс") : QString());
        }
    }

    /**
     * Выбрать сервер так, как это делает человек: открыть окно выбора, отметить
     * строку и нажать «Выбрать». Окно модальное, поэтому ответ на него
     * готовится заранее — он сработает, когда окно уже будет на экране.
     */
    void chooseServerRow(int row) {
        QTimer::singleShot(0, [row] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog == nullptr) return;
            auto *list = dialog->findChild<QListWidget *>();
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (list == nullptr || buttons == nullptr) {
                dialog->reject();
                return;
            }
            list->setCurrentRow(row);
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        choice->click();
    }

    /** Нажать кнопку мышью: событие получает окно, получателя Qt ищет сам. */
    void clickPower() {
        QCoreApplication::processEvents();
        const QPoint at = power->mapTo(&w, power->rect().center());
        QWindow *window = w.windowHandle();
        const QPointF local(at);
        const QPointF global(w.mapToGlobal(at));
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        QCoreApplication::processEvents();
    }
};

static void spin(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

int main(int argc, char **argv) {
    // Окну нужен QWindow, а экрана на сборочной машине может не быть.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // ---- ЧАСТЬ 1: ОБОЛОЧКА ----

    {
        Shell s;
        is(QStringLiteral("кнопка, подпись и карточка выбора сервера найдены"), s.ready());
        if (!s.ready()) goto source;

        s.w.setState(State::Connecting);
        s.poll(false);
        is(QStringLiteral("опрос «не подключено» не снимает «Подключаюсь…»"), s.text() == kConnecting);
        is(QStringLiteral("и кнопка остаётся выключенной — второго нажатия нет"), !s.power->isEnabled());
        is(QStringLiteral("оболочка знает, что попытка идёт"), s.w.isConnecting());

        // Смена сервера: прежний профиль ещё работает, пока окно его не остановит.
        s.poll(true);
        is(QStringLiteral("опрос «подключено к прежнему» тоже не снимает попытку"), s.text() == kConnecting);

        s.w.finishConnecting();
        is(QStringLiteral("конец попытки при живом подключении — «Подключено»"),
           s.text().startsWith(QStringLiteral("Подключено")) && s.power->isEnabled());
        is(QStringLiteral("после конца попытки она не числится идущей"), !s.w.isConnecting());
    }

    {
        Shell s;
        s.w.setState(State::Connecting);
        s.poll(false);
        s.w.finishConnecting();
        is(QStringLiteral("конец попытки без подключения — снова «Нажмите для подключения»"), s.text() == kIdle);
        is(QStringLiteral("и кнопка снова нажимается"), s.power->isEnabled());
    }

    {
        Shell s;
        s.w.setState(State::Connecting);
        s.w.setState(State::Failed, kReason);
        s.poll(false);
        is(QStringLiteral("опрос не стирает причину отказа"), s.text() == kFailed + QStringLiteral(": ") + kReason);
        is(QStringLiteral("при отказе кнопка нажимается"), s.power->isEnabled());
        is(QStringLiteral("отказ — это конец попытки"), !s.w.isConnecting());

        // Поток попытки зовёт finishConnecting в любом исходе — и после отказа тоже.
        s.w.finishConnecting();
        s.poll(false);
        is(QStringLiteral("конец попытки после отказа причину не стирает"), s.text().contains(kReason));

        s.clickPower();
        is(QStringLiteral("нажатие при отказе доходит до connectToggled"), s.toggled == 1);
        is(QStringLiteral("нажатие снимает отказ"), s.text() == kIdle);
        s.poll(false);
        is(QStringLiteral("и опрос его не возвращает"), s.text() == kIdle);
    }

    {
        Shell s;
        s.w.setServers({{7, QStringLiteral("Германия"), QStringLiteral("32 мс")}}, -1);
        s.w.setState(State::Failed, kReason);
        // Строка 0 — автовыбор, строка 1 — «Германия».
        s.chooseServerRow(1);
        is(QStringLiteral("выбор сервера доходит до serverChosen"), s.chosen == 7);
        is(QStringLiteral("выбор другого сервера снимает отказ"), s.text() == kIdle);
    }

    {
        Shell s;
        s.w.setState(State::Failed, kReason);
        s.w.setState(State::Connecting);
        s.poll(true);
        s.w.finishConnecting();
        is(QStringLiteral("успешная попытка снимает отказ"), s.text().startsWith(QStringLiteral("Подключено")));
        s.poll(false);
        is(QStringLiteral("после отключения — покой, а не старый отказ"), s.text() == kIdle);
    }

    {
        Shell s;
        s.w.setConnectingTimeout(200);
        s.w.setState(State::Connecting);
        s.poll(false);
        spin(600);
        is(QStringLiteral("сторож снимает зависшую попытку: кнопка снова нажимается"), s.power->isEnabled());
        is(QStringLiteral("и честно говорит, что не вышло"), s.text().startsWith(kFailed));
        s.poll(false);
        is(QStringLiteral("этот отказ опрос тоже не стирает"), s.text().startsWith(kFailed));
    }

source:
    // ---- ЧАСТЬ 2: СТОРОЖ ИСХОДНИКА ОКНА ----
    //
    // Проверяются ОТНОШЕНИЯ, а не номера строк: номера — третья копия того же
    // знания, и разъезжаются они молча.
    {
        const QString grpc = slurp(QStringLiteral("ui/mainwindow_grpc.cpp"));
        is(QStringLiteral("исходник mainwindow_grpc.cpp прочитан"), !grpc.isEmpty());
        const int from = grpc.indexOf(QStringLiteral("void MainWindow::neko_start("));
        const int to = grpc.indexOf(QStringLiteral("void MainWindow::neko_stop("));
        const QString body = (from >= 0 && to > from) ? grpc.mid(from, to - from) : QString();
        is(QStringLiteral("тело neko_start найдено"), !body.isEmpty());

        const int begin = body.indexOf(QStringLiteral("State::Connecting"));
        is(QStringLiteral("neko_start начинает попытку"), begin >= 0);

        auto between = [&body](const QString &start, const QString &end) {
            const int a = body.indexOf(start);
            if (a < 0) return QString();
            const int b = body.indexOf(end, a);
            return b > a ? body.mid(a, b - a) : QString();
        };

        is(QStringLiteral("ошибка BuildConfig кончается отказом до выхода"),
           between(QStringLiteral("if (!result->error.isEmpty())"), QStringLiteral("return;"))
               .contains(QStringLiteral("State::Failed")));
        is(QStringLiteral("ядро не ответило (!rpcOK) — отказ до выхода"),
           between(QStringLiteral("if (!rpcOK)"), QStringLiteral("return false;"))
               .contains(QStringLiteral("State::Failed")));
        is(QStringLiteral("идущая остановка заканчивает попытку до выхода"),
           between(QStringLiteral("if (!mu_stopping.tryLock())"), QStringLiteral("return;"))
               .contains(QStringLiteral("finishConnecting()")));
        // Обратное тоже важно: при «Another profile is starting» идёт ЧУЖАЯ
        // попытка, и закончить её отсюда — ровно тот баг, ради которого набор.
        is(QStringLiteral("повторное нажатие не заканчивает чужую попытку"),
           !between(QStringLiteral("if (!mu_starting.tryLock())"), QStringLiteral("return;"))
                .contains(QStringLiteral("finishConnecting")));
        const int stage = body.indexOf(QStringLiteral("if (!neko_start_stage2())"));
        is(QStringLiteral("поток попытки после neko_start_stage2 зовёт finishConnecting"),
           stage > begin && body.indexOf(QStringLiteral("finishConnecting()"), stage) > stage);

        const QString mw = slurp(QStringLiteral("ui/mainwindow.cpp"));
        const int crash = mw.indexOf(QStringLiteral("info == \"CoreCrashed\""));
        is(QStringLiteral("падение ядра заканчивает висящую попытку отказом"),
           crash >= 0 && mw.mid(crash, 900).contains(QStringLiteral("State::Failed")));
    }

    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
