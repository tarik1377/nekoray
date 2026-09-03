#pragma once

#include "main/ProgramTrouble.hpp"

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

/**
 * «Что-то не работает» — разбор без единого конфига.
 *
 * ЗАЧЕМ. Сегодняшний случай чинился знанием, которого у клиента нет и быть не
 * может: настоящее имя процесса игры, смысл «увести мимо туннеля» и порядок
 * правил. Здесь ничего этого не спрашивают: человек называет программу, клиент
 * смотрит, что она делает НА САМОМ ДЕЛЕ, и предлагает одну кнопку.
 *
 * ИМЯ НЕ СПРАШИВАЮТ — ПОДСМАТРИВАЮТ. Угадать «SquadGame-Win64-Shipping.exe»
 * нельзя, а увидеть — можно: ядро сообщает, под каким именем видит программу
 * оно само. Это снимает целый класс ошибок, где неверное имя не совпадает ни с
 * чем и молча не делает ничего.
 *
 * НАБЛЮДЕНИЕ ФОНОВОЕ. Окно можно свернуть и уйти в игру: полноэкранная игра
 * закрывает собой любой отсчёт, и модальное «подождите 20 секунд» здесь просто
 * не работает. Приговор дожидается человека.
 *
 * ЧЕГО ЭТО ОКНО НЕ УМЕЕТ, и это сказано вслух в самом окне:
 *  — видеть запрещённые соединения: в ядре запрет срабатывает раньше учёта;
 *  — отличить «пинг не проходит» по конкретной программе: у проверок связи нет
 *    ни порта, ни владельца, они чинятся сразу для всех;
 *  — заглянуть внутрь игры. Поэтому итог звучит «теперь проходит, попробуйте»,
 *    а никогда не «починено».
 */
class DialogWhatBroke : public QDialog {
    Q_OBJECT

public:
    explicit DialogWhatBroke(QWidget *parent = nullptr);

    /** Очередной ответ ядра. Зовёт главное окно на каждом опросе. */
    void feed(const QList<GreenRhythm::Seen> &batch);

    /** Имя наблюдаемой программы; пусто, когда наблюдение не идёт. */
    QString watching() const { return program; }

    /**
     * Начать разбор названной программы, минуя выбор.
     *
     * Нужно и по делу — разбор можно будет начать прямо из таблицы соединений,
     * ткнув в строку, — и для предпросмотра вёрстки без запуска клиента.
     */
    void inspect(const QString &program);

    /** Подвести итог по тому, что успели увидеть. */
    void conclude();

    /** Список программ, уже идущих мимо туннеля, — чтобы не чинить починенное. */
    void setAlreadyDirect(const QStringList &names) { alreadyDirect = names; }

signals:
    /** Увести программу мимо туннеля. Сохраняет главное окно. */
    void fixRequested(const QString &program);

    /** Переподключиться, чтобы правка вступила в силу. */
    void reconnectRequested();

private:
    void buildPick();
    void buildWatch();
    void buildVerdict();

    void stopWatch();

    QStackedWidget *pages = nullptr;

    QListWidget *programs = nullptr;
    QPushButton *begin = nullptr;

    QLabel *watchTitle = nullptr;
    QLabel *watchCount = nullptr;

    QLabel *verdictTitle = nullptr;
    QLabel *verdictBody = nullptr;
    QPushButton *fixButton = nullptr;

    GreenRhythm::Watch watch;
    QString program;
    QStringList alreadyDirect;
    bool applied = false;
};
