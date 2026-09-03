#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QVBoxLayout;

/**
 * Панель «Зелёный Ритм» — наши функции в одном месте и со словами.
 *
 * ЗАЧЕМ ОНА. Всё наше лежало в подменю второго уровня: Программа → Зелёный Ритм
 * → десять пунктов подряд, без единого пояснения, что каждый делает. Кнопка
 * «Панель» в панели инструментов при этом открывает ЧУЖОЙ дашборд Clash — то
 * есть слово «панель» у нас уже занято не нами.
 *
 * Цена такой раскладки видна на живом случае: у владельца перестал работать
 * Squad, и он полдня думал, что сломаны маршруты. Средство лечения — увести игру
 * мимо туннеля — лежало в настройках TUN под английским заголовком «Bypass
 * Process Name», без слова о том, что игры вписывают именно туда. Догадаться
 * нельзя, поэтому человек и не догадался.
 *
 * НИ ОДНОГО ОБРАЩЕНИЯ К НАСТРОЙКАМ И К ГЛАВНОМУ ОКНУ. Панель принимает значения
 * и отдаёт сигналы, а хранит и исполняет главное окно. Это не чистоплюйство:
 * так её собирает отдельная цель предпросмотра, и на вёрстку можно посмотреть,
 * не запуская второй клиент поверх рабочего туннеля.
 */
class DialogGreenRhythm : public QDialog {
    Q_OBJECT

public:
    explicit DialogGreenRhythm(QWidget *parent = nullptr);

    /** Список программ, идущих мимо туннеля, по одной в строке. */
    void setBypassList(const QString &text);
    QString bypassList() const;

    void setAutopilot(bool on);

    /** Состояние в шапке: подключено ли и куда. */
    void setConnectionState(bool connected, const QString &where);

signals:
    void connectRequested();
    void relayRequested();
    void qrRequested();
    /** Разобрать, почему не работает конкретная программа. */
    void troubleRequested();

    void diagnosticsRequested();
    void buyRequested();
    void telegramRequested();
    void fixNetRequested();
    void adaptersRequested();
    void autopilotChanged(bool on);

    /** Список изменён и подтверждён человеком. Сохраняет главное окно. */
    void bypassChanged(const QString &text);

private:
    void buildHeader(QVBoxLayout *page);
    void buildConnection(QVBoxLayout *page);
    void buildGames(QVBoxLayout *page);
    void buildSubscription(QVBoxLayout *page);
    void buildHelp(QVBoxLayout *page);
    void buildButtons(QVBoxLayout *page);

    /**
     * Показать ЗАПУЩЕННЫЕ программы и дать отметить нужные.
     *
     * Готового списка игр здесь нет намеренно. Имя исполняемого файла у каждой
     * игры своё, угадать его неоткуда, а неверное имя не совпадёт ни с чем и
     * молча не уведёт ничего — человек будет считать, что игра идёт напрямую,
     * пока она идёт кругом. Ровно так и вышло со Squad: очевидное «SquadGame.exe»
     * оказалось «SquadGame-Win64-Shipping.exe», и настроенный на вид обход не
     * делал ничего. На машине же лежит правда — то, что запущено прямо сейчас.
     */
    void pickFromRunning();

    void addLine(const QString &name);

    QLabel *state = nullptr;
    QLabel *stateWhere = nullptr;
    QPlainTextEdit *bypass = nullptr;
    QCheckBox *autopilotBox = nullptr;
};
