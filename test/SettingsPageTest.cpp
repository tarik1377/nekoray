/**
 * «НАСТРОЙКИ → ПРИЛОЖЕНИЕ»: ТО, ЧТО ЛЮДИ ТРОГАЮТ, — СТРОКАМИ СТРАНИЦЫ.
 *
 * Владелец, 25.09.2026, о новом виде (#38): «надо делать везде так же».
 * Автозапуск жил в меню «Ещё → Все команды», а «Спрятать окно при старте» и
 * автообновление подписки — в «Дополнительных настройках», между адресом
 * прокси и User-Agent. Теперь это строки страницы «Настройки»: название,
 * пояснение, переключатель, как у режимов.
 *
 * Первая часть — оболочка без окна: значения ставятся молча (опрос не должен
 * включать автозапуск сам), нажатие мышью шлёт сигнал ровно раз, пояснение
 * говорит правду об интервале. Нажатие отдаётся окну, как в
 * PowerButtonTest.cpp, — иначе проверялась бы не мышь.
 *
 * Вторая часть — правило числа подписки (ui/SubscriptionSchedule.hpp): знак —
 * включено ли, модуль — интервал. Таймер окна не заводится чаще раза в 30
 * минут, и переключатель не должен показывать «вкл», когда таймер стоит.
 *
 * Третья часть сторожит исходники: окно ведёт сигналы туда же, куда вели
 * старый диалог и пункт меню, а в старом диалоге этих галок больше нет. Диалог
 * открыт немодально: живи галка в двух местах, «Готово» в диалоге молча
 * вернуло бы то, что человек только что поменял на странице.
 *
 * Заход 2б — «Туннель». Переносить из него нечего: всё техническое. Но окно
 * открывалось только из «Ещё → Все команды», а три подписи («Stack», «Strict
 * Route», «FakeDNS») были помечены «не переводить» ещё в NekoRay — и стояли
 * по-английски посреди русского окна, хотя перевод в ru_RU.ts давно есть.
 *
 * Аудит 25.09 — «что ещё забыли». В «Ещё → Все команды» жили вещи, которые
 * людям нужны, но о которых они не знали: автоподключение при запуске
 * (пункт назывался «Запомнить последний профиль»), автопилот соединения
 * (включён у всех), резервное подключение (отдельная услуга), раздача VPN
 * другим устройствам, поддержка, диагностика и «О программе». Теперь это
 * строки страницы; меню осталось второй дверью к тем же пунктам. Адрес для
 * раздачи выбирает ui/LanAddress.hpp — у машины много адаптеров, и почти все
 * не те.
 *
 * Обновления: раньше проверка шла только по кнопке, и человек на сборке со
 * сломанной кнопкой питания о починке не узнавал. Теперь окно спрашивает само
 * (минута после запуска, дальше раз в сутки) и показывает найденную версию
 * строкой в колонке — без окон. Номер — из имени пакета (ui/UpdateNotice.hpp).
 *
 * Заход 2в — «Маршруты». Переносить тоже нечего, а вот слова — да: sniffing
 * был переведён как «Режим подслушивания». В VPN-клиенте это читается как
 * слежка, и по смыслу неверно: ядро лишь распознаёт домен по началу
 * соединения, чтобы правила по доменам работали.
 *
 * Запуск: ninja settings_page_test && ./settings_page_test
 */

#include "ui/LanAddress.hpp"
#include "ui/MainShell.hpp"
#include "ui/SubscriptionSchedule.hpp"
#include "ui/UpdateNotice.hpp"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QWindow>

#include <cstdio>

namespace Schedule = GreenRhythm::SubscriptionSchedule;

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

/** Исходник целиком, переводы строк — «\n»: сторожу всё равно, CRLF ли на диске. */
static QString slurp(const QString &path) {
    for (const QString &prefix: {QStringLiteral(""), QStringLiteral("../"), QStringLiteral("../../")}) {
        QFile f(prefix + path);
        if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    }
    return {};
}

/** Обработчик: от упоминания до конца его лямбды. */
static QString handler(const QString &source, const QString &anchor) {
    const int at = source.indexOf(anchor);
    if (at < 0) return {};
    const int end = source.indexOf(QStringLiteral("});"), at);
    return end > at ? source.mid(at, end - at) : QString();
}

/** Ветка if верхнего уровня функции: от заголовка до её закрывающей скобки. */
static QString branch(const QString &source, const QString &head) {
    const int at = source.indexOf(head);
    if (at < 0) return {};
    const int end = source.indexOf(QStringLiteral("\n    }\n"), at);
    return end > at ? source.mid(at, end - at) : QString();
}

