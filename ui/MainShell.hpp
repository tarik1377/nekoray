#pragma once

#include <QWidget>

class QComboBox;
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

        /** Одна строка выбора сервера под кнопкой. */
        struct ServerItem {
            int id = -1;
            QString name;
            QString latency; ///< «32 мс»; пусто — не измерена
        };

        /**
         * Список серверов для выбора прямо под кнопкой подключения.
         *
         * Раньше сервер выбирался только на своей вкладке, а страница
         * подключения стояла пустой, пока туда не сходишь. Место под кнопкой
         * есть, и вопрос «к чему подключаюсь» решается там же, где нажимают.
         * Перестраивается только при смене состава — иначе раскрытый список
         * захлопывался бы каждые две секунды.
         *
         * @param currentId выбранный сейчас; -1 — автовыбор самого быстрого
         */
        void setServers(const QList<ServerItem> &servers, int currentId);

        /**
         * Режимы: туннель, системный прокси, игры через туннель.
         *
         * Переключатели живут в колонке, потому что раньше жили в ряду кнопок
         * сверху, а ряд ушёл вместе с прежней вёрсткой. Человек искал их и не
         * находил — ровно тот вопрос, с которого и начался этот блок.
         */
        void setModes(bool tun, bool systemProxy, bool gamesViaTunnel, bool dpiFragment);

        /** Кого подключит кнопка, пока не подключено. Пусто — «Сервер не выбран». */
        void setIdleServer(const QString &name);

    signals:
        void connectToggled();

        /** Переключатели режима — человек нажал сам. */
        void tunToggled(bool on);
        void systemProxyToggled(bool on);
        void gamesViaTunnelToggled(bool on);
        void dpiFragmentToggled(bool on);

        /** Инструменты, переехавшие из верхнего ряда. */
        void routesRequested();
        void updateSubscriptionRequested();
        void checkUpdateRequested();

        /** Выбор сервера под кнопкой. -1 — автовыбор. */
        void serverChosen(int id);
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

        QPushButton *modeTun = nullptr;
        QPushButton *modeProxy = nullptr;
        QPushButton *gamesToggle = nullptr;
        QPushButton *dpiToggle = nullptr;
        QComboBox *serverPick = nullptr;
        QStringList serverSignature; ///< состав списка, чтобы не перестраивать зря
        QString idleServerName;
        bool connected = false;
    };

} // namespace GreenRhythm
