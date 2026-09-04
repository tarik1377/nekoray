#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace GreenRhythm {

    /**
     * Новая оболочка главного окна: боковая колонка и страницы.
     *
     * ЗАЧЕМ. Окно было устроено как приборная панель инженера: восемь кнопок в
     * ряд, таблица из шести колонок, где «Тип» у всех строк одинаковый, и пустой
     * журнал на пол-экрана. Человек открывал его и первым делом видел чёрное
     * поле журнала, а своё имя сервера — обрезанным между протоколом и адресом.
     *
     * ГЛАВНОЕ РЕШЕНИЕ — НИЧЕГО НЕ ПЕРЕПИСЫВАТЬ, А ПЕРЕСТАВИТЬ. Оболочка не
     * создаёт ни таблицы серверов, ни вкладок журнала: она забирает уже
     * существующие виджеты из mainwindow.ui и раскладывает их по страницам.
     * Поэтому сортировка по колонкам, перетаскивание строк, контекстное меню,
     * поиск, проверка задержки и запуск двойным щелчком продолжают работать: это
     * те же самые объекты, к которым привязан весь прежний код. Перепиши мы их
     * заново — пришлось бы переносить и всю проводку, а это верный способ
     * потерять что-нибудь молча.
     *
     * Оболочка не знает ни о настройках, ни о ядре: принимает значения, отдаёт
     * сигналы. Как и панель, она собирается отдельной целью предпросмотра, и на
     * вёрстку можно смотреть, не запуская клиент.
     */
    class MainShell : public QWidget {
        Q_OBJECT

    public:
        explicit MainShell(QWidget *parent = nullptr);

        /**
         * Отдать оболочке существующие виджеты окна.
         *
         * @param servers таблица серверов вместе с её вкладкой
         * @param logs    нижние вкладки «Журнал» и «Подключение»
         */
        void adopt(QWidget *servers, QWidget *logs);

        /** Состояние: подключено ли, к чему, и задержка (пусто — не измерена). */
        void setConnectionState(bool connected, const QString &server, const QString &latency);

        /** Показать страницу по номеру: 0 подключение, 1 серверы, 2 журнал. */
        void showPage(int index);

        /**
         * Живые числа: сколько соединений идёт через VPN и сколько мимо.
         *
         * Это честный ответ на вопрос «работает ли защита». Раньше он был только
         * на вкладке соединений, куда человек не заглядывает.
         */
        void setLive(int viaVpn, int direct, const QString &down, const QString &up);

        /** Сколько программ выведено из-под защиты поимённо. */
        void setBypassCount(int programs);

        /**
         * Остаток подписки для боковой колонки.
         *
         * @param summary «27 дн. · 84 ГБ»; пусто — блок прячется
         * @param low     остаток на исходе: подпись и кнопка становятся заметнее
         */
        void setSubscription(const QString &summary, bool low);

        /** Состояние подключения, четыре положения вместо двух. */
        enum class State {
            Idle,       ///< не подключено
            Connecting, ///< идёт попытка
            Connected,  ///< работает
            Failed,     ///< не вышло, причина в reason
        };

        /**
         * Показать состояние.
         *
         * ЧЕТЫРЕ, А НЕ ДВА. «Подключаюсь» и «не вышло» человек видел одинаково —
         * как «не подключено», — и не понимал, ждать ему или нажимать снова.
         * Причина отказа выводится строкой: сегодня их было три разных, и каждая
         * требовала своего действия.
         */
        void setState(State state, const QString &reason = QString());

        /** Метки под именем сервера: протокол, транспорт и прочее. */
        void setServerTags(const QStringList &tags);

        /** Показать приглашение вместо пустого списка. */
        void setEmpty(bool empty);

        /** Идёт подключение. Оставлено ради прежних вызовов: это State::Connecting. */
        void setBusy(bool busy);

    signals:
        void connectToggled();
        void addServerRequested();
        void panelRequested();
        void settingsRequested();

        /** Открыть страницу продления. */
        void renewRequested();

        /** Открыть разбор «что-то не работает». */
        void troubleRequested();

        /** Показать список программ, идущих мимо VPN. */
        void bypassListRequested();

        /** Открыть меню «Ещё» — туда переехало содержимое полосы меню. */
        void moreRequested(const QPoint &globalPos);
        void subscriptionRequested();

    private:
        QWidget *buildSidebar();
        QWidget *buildConnectPage();
        void selectPage(int index);

        QStackedWidget *pages = nullptr;
        QList<QPushButton *> navButtons;

        QPushButton *power = nullptr;
        QLabel *powerHint = nullptr;
        QLabel *stateDot = nullptr;
        QLabel *currentTitle = nullptr;
        QLabel *currentMeta = nullptr;
        QWidget *currentCard = nullptr;
        QWidget *subBlock = nullptr;
        QLabel *subSummary = nullptr;
        QPushButton *subButton = nullptr;
        QLabel *liveVpn = nullptr;
        QLabel *liveDirect = nullptr;
        QLabel *liveTraffic = nullptr;
        QPushButton *bypassLine = nullptr;

        State state = State::Idle;
        QWidget *tagRow = nullptr;
        QWidget *emptyHint = nullptr;
    };

} // namespace GreenRhythm