/** Оболочка на экране и строки раздела «Приложение». */
struct Shell {
    GreenRhythm::MainShell w;
    QPushButton *autostart = nullptr;
    QPushButton *startHidden = nullptr;
    QPushButton *subscription = nullptr;
    QPushButton *checkUpdates = nullptr;
    QPushButton *tunnelSettings = nullptr;
    QLabel *subscriptionDetail = nullptr;
    QLabel *updatesDetail = nullptr;
    QList<bool> autostartSignals;
    QList<bool> startHiddenSignals;
    QList<bool> subscriptionSignals;
    int updateChecks = 0;
    int tunnelSettingsOpened = 0;

    Shell() {
        w.resize(1100, 720);
        w.show();
        QCoreApplication::processEvents();
        autostart = w.findChild<QPushButton *>(QStringLiteral("grAutostart"));
        startHidden = w.findChild<QPushButton *>(QStringLiteral("grStartHidden"));
        subscription = w.findChild<QPushButton *>(QStringLiteral("grSubscriptionAutoUpdate"));
        checkUpdates = w.findChild<QPushButton *>(QStringLiteral("grCheckUpdates"));
        tunnelSettings = w.findChild<QPushButton *>(QStringLiteral("grTunnelSettings"));
        subscriptionDetail = w.findChild<QLabel *>(QStringLiteral("grSubscriptionAutoUpdateDetail"));
        updatesDetail = w.findChild<QLabel *>(QStringLiteral("grCheckUpdatesDetail"));
        QObject::connect(&w, &GreenRhythm::MainShell::autostartToggled, [this](bool on) { autostartSignals << on; });
        QObject::connect(&w, &GreenRhythm::MainShell::startHiddenToggled, [this](bool on) { startHiddenSignals << on; });
        QObject::connect(&w, &GreenRhythm::MainShell::subscriptionAutoUpdateToggled,
                         [this](bool on) { subscriptionSignals << on; });
        QObject::connect(&w, &GreenRhythm::MainShell::checkUpdateRequested, [this] { updateChecks++; });
        QObject::connect(&w, &GreenRhythm::MainShell::tunnelSettingsRequested, [this] { tunnelSettingsOpened++; });
    }

    bool ready() const {
        return autostart != nullptr && startHidden != nullptr && subscription != nullptr &&
               checkUpdates != nullptr && subscriptionDetail != nullptr && updatesDetail != nullptr;
    }

    bool silent() const {
        return autostartSignals.isEmpty() && startHiddenSignals.isEmpty() && subscriptionSignals.isEmpty() &&
               updateChecks == 0;
    }

    /** Открыть страницу с кнопкой и докрутить до неё: человек нажимает то, что видит. */
    void reveal(QWidget *button) {
        auto *stack = w.findChild<QStackedWidget *>(QStringLiteral("grPages"));
        for (QWidget *p = button; p != nullptr; p = p->parentWidget()) {
            if (p->parentWidget() == stack) {
                stack->setCurrentWidget(p);
                break;
            }
        }
        for (QWidget *p = button; p != nullptr; p = p->parentWidget()) {
            if (auto *scroll = qobject_cast<QScrollArea *>(p)) {
                scroll->ensureWidgetVisible(button);
                break;
            }
        }
        QCoreApplication::processEvents();
    }

    /** Нажать мышью: событие получает окно, получателя Qt ищет сам. */
    void click(QPushButton *button) {
        reveal(button);
        const QPoint at = button->mapTo(&w, button->rect().center());
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

static void shellShowsTheTruth() {
    std::puts("Оболочка: значения ставятся молча, нажатие шлёт сигнал");
    Shell s;
    is(QStringLiteral("строки «Приложения» на месте: автозапуск, свёрнутым, подписка, обновления"), s.ready());
    if (!s.ready()) return;

    s.w.setAppOptions(true, false, 120);
    is(QStringLiteral("автозапуск показан включённым"), s.autostart->isChecked());
    is(QStringLiteral("«свёрнутым» показан выключенным"), !s.startHidden->isChecked());
    is(QStringLiteral("120 минут — автообновление включено"), s.subscription->isChecked());
    is(QStringLiteral("пояснение называет интервал: «раз в 2 часа»"),
       s.subscriptionDetail->text().contains(QStringLiteral("раз в 2 часа")));

    s.w.setAppOptions(false, true, -120);
    is(QStringLiteral("автозапуск показан выключенным"), !s.autostart->isChecked());
    is(QStringLiteral("«свёрнутым» показан включённым"), s.startHidden->isChecked());
    is(QStringLiteral("минус — автообновление выключено"), !s.subscription->isChecked());
    is(QStringLiteral("выключенное пояснение говорит, где обновить вручную"),
       s.subscriptionDetail->text().contains(QStringLiteral("вручную")));

    s.w.setAppOptions(false, false, 10);
    is(QStringLiteral("10 минут — таймер не заведётся, и переключатель не врёт «вкл»"), !s.subscription->isChecked());
    is(QStringLiteral("установка значений не шлёт ни одного сигнала"), s.silent());

    const struct {
        int minutes;
        const char *text;
    } every[] = {{30, "раз в 30 минут"}, {32, "раз в 32 минуты"}, {60, "раз в час"}, {90, "раз в 90 минут"},
                 {121, "раз в 121 минуту"}, {180, "раз в 3 часа"}, {300, "раз в 5 часов"},
                 {1320, "раз в 22 часа"}, {1440, "раз в сутки"}};
    for (const auto &e: every) {
        s.w.setAppOptions(false, false, e.minutes);
        const QString want = QString::fromUtf8(e.text);
        is(QStringLiteral("%1 мин — «%2»").arg(e.minutes).arg(want), s.subscriptionDetail->text().contains(want));
    }

    s.w.setAppOptions(false, false, 120);
    s.click(s.autostart);
    is(QStringLiteral("нажатие включает автозапуск: один сигнал «вкл»"), s.autostartSignals == QList<bool>{true});
    s.click(s.autostart);
    is(QStringLiteral("второе нажатие выключает"), s.autostartSignals == QList<bool>{true, false});
    s.click(s.startHidden);
    is(QStringLiteral("«свёрнутым»: один сигнал «вкл»"), s.startHiddenSignals == QList<bool>{true});
    s.click(s.subscription);
    is(QStringLiteral("автообновление было включено — нажатие выключает"),
       s.subscriptionSignals == QList<bool>{false});
    s.click(s.checkUpdates);
    is(QStringLiteral("«Проверить» просит проверку обновлений ровно раз"), s.updateChecks == 1);

    s.w.setAppVersion(QStringLiteral("1.8.3"));
    is(QStringLiteral("строка обновлений называет установленную версию"),
       s.updatesDetail->text().contains(QStringLiteral("1.8.3")));

    is(QStringLiteral("строка «Параметры туннеля» на месте"), s.tunnelSettings != nullptr);
    if (s.tunnelSettings == nullptr) return;
    s.click(s.tunnelSettings);
    is(QStringLiteral("«Настроить» у туннеля просит окно параметров ровно раз"), s.tunnelSettingsOpened == 1);
}

static void scheduleRule() {
    std::puts("Правило числа подписки: знак — включено, модуль — интервал");
    is(QStringLiteral("120 — включено"), Schedule::isOn(120));
    is(QStringLiteral("30 — включено: нижняя граница таймера"), Schedule::isOn(30));
    is(QStringLiteral("29 — выключено: таймер не заводится"), !Schedule::isOn(29));
    is(QStringLiteral("0 и минус — выключено"), !Schedule::isOn(0) && !Schedule::isOn(-120));

    is(QStringLiteral("выключить 120 → −120: интервал помнится"), Schedule::toggled(120, false) == -120);
    is(QStringLiteral("включить −45 → 45"), Schedule::toggled(-45, true) == 45);
    is(QStringLiteral("включить 0 → интервал по умолчанию"), Schedule::toggled(0, true) == Schedule::kDefaultMinutes);
    is(QStringLiteral("включить 10 → по умолчанию: меньше 30 таймер не примет"),
       Schedule::toggled(10, true) == Schedule::kDefaultMinutes);
    is(QStringLiteral("включить −10 → по умолчанию"), Schedule::toggled(-10, true) == Schedule::kDefaultMinutes);
    is(QStringLiteral("выключить 0 → −120: потом включится рабочим"),
       Schedule::toggled(0, false) == -Schedule::kDefaultMinutes);
    is(QStringLiteral("включить включённое — без изменений"), Schedule::toggled(90, true) == 90);
    is(QStringLiteral("выключить выключенное — без изменений"), Schedule::toggled(-90, false) == -90);
    is(QStringLiteral("включённое переключателем таймер заводит"), Schedule::isOn(Schedule::toggled(-5, true)));

    is(QStringLiteral("интервал при включённом — включённый"), Schedule::withInterval(120, 60) == 60);
    is(QStringLiteral("интервал при выключенном — выключенный"), Schedule::withInterval(-120, 60) == -60);
    is(QStringLiteral("интервал при нуле — выключенный"), Schedule::withInterval(0, 60) == -60);
    is(QStringLiteral("страница показывала «выкл» при 10 — поле интервала не включает за спиной"),
       Schedule::withInterval(10, 60) == -60);
}

static void sourcesWiredAndMoved() {
    std::puts("Исходники: проводка окна и старый диалог");
    const QString window = slurp(QStringLiteral("ui/mainwindow.cpp"));
    const QString header = slurp(QStringLiteral("ui/mainwindow.h"));
    is(QStringLiteral("исходник окна прочитан"), !window.isEmpty() && !header.isEmpty());

    const QString autostart = handler(window, QStringLiteral("MainShell::autostartToggled"));
    is(QStringLiteral("автозапуск → AutoRun_SetEnabled, как у пункта меню"),
       autostart.contains(QStringLiteral("AutoRun_SetEnabled(on)")));
    const QString hidden = handler(window, QStringLiteral("MainShell::startHiddenToggled"));
    is(QStringLiteral("«свёрнутым» → dataStore->start_minimal и сохранение"),
       hidden.contains(QStringLiteral("start_minimal = on")) && hidden.contains(QStringLiteral("Save()")));
    const QString subscription = handler(window, QStringLiteral("MainShell::subscriptionAutoUpdateToggled"));
    is(QStringLiteral("автообновление → правило SubscriptionSchedule"),
       subscription.contains(QStringLiteral("SubscriptionSchedule::toggled(")));
    is(QStringLiteral("автообновление → таймер окна перезаведён и сохранено"),
       subscription.contains(QStringLiteral("TM_auto_update_subsctiption_Reset_Minute(")) &&
           subscription.contains(QStringLiteral("Save()")));
    is(QStringLiteral("после каждого нажатия страница перечитывает правду"),
       autostart.contains(QStringLiteral("refresh_app_options()")) &&
           hidden.contains(QStringLiteral("refresh_app_options()")) &&
           subscription.contains(QStringLiteral("refresh_app_options()")));

    is(QStringLiteral("«Параметры туннеля» открывают прежнее окно «Туннеля»"),
       handler(window, QStringLiteral("MainShell::tunnelSettingsRequested"))
           .contains(QStringLiteral("on_menu_vpn_settings_triggered()")));
    is(QStringLiteral("правду на страницу кладёт окно: shell->setAppOptions"),
       window.contains(QStringLiteral("shell->setAppOptions(")) &&
           header.contains(QStringLiteral("void refresh_app_options();")));
    is(QStringLiteral("версия — из сборки"),
       window.contains(QStringLiteral("shell->setAppVersion(")) && window.contains(QStringLiteral("NKR_VERSION")));
    // Якорь — таймер подписки в конструкторе: он заводится после миграции
    // интервала. Строка сброса таймера есть и в обработчике переключателя, так
    // что искать по ней значило бы найти не то место.
    const int timer = window.indexOf(QStringLiteral("connect(TM_auto_update_subsctiption, &QTimer::timeout"));
    is(QStringLiteral("при запуске — после таймера подписки и миграции"),
       timer > 0 && window.indexOf(QStringLiteral("refresh_app_options();"), timer) > timer);
    is(QStringLiteral("пункт меню «Запуск с системой» — и страница следом"),
       handler(window, QStringLiteral("actionStart_with_system, &QAction::triggered"))
           .contains(QStringLiteral("refresh_app_options()")));
    is(QStringLiteral("после старого диалога (UpdateDataStore) — и страница следом"),
       branch(window, QStringLiteral("if (info.contains(\"UpdateDataStore\")) {"))
           .contains(QStringLiteral("refresh_app_options()")));

    const QString form = slurp(QStringLiteral("ui/dialog_basic_settings.ui"));
    const QString dialog = slurp(QStringLiteral("ui/dialog_basic_settings.cpp"));
    is(QStringLiteral("старый диалог прочитан"), !form.isEmpty() && !dialog.isEmpty());
    is(QStringLiteral("в старом диалоге нет галки «Спрятать окно при старте»"),
       !form.contains(QStringLiteral("name=\"start_minimal\"")) && !dialog.contains(QStringLiteral("start_minimal")));
    is(QStringLiteral("в старом диалоге нет галки автообновления"),
       !form.contains(QStringLiteral("name=\"sub_auto_update_enable\"")) &&
           !dialog.contains(QStringLiteral("sub_auto_update_enable")));
    is(QStringLiteral("поле интервала осталось в старом диалоге"),
       form.contains(QStringLiteral("name=\"sub_auto_update\"")));
    is(QStringLiteral("интервал сохраняется по правилу, знак — из настроек в момент сохранения"),
       dialog.contains(QStringLiteral("SubscriptionSchedule::withInterval(NekoGui::dataStore->sub_auto_update")));
    is(QStringLiteral("таймер после диалога — от сохранённого числа"),
       dialog.contains(QStringLiteral("TM_auto_update_subsctiption_Reset_Minute(NekoGui::dataStore->sub_auto_update)")));
    is(QStringLiteral("«свёрнутым» по-прежнему работает при запуске"),
       slurp(QStringLiteral("main/main.cpp")).contains(QStringLiteral("dataStore->start_minimal")));
}

/** Перевод строки из контекста ru_RU.ts: пусто — нет, не закончен или пустой. */
static QString russian(const QString &ts, const QString &context, const QString &source) {
    const int at = ts.indexOf(QStringLiteral("<name>%1</name>").arg(context));
    if (at < 0) return {};
    const int end = ts.indexOf(QStringLiteral("</context>"), at);
    const int src = ts.indexOf(QStringLiteral("<source>%1</source>").arg(source), at);
    if (src < 0 || src > end) return {};
    const int open = ts.indexOf(QStringLiteral("<translation"), src);
    const int close = ts.indexOf(QStringLiteral("</translation>"), open);
    if (open < 0 || close < 0 || close > end) return {};
    const int body = ts.indexOf(QLatin1Char('>'), open) + 1;
    if (ts.mid(open, body - open).contains(QStringLiteral("unfinished"))) return {};
    return ts.mid(body, close - body).trimmed();
}

static void tunnelDialogSpeaksRussian() {
    std::puts("«Туннель»: подписи переводятся, перевод есть");
    const QString form = slurp(QStringLiteral("ui/dialog_vpn_settings.ui"));
    const QString ts = slurp(QStringLiteral("translations/ru_RU.ts"));
    is(QStringLiteral("форма «Туннеля» и ru_RU.ts прочитаны"), !form.isEmpty() && !ts.isEmpty());
    for (const QString &label: {QStringLiteral("Stack"), QStringLiteral("Strict Route"), QStringLiteral("FakeDNS")}) {
        is(QStringLiteral("«%1» — без пометки «не переводить»").arg(label),
           form.contains(QStringLiteral("<string>%1</string>").arg(label)) &&
               !form.contains(QStringLiteral("notr=\"true\">%1<").arg(label)));
        is(QStringLiteral("«%1» — русский перевод в DialogVPNSettings есть").arg(label),
           !russian(ts, QStringLiteral("DialogVPNSettings"), label).isEmpty());
    }
}

static void routesDialogWording() {
    std::puts("«Маршруты»: слова, которые не пугают и не мешают языки");
    const QString ts = slurp(QStringLiteral("translations/ru_RU.ts"));
    const QString context = QStringLiteral("DialogManageRoutes");
    is(QStringLiteral("в русском переводе нет «подслушивания» — ни в одном окне"),
       !ts.isEmpty() && !ts.contains(QStringLiteral("одслушива")));
    is(QStringLiteral("Sniffing Mode — «Распознавание домена»"),
       russian(ts, context, QStringLiteral("Sniffing Mode")) == QStringLiteral("Распознавание домена"));
    is(QStringLiteral("Sniff result for routing — «Только для правил маршрутизации»"),
       russian(ts, context, QStringLiteral("Sniff result for routing")) == QStringLiteral("Только для правил маршрутизации"));
    is(QStringLiteral("Sniff result for destination — «Для правил и адреса подключения»"),
       russian(ts, context, QStringLiteral("Sniff result for destination")) ==
           QStringLiteral("Для правил и адреса подключения"));
    const QString global = russian(ts, context, QStringLiteral("Custom Route (global)"));
    is(QStringLiteral("«Свои маршруты (для всех наборов)» — без «(global)» посреди русского"),
       global == QStringLiteral("Свои маршруты (для всех наборов)"));
    is(QStringLiteral("Custom Route — «Свои маршруты»"),
       russian(ts, context, QStringLiteral("Custom Route")) == QStringLiteral("Свои маршруты"));
}

/** Строки, вынесенные из «Все команды», — на странице и отвечают на нажатие. */
static void hiddenFeaturesOnThePage() {
    std::puts("Вынесенное из «Все команды»: на странице, молча ставится, отвечает на нажатие");
    Shell s;
    const auto button = [&](const char *name) { return s.w.findChild<QPushButton *>(QString::fromLatin1(name)); };
    QPushButton *connectOnStart = button("grConnectOnStart");
    QPushButton *autopilot = button("grAutopilot");
    QPushButton *share = button("grShareToLan");
    QPushButton *relay = button("grRelay");
    QPushButton *support = button("grSupport");
    QPushButton *diagnostics = button("grDiagnostics");
    QPushButton *about = button("grAbout");
    auto *shareDetail = s.w.findChild<QLabel *>(QStringLiteral("grShareToLanDetail"));
    const bool ready = connectOnStart && autopilot && share && relay && support && diagnostics && about && shareDetail;
    is(QStringLiteral("строки на месте: при запуске, автопилот, раздача, резерв, поддержка, диагностика, о программе"), ready);
    if (!ready) return;
    is(QStringLiteral("три из них — переключатели, остальные — кнопки"),
       connectOnStart->isCheckable() && autopilot->isCheckable() && share->isCheckable() && !relay->isCheckable() &&
           !support->isCheckable() && !diagnostics->isCheckable() && !about->isCheckable());

    QList<bool> startSignals, autopilotSignals, shareSignals;
    int relays = 0, supports = 0, diagnoses = 0, abouts = 0;
    QObject::connect(&s.w, &GreenRhythm::MainShell::connectOnStartToggled, [&](bool on) { startSignals << on; });
    QObject::connect(&s.w, &GreenRhythm::MainShell::autopilotToggled, [&](bool on) { autopilotSignals << on; });
    QObject::connect(&s.w, &GreenRhythm::MainShell::shareToLanToggled, [&](bool on) { shareSignals << on; });
    QObject::connect(&s.w, &GreenRhythm::MainShell::relayRequested, [&] { relays++; });
    QObject::connect(&s.w, &GreenRhythm::MainShell::supportRequested, [&] { supports++; });
    QObject::connect(&s.w, &GreenRhythm::MainShell::diagnosticsRequested, [&] { diagnoses++; });
    QObject::connect(&s.w, &GreenRhythm::MainShell::aboutRequested, [&] { abouts++; });

    s.w.setConnectionOptions(true, false, false, QString(), 2080);
    is(QStringLiteral("«подключаться при запуске» показан включённым, автопилот и раздача — нет"),
       connectOnStart->isChecked() && !autopilot->isChecked() && !share->isChecked());
    is(QStringLiteral("выключенная раздача предупреждает про общественный Wi-Fi"),
       shareDetail->text().contains(QStringLiteral("общественном Wi-Fi")));
    s.w.setConnectionOptions(false, true, true, QStringLiteral("192.168.1.5"), 2080);
    is(QStringLiteral("включённая раздача называет адрес и порт: 192.168.1.5, 2080"),
       share->isChecked() && shareDetail->text().contains(QStringLiteral("192.168.1.5")) &&
           shareDetail->text().contains(QStringLiteral("2080")));
    s.w.setConnectionOptions(false, true, true, QString(), 2080);
    is(QStringLiteral("адрес не нашёлся — говорит, где его взять, и называет порт"),
       shareDetail->text().contains(QStringLiteral("адрес этого компьютера")) &&
           shareDetail->text().contains(QStringLiteral("2080")));
    is(QStringLiteral("установка значений не шлёт ни одного сигнала"),
       startSignals.isEmpty() && autopilotSignals.isEmpty() && shareSignals.isEmpty());

    s.click(connectOnStart);
    is(QStringLiteral("нажатие «при запуске» — один сигнал «вкл»"), startSignals == QList<bool>{true});
    s.click(autopilot);
    is(QStringLiteral("автопилот был включён — нажатие выключает"), autopilotSignals == QList<bool>{false});
    s.click(share);
    is(QStringLiteral("раздача была включена — нажатие выключает"), shareSignals == QList<bool>{false});
    s.click(relay);
    s.click(support);
    s.click(diagnostics);
    s.click(about);
    is(QStringLiteral("резерв, поддержка, диагностика, о программе — по одному разу"),
       relays == 1 && supports == 1 && diagnoses == 1 && abouts == 1);
}

/** Какой адрес назвать другим устройствам — ui/LanAddress.hpp. */
static void lanAddressPick() {
    std::puts("Адрес для раздачи: живой настоящий адаптер, частный IPv4");
    using GreenRhythm::Lan::Candidate;
    using GreenRhythm::Lan::pick;
    const auto c = [](const char *name, const char *address, bool up = true, bool loopback = false) {
        return Candidate{QString::fromUtf8(name), QString::fromLatin1(address), up, loopback};
    };
    is(QStringLiteral("домашний Wi-Fi, а не наш туннель, даже если туннель первый"),
       pick({c("neko-tun", "172.19.0.1"), c("Wi-Fi", "192.168.1.5")}) == QStringLiteral("192.168.1.5"));
    is(QStringLiteral("Ethernet 10.x, а не виртуальная сеть WSL"),
       pick({c("vEthernet (WSL)", "172.25.64.1"), c("Ethernet", "10.0.0.12")}) == QStringLiteral("10.0.0.12"));
    is(QStringLiteral("192.168 лучше 10.x"),
       pick({c("Ethernet", "10.1.2.3"), c("Wi-Fi", "192.168.0.7")}) == QStringLiteral("192.168.0.7"));
    is(QStringLiteral("точка доступа iPhone 172.20.10.x годится, когда другого нет"),
       pick({c("Wi-Fi", "172.20.10.4")}) == QStringLiteral("172.20.10.4"));
    is(QStringLiteral("Docker, VirtualBox, VMware — мимо"),
       pick({c("docker0", "172.17.0.1"), c("VirtualBox Host-Only Network", "192.168.56.1"),
             c("VMware Network Adapter VMnet8", "192.168.80.1")}).isEmpty());
    is(QStringLiteral("выключенный, петлевой, link-local, публичный, IPv6 — мимо"),
       pick({c("Ethernet 2", "192.168.5.5", false), c("Loopback Pseudo-Interface 1", "127.0.0.1", true, true),
             c("Ethernet", "169.254.3.3"), c("Ethernet", "85.12.3.4"), c("Wi-Fi", "fe80::1")}).isEmpty());
    is(QStringLiteral("адреса нашего туннеля 172.19.0.0/28 — мимо и под чужим именем"),
       pick({c("Unknown adapter", "172.19.0.1")}).isEmpty());
    is(QStringLiteral("Wintun и Teredo по имени системы — мимо"),
       pick({c("GreenRhythm wintun", "10.7.0.2"), c("Teredo Tunneling Pseudo-Interface tunnel_32512", "10.9.9.9")})
           .isEmpty());
}

/** Окно ведёт вынесенные строки туда же, куда вели пункты меню. */
static void hiddenFeaturesWired() {
    std::puts("Исходник окна: вынесенное ведёт в те же пункты меню");
    const QString window = slurp(QStringLiteral("ui/mainwindow.cpp"));
    for (const auto &pair: QList<QPair<QString, QString>>{
             {QStringLiteral("relayRequested"), QStringLiteral("menu_gr_relay")},
             {QStringLiteral("supportRequested"), QStringLiteral("menu_gr_telegram")},
             {QStringLiteral("diagnosticsRequested"), QStringLiteral("menu_gr_diag")},
             {QStringLiteral("aboutRequested"), QStringLiteral("menu_gr_about")}}) {
        is(QStringLiteral("%1 → пункт %2").arg(pair.first, pair.second),
           window.contains(QStringLiteral("MainShell::%1, ui->%2, &QAction::trigger").arg(pair.first, pair.second)));
    }
    is(QStringLiteral("автопилот — через галку пункта меню, как у панели «Зелёный Ритм»"),
       handler(window, QStringLiteral("MainShell::autopilotToggled"))
           .contains(QStringLiteral("ui->menu_gr_autopilot->setChecked(on)")));
    const QString start = handler(window, QStringLiteral("MainShell::connectOnStartToggled"));
    is(QStringLiteral("«подключаться при запуске» → remember_enable и сохранение"),
       start.contains(QStringLiteral("remember_enable = on")) && start.contains(QStringLiteral("Save()")));
    const QString share = handler(window, QStringLiteral("MainShell::shareToLanToggled"));
    is(QStringLiteral("раздача → адрес входа и UpdateDataStore (вопрос о перезапуске ядра)"),
       share.contains(QStringLiteral("inbound_address = on ? \"::\" : \"127.0.0.1\"")) &&
           share.contains(QStringLiteral("UpdateDataStore")));
    const QString refresh = window.mid(window.indexOf(QStringLiteral("void MainWindow::refresh_app_options()")), 1400);
    is(QStringLiteral("правду на страницу кладёт окно: setConnectionOptions с настройками и адресом"),
       refresh.contains(QStringLiteral("shell->setConnectionOptions(")) &&
           refresh.contains(QStringLiteral("remember_enable")) && refresh.contains(QStringLiteral("connection_autopilot")) &&
           refresh.contains(QStringLiteral("GreenRhythm::Lan::pick(")));
    is(QStringLiteral("пункт меню «Запомнить последний профиль» — и страница следом"),
       handler(window, QStringLiteral("actionRemember_last_proxy, &QAction::triggered"))
           .contains(QStringLiteral("refresh_app_options()")));
    is(QStringLiteral("галка автопилота в меню — и страница следом"),
       handler(window, QStringLiteral("menu_gr_autopilot, &QAction::toggled"))
           .contains(QStringLiteral("refresh_app_options()")));
}

/** Найденная версия — строкой в колонке и в «Обновлениях программы». */
static void updateNoticeInSidebar() {
    std::puts("Новая версия: строка в колонке, без окна");
    Shell s;
    auto *notice = s.w.findChild<QWidget *>(QStringLiteral("grUpdateNotice"));
    auto *noticeText = s.w.findChild<QLabel *>(QStringLiteral("grUpdateNoticeText"));
    auto *noticeButton = s.w.findChild<QPushButton *>(QStringLiteral("grUpdateNoticeButton"));
    is(QStringLiteral("блок о новой версии есть в колонке: текст и «Обновить»"),
       notice != nullptr && noticeText != nullptr && noticeButton != nullptr && s.ready());
    if (notice == nullptr || noticeText == nullptr || noticeButton == nullptr || !s.ready()) return;
    is(QStringLiteral("пока новой версии нет — строка спрятана"), notice->isHidden());

    s.w.setAppVersion(QStringLiteral("1.8.3"));
    s.w.setUpdateAvailable(true, QStringLiteral("1.8.4"));
    is(QStringLiteral("вышла 1.8.4 — блок виден и называет номер"),
       !notice->isHidden() && noticeText->text().contains(QStringLiteral("1.8.4")));
    is(QStringLiteral("текст блока переносится, а не режется краем колонки"), noticeText->wordWrap());
    is(QStringLiteral("«Обновления программы» называют и новую, и установленную"),
       s.updatesDetail->text().contains(QStringLiteral("1.8.4")) &&
           s.updatesDetail->text().contains(QStringLiteral("1.8.3")));
    is(QStringLiteral("кнопка строки — «Обновить», а не «Проверить»"),
       s.checkUpdates->text() == QStringLiteral("Обновить"));
    is(QStringLiteral("установка значения не шлёт сигнала"), s.updateChecks == 0);
    s.click(noticeButton);
    is(QStringLiteral("«Обновить» в колонке — прежняя проверка с кнопками, ровно раз"), s.updateChecks == 1);

    s.w.setUpdateAvailable(true);
    is(QStringLiteral("номер не нашёлся — «новая версия» без номера"),
       !notice->isHidden() && noticeText->text().contains(QStringLiteral("новая версия")));
    s.w.setUpdateAvailable(false);
    is(QStringLiteral("обновлений нет — строка прячется, кнопка снова «Проверить»"),
       notice->isHidden() && s.checkUpdates->text() == QStringLiteral("Проверить") &&
           s.updatesDetail->text().contains(QStringLiteral("1.8.3")));
}

/** Номер версии из имени пакета — ui/UpdateNotice.hpp. */
static void versionFromAssetName() {
    std::puts("Номер версии из имени пакета");
    using GreenRhythm::Update::versionFromAsset;
    is(QStringLiteral("GreenRhythm-v1.8.4-windows-x64.zip → 1.8.4"),
       versionFromAsset(QStringLiteral("GreenRhythm-v1.8.4-windows-x64.zip")) == QStringLiteral("1.8.4"));
    is(QStringLiteral("GreenRhythm-1.9.0-macos-arm64.dmg → 1.9.0"),
       versionFromAsset(QStringLiteral("GreenRhythm-1.9.0-macos-arm64.dmg")) == QStringLiteral("1.9.0"));
    is(QStringLiteral("GreenRhythm-v2.0-windows-x64.zip → 2.0"),
       versionFromAsset(QStringLiteral("GreenRhythm-v2.0-windows-x64.zip")) == QStringLiteral("2.0"));
    is(QStringLiteral("без номера — пусто, а не «64» из x64"),
       versionFromAsset(QStringLiteral("greenrhythm-windows-x64.zip")).isEmpty() &&
           versionFromAsset(QString()).isEmpty());
    is(QStringLiteral("первая проверка — через минуту, дальше — раз в сутки"),
       GreenRhythm::Update::kFirstCheckMs == 60 * 1000 &&
           GreenRhythm::Update::kCheckEveryMs == 24 * 60 * 60 * 1000);
}

/** Окно спрашивает само и молчит обо всём, кроме найденной версии. */
static void quietUpdateCheckWired() {
    std::puts("Исходник окна: тихая проверка по таймеру");
    const QString window = slurp(QStringLiteral("ui/mainwindow.cpp"));
    const QString grpc = slurp(QStringLiteral("ui/mainwindow_grpc.cpp"));
    const QString header = slurp(QStringLiteral("ui/mainwindow.h"));
    is(QStringLiteral("CheckUpdate умеет молчать: void CheckUpdate(bool quiet = false)"),
       header.contains(QStringLiteral("void CheckUpdate(bool quiet = false);")));
    const int timer = window.indexOf(QStringLiteral("GreenRhythm::Update::kFirstCheckMs"));
    const QString around = timer > 0 ? window.mid(timer - 900, 1400) : QString();
    is(QStringLiteral("окно заводит тихую проверку: минута после запуска, потом раз в сутки"),
       timer > 0 && around.contains(QStringLiteral("GreenRhythm::Update::kCheckEveryMs")) &&
           around.contains(QStringLiteral("CheckUpdate(true)")) && around.contains(QStringLiteral("runOnNewThread")));
    const QString check = grpc.mid(grpc.indexOf(QStringLiteral("void MainWindow::CheckUpdate(bool quiet)")), 2600);
    is(QStringLiteral("тихая проверка не показывает ни «обновлений нет», ни ошибку сети"),
       check.contains(QStringLiteral("if (!quiet) MessageBoxInfo(QObject::tr(\"Update\"), QObject::tr(\"No update\"));")) &&
           check.contains(QStringLiteral("if (quiet) return;")));
    is(QStringLiteral("найденная версия — в колонку, номер из имени пакета"),
       check.contains(QStringLiteral("shell->setUpdateAvailable(true,")) &&
           check.contains(QStringLiteral("GreenRhythm::Update::versionFromAsset(")));
    is(QStringLiteral("ядро ещё не поднято — тихая проверка просто выходит"),
       check.contains(QStringLiteral("NekoGui_rpc::defaultClient == nullptr")));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    shellShowsTheTruth();
    scheduleRule();
    sourcesWiredAndMoved();
    tunnelDialogSpeaksRussian();
    routesDialogWording();
    hiddenFeaturesOnThePage();
    lanAddressPick();
    hiddenFeaturesWired();
    updateNoticeInSidebar();
    versionFromAssetName();
    quietUpdateCheckWired();
    std::printf("\n%d проверок, провалено %d\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
