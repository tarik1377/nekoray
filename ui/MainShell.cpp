#include "ui/MainShell.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace GreenRhythm {

    namespace {

        // Токены темы — в одном месте. Палитрой их не задать: таблица стилей Qt
        // перекрывает QPalette, и цвет, выставленный палитрой, молча не
        // применяется. Значения те же, что в res/theme/feiyangqingyun/qss/modern.css.
        constexpr auto kAccent = "#3fb950";
        constexpr auto kAccentDim = "#2ea043";
        constexpr auto kSurface = "#1e2126";
        constexpr auto kSurfaceUp = "#262a30";
        constexpr auto kText = "#e4e6eb";
        constexpr auto kMuted = "#9aa0a8";
        constexpr auto kLine = "#2f343b";
        constexpr auto kAmber = "#e3a008";
        constexpr auto kRed = "#e5484d";

        /** Строка «подпись слева, значение справа» — для живых чисел в колонке. */
        QWidget *statRow(QWidget *parent, const QString &caption, QLabel **value,
                         const QColor &valueColor) {
            auto *row = new QWidget(parent);
            auto *line = new QHBoxLayout(row);
            line->setContentsMargins(0, 0, 0, 0);
            line->setSpacing(8);

            auto *cap = new QLabel(caption, row);
            cap->setStyleSheet(QStringLiteral("color: #9aa0a8;"));
            QFont cf = cap->font();
            cf.setPointSizeF(cf.pointSizeF() * 0.88);
            cap->setFont(cf);
            line->addWidget(cap, 1);

            *value = new QLabel(QStringLiteral("—"), row);
            QFont vf = (*value)->font();
            vf.setBold(true);
            vf.setPointSizeF(vf.pointSizeF() * 0.9);
            (*value)->setFont(vf);
            (*value)->setStyleSheet(QStringLiteral("color: %1;").arg(valueColor.name()));
            line->addWidget(*value, 0, Qt::AlignRight);
            return row;
        }

        /**
         * Русское склонение после числа: 1 программа, 2 программы, 5 программ.
         *
         * Qt-шный tr() с числом этого не даёт без файла перевода с правилами, а
         * «22 программ» в интерфейсе читается как небрежность — и справедливо.
         */
        QString plural(int n, const QString &one, const QString &few, const QString &many) {
            const int mod100 = n % 100;
            if (mod100 >= 11 && mod100 <= 14) return many;
            switch (n % 10) {
                case 1: return one;
                case 2:
                case 3:
                case 4: return few;
                default: return many;
            }
        }

        QLabel *muted(QWidget *p, const QString &text, double scale = 1.0) {
            auto *l = new QLabel(text, p);
            l->setStyleSheet(QStringLiteral("color: %1;").arg(kMuted));
            if (scale != 1.0) {
                QFont f = l->font();
                f.setPointSizeF(f.pointSizeF() * scale);
                l->setFont(f);
            }
            return l;
        }

        /** Заголовок раздела колонки: мелкий, разрядкой, приглушённый. */
        QLabel *caption(QWidget *p, const QString &text) {
            auto *cap = muted(p, text, 0.8);
            QFont cf = cap->font();
            cf.setBold(true);
            cf.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
            cap->setFont(cf);
            return cap;
        }

        /**
         * Переключатель режима: кнопка с двумя положениями.
         *
         * Не галка: галка в тёмной теме читается плохо, а состояние режима
         * человек должен видеть с расстояния — заливкой, а не крестиком.
         */
        QPushButton *toggle(QWidget *p, const QString &text) {
            auto *b = new QPushButton(text, p);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setMinimumHeight(32);
            // Без жирного во включённом состоянии: жирный шире обычного, фишка
            // прыгала по ширине и резала собственную подпись. Состояние читается
            // по заливке и цвету, этого достаточно.
            b->setStyleSheet(QStringLiteral(
                                 "QPushButton { border: 1px solid %1; border-radius: 8px;"
                                 " color: %2; background: transparent; padding: 0 12px; }"
                                 "QPushButton:hover { border-color: %3; }"
                                 "QPushButton:checked { background: rgba(63,185,80,0.18);"
                                 " border-color: %3; color: %3; }")
                                 .arg(kLine, kMuted, kAccent));
            return b;
        }

        /** Инструмент: маленькая ровная кнопка для сетки. */
        QPushButton *tool(QWidget *p, const QString &text) {
            auto *b = new QPushButton(text, p);
            b->setCursor(Qt::PointingHandCursor);
            b->setMinimumHeight(30);
            QFont f = b->font();
            f.setPointSizeF(f.pointSizeF() * 0.9);
            b->setFont(f);
            return b;
        }

    } // namespace

    MainShell::MainShell(QWidget *parent) : QWidget(parent) {
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);

        row->addWidget(buildSidebar(), 0);

        pages = new QStackedWidget(this);
        pages->setStyleSheet(QStringLiteral("background: %1;").arg(kSurface));
        row->addWidget(pages, 1);

        pages->addWidget(buildConnectPage()); // 0 — подключение
        selectPage(0);
    }

    QWidget *MainShell::buildSidebar() {
        auto *bar = new QWidget(this);
        // 236, а не 216: в колонку вернулись режимы и инструменты, и на прежней
        // ширине «Обновить подписку» резалось до «овить подп». Снимок это
        // показал до того, как увидел бы человек.
        bar->setFixedWidth(236);
        bar->setStyleSheet(QStringLiteral("background: %1; border-right: 1px solid %2;")
                               .arg(kSurfaceUp, kLine));

        auto *box = new QVBoxLayout(bar);
        box->setContentsMargins(16, 16, 16, 14);
        box->setSpacing(6);

        // Шапка: имя и точка состояния. Точка — самый дешёвый способ ответить на
        // вопрос «работает ли оно» до того, как человек начал искать ответ.
        auto *head = new QHBoxLayout();
        auto *title = new QLabel(tr("Зелёный Ритм"), bar);
        QFont big = title->font();
        big.setBold(true);
        big.setPointSizeF(big.pointSizeF() * 1.25);
        title->setFont(big);
        title->setStyleSheet(QStringLiteral("color: %1;").arg(kText));
        head->addWidget(title, 1);

        stateDot = new QLabel(bar);
        stateDot->setFixedSize(10, 10);
        head->addWidget(stateDot, 0, Qt::AlignVCenter);
        box->addLayout(head);
        box->addSpacing(16);

        struct Item {
            QString text;
            QString icon;
            int page;
        };
        const QList<Item> items{
            {tr("Подключение"), QStringLiteral(":/icon/gr-nav-connect.svg"), 0},
            {tr("Серверы"), QStringLiteral(":/icon/gr-nav-servers.svg"), 1},
            {tr("Журнал"), QStringLiteral(":/icon/gr-nav-log.svg"), 2},
        };
        for (const auto &item: items) {
            // Значок рисуется линией и красится currentColor, поэтому он берёт
            // цвет от состояния кнопки: приглушённый у обычной, акцентный у
            // выбранной. Заливкой в 20 точек он слился бы в пятно.
            auto *b = new QPushButton(QIcon(item.icon), QStringLiteral("  ") + item.text, bar);
            b->setIconSize(QSize(19, 19));
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setMinimumHeight(40);
            // Состояния кнопки описаны здесь целиком: выбранный пункт заливается
            // акцентом вполсилы, наведённый — поверхностью. Без этого колонка
            // выглядит списком ссылок, а не навигацией.
            b->setStyleSheet(QStringLiteral(
                                 "QPushButton { text-align: left; padding-left: 14px; border: none;"
                                 " border-radius: 8px; color: %1; background: transparent; }"
                                 "QPushButton:hover { background: %2; }"
                                 "QPushButton:checked { background: rgba(63,185,80,0.16); color: %3;"
                                 " font-weight: bold; }")
                                 .arg(kMuted, kSurface, kAccent));
            const int page = item.page;
            connect(b, &QPushButton::clicked, this, [this, page] { selectPage(page); });
            navButtons += b;
            box->addWidget(b);
        }

        // ИНСТРУМЕНТЫ. То, ради чего раньше лезли в верхний ряд: маршруты,
        // настройки, обновление подписки и проверка обновлений. Режимы здесь
        // не живут: они про подключение и стоят на его странице, под кнопкой,
        // — колонка остаётся навигации и инструментам.
        box->addSpacing(14);
        box->addWidget(caption(bar, tr("ИНСТРУМЕНТЫ")));
        {
            // Короткие подписи — парой, длинные — во всю ширину. Сетка два на
            // два резала «Обновить подписку» пополам; подпись, которую нельзя
            // прочитать, хуже лишней строки.
            auto *grid = new QGridLayout();
            grid->setSpacing(6);
            auto *routes = tool(bar, tr("Маршруты"));
            connect(routes, &QPushButton::clicked, this, &MainShell::routesRequested);
            grid->addWidget(routes, 0, 0);
            auto *settings = tool(bar, tr("Настройки"));
            connect(settings, &QPushButton::clicked, this, &MainShell::settingsRequested);
            grid->addWidget(settings, 0, 1);
            auto *sub = tool(bar, tr("Обновить подписку"));
            connect(sub, &QPushButton::clicked, this, &MainShell::updateSubscriptionRequested);
            grid->addWidget(sub, 1, 0, 1, 2);
            auto *upd = tool(bar, tr("Проверить обновление клиента"));
            connect(upd, &QPushButton::clicked, this, &MainShell::checkUpdateRequested);
            grid->addWidget(upd, 2, 0, 1, 2);
            box->addLayout(grid);
        }

        box->addStretch(1);

        // ЖИВЫЕ ЧИСЛА. Честный ответ на «работает ли защита»: сколько соединений
        // идёт через VPN и сколько мимо. Раньше это было только на вкладке
        // соединений, куда человек не заглядывает, — а вопрос у него возникает
        // ровно тогда, когда что-то не работает.
        {
            auto *live = new QWidget(bar);
            auto *liveBox = new QVBoxLayout(live);
            liveBox->setContentsMargins(0, 0, 0, 12);
            liveBox->setSpacing(5);

            auto *cap = muted(live, tr("СЕЙЧАС"), 0.8);
            QFont cf = cap->font();
            cf.setBold(true);
            cf.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
            cap->setFont(cf);
            liveBox->addWidget(cap);

            liveBox->addWidget(statRow(live, tr("через VPN"), &liveVpn, QColor(kAccent)));
            liveBox->addWidget(statRow(live, tr("напрямую"), &liveDirect, QColor(kText)));
            liveBox->addWidget(statRow(live, tr("трафик"), &liveTraffic, QColor(kMuted)));

            // Отдельной строкой-кнопкой: список программ, выведенных из-под
            // защиты. Сегодня выяснилось, что человек про этот список не помнит,
            // а он решает, работает у него игра или нет.
            bypassLine = new QPushButton(tr("мимо VPN: нет программ"), live);
            bypassLine->setCursor(Qt::PointingHandCursor);
            bypassLine->setFlat(true);
            bypassLine->setStyleSheet(
                QStringLiteral("QPushButton { text-align: left; border: none; padding: 2px 0;"
                               " color: %1; background: transparent; }"
                               "QPushButton:hover { color: %2; }")
                    .arg(kMuted, kAccent));
            QFont bf = bypassLine->font();
            bf.setPointSizeF(bf.pointSizeF() * 0.88);
            bypassLine->setFont(bf);
            connect(bypassLine, &QPushButton::clicked, this, &MainShell::bypassListRequested);
            liveBox->addWidget(bypassLine);

            box->addWidget(live);
        }

        // ПОДПИСКА НА ВИДУ. Остаток клиент считал и раньше, но показывал строкой
        // в самом низу окна, рядом со счётчиками трафика, — там её не искали и не
        // находили. Между тем это единственное, что человеку надо знать про свои
        // деньги, и единственное, из-за чего он однажды останется без связи.
        subBlock = new QWidget(bar);
        auto *subBox = new QVBoxLayout(subBlock);
        subBox->setContentsMargins(0, 0, 0, 10);
        subBox->setSpacing(6);

        auto *subCaption = muted(subBlock, tr("ПОДПИСКА"), 0.8);
        QFont cap = subCaption->font();
        cap.setBold(true);
        cap.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
        subCaption->setFont(cap);
        subBox->addWidget(subCaption);

        subSummary = new QLabel(subBlock);
        QFont sf = subSummary->font();
        sf.setBold(true);
        subSummary->setFont(sf);
        subBox->addWidget(subSummary);

        subButton = new QPushButton(tr("Продлить"), subBlock);
        subButton->setCursor(Qt::PointingHandCursor);
        subButton->setMinimumHeight(32);
        connect(subButton, &QPushButton::clicked, this, &MainShell::renewRequested);
        subBox->addWidget(subButton);

        subBlock->setVisible(false); // покажется, когда будет что показать
        box->addWidget(subBlock);

        // Разбор поломок — кнопкой в колонке, а не пунктом внутри панели.
        // Сегодня выяснилось, что до него доходят через два окна, и человек не
        // доходит: ищет причину сам, полночи.
        auto *trouble = new QPushButton(tr("Что-то не работает"), bar);
        trouble->setCursor(Qt::PointingHandCursor);
        trouble->setMinimumHeight(34);
        connect(trouble, &QPushButton::clicked, this, &MainShell::troubleRequested);
        box->addWidget(trouble);

        // «Ещё» — сюда переехала полоса меню. Прятать её, не дав замены, значило
        // бы отнять у опытных всё: маршрутизацию, горячие клавиши, папку
        // настроек. Кнопка отдаёт те же самые меню, ничего не переписывая.
        // С «Панелью» — в один ряд: три кнопки во всю ширину друг под другом
        // не оставляли колонке воздуха на окне в 720 точек высотой.
        {
            auto *row = new QHBoxLayout();
            row->setSpacing(6);
            auto *more = new QPushButton(tr("Ещё"), bar);
            more->setCursor(Qt::PointingHandCursor);
            more->setMinimumHeight(34);
            connect(more, &QPushButton::clicked, this, [this, more] {
                emit moreRequested(more->mapToGlobal(QPoint(more->width(), 0)));
            });
            row->addWidget(more, 1);

            auto *panel = new QPushButton(tr("Панель"), bar);
            panel->setCursor(Qt::PointingHandCursor);
            panel->setMinimumHeight(34);
            connect(panel, &QPushButton::clicked, this, &MainShell::panelRequested);
            row->addWidget(panel, 1);
            box->addLayout(row);
        }

        auto *add = new QPushButton(tr("+  Добавить сервер"), bar);
        add->setCursor(Qt::PointingHandCursor);
        add->setMinimumHeight(42);
        add->setStyleSheet(QStringLiteral(
                               "QPushButton { background: %1; color: #08170c; border: none;"
                               " border-radius: 10px; font-weight: bold; }"
                               "QPushButton:hover { background: %2; }")
                               .arg(kAccent, kAccentDim));
        connect(add, &QPushButton::clicked, this, &MainShell::addServerRequested);
        box->addWidget(add);

        return bar;
    }

    QWidget *MainShell::buildConnectPage() {
        auto *page = new QWidget(this);
        auto *box = new QVBoxLayout(page);
        box->setContentsMargins(40, 40, 40, 40);
        box->setSpacing(0);
        box->addStretch(1);

        // Кнопка — главный предмет на экране, и она обязана быть крупной. Прежде
        // подключение включалось галкой «Режим TUN» в углу панели инструментов:
        // человек не находил её и не понимал, включено у него что-нибудь или нет.
        power = new QPushButton(QStringLiteral("⏻"), page);
        // Имя нужно ради селектора по имени: у темы есть своё правило для
        // QPushButton, и при равной точности выигрывает не наше. Круглая кнопка
        // от этого получалась квадратной — скругление просто не применялось.
        power->setObjectName(QStringLiteral("grPower"));
        power->setFixedSize(168, 168);
        power->setCursor(Qt::PointingHandCursor);
        QFont glyph = power->font();
        glyph.setPointSize(52);
        power->setFont(glyph);
        connect(power, &QPushButton::clicked, this, &MainShell::connectToggled);
        box->addWidget(power, 0, Qt::AlignHCenter);
        box->addSpacing(20);

        powerHint = muted(page, tr("Нажмите для подключения"), 1.15);
        powerHint->setAlignment(Qt::AlignHCenter);
        box->addWidget(powerHint);
        box->addSpacing(22);

        // РЕЖИМ — ЗДЕСЬ, ПОД КНОПКОЙ, А НЕ В КОЛОНКЕ. Режим — это свойство
        // подключения, и место ему рядом с тем, что он меняет. Слева
        // переключатель из двух положений одним куском, справа два дополнения
        // отдельными фишками: они не исключают друг друга.
        {
            auto *strip = new QWidget(page);
            auto *row = new QHBoxLayout(strip);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(10);

            auto *seg = new QWidget(strip);
            seg->setObjectName(QStringLiteral("grSeg"));
            seg->setStyleSheet(QStringLiteral(
                                   "QWidget#grSeg { background: %1; border: 1px solid %2; border-radius: 11px; }")
                                   .arg(kSurfaceUp, kLine));
            auto *segBox = new QHBoxLayout(seg);
            segBox->setContentsMargins(3, 3, 3, 3);
            segBox->setSpacing(2);
            const QString segStyle = QStringLiteral(
                "QPushButton { border: none; border-radius: 8px; padding: 6px 16px;"
                " color: %1; background: transparent; }"
                "QPushButton:hover { color: %2; }"
                "QPushButton:checked { background: %3; color: #08170c; }")
                .arg(kMuted, kText, kAccent);

            modeTun = new QPushButton(tr("Туннель"), seg);
            modeTun->setCheckable(true);
            modeTun->setCursor(Qt::PointingHandCursor);
            modeTun->setStyleSheet(segStyle);
            modeTun->setToolTip(tr("Весь трафик системы идёт через клиент (TUN). Нужны права администратора."));
            connect(modeTun, &QPushButton::clicked, this, &MainShell::tunToggled);
            segBox->addWidget(modeTun);

            modeProxy = new QPushButton(tr("Системный прокси"), seg);
            modeProxy->setCheckable(true);
            modeProxy->setCursor(Qt::PointingHandCursor);
            modeProxy->setStyleSheet(segStyle);
            modeProxy->setToolTip(tr("Только программы, которые уважают системный прокси: браузеры и большинство мессенджеров."));
            connect(modeProxy, &QPushButton::clicked, this, &MainShell::systemProxyToggled);
            segBox->addWidget(modeProxy);
            row->addWidget(seg);

            gamesToggle = toggle(strip, tr("Игры через VPN"));
            gamesToggle->setToolTip(
                tr("Обычно игры идут мимо туннеля: пинг ниже, адрес российский, античит спокоен.\n"
               "Включите, если серверы игры фильтруются у провайдера и мимо туннеля она не работает."));
            connect(gamesToggle, &QPushButton::clicked, this, &MainShell::gamesViaTunnelToggled);
            row->addWidget(gamesToggle);

            dpiToggle = toggle(strip, tr("Обход фильтрации"));
            dpiToggle->setToolTip(
                tr("Дробит приветствие TLS у того, что идёт мимо туннеля, — игры и античиты остаются на своём адресе,\n"
               "а фильтр провайдера не видит имени сервера. Без драйвера, внутри ядра. Против простых фильтров; если не помогло — скажите."));
            connect(dpiToggle, &QPushButton::clicked, this, &MainShell::dpiFragmentToggled);
            row->addWidget(dpiToggle);

            // ВТОРОЙ ЯРУС — ОТДЕЛЬНОЙ ФИШКОЙ, А НЕ ГАЛКОЙ В НАСТРОЙКАХ. Он
            // качает чужую программу и поднимает системный драйвер; такое
            // решение человек должен принимать глядя на него, а не найдя
            // случайно. Рядом — строка состояния: см. setDpiModule.
            dpiModuleToggle = toggle(strip, tr("Усиленный обход"));
            dpiModuleToggle->setToolTip(
                tr("Второй ярус на случай, когда дробления в ядре не хватает: отдельная программа winws\n"
                   "(проект zapret, лицензия MIT) и системный драйвер. Скачивается по запросу.\n\n"
                   "Пока запущена игра с античитом, обход не включается и гаснет сам — бан отменить нельзя."));
            connect(dpiModuleToggle, &QPushButton::clicked, this, &MainShell::dpiModuleToggled);
            row->addWidget(dpiModuleToggle);

            box->addWidget(strip, 0, Qt::AlignHCenter);

            // Строка состояния модуля: под полосой режимов, по центру, тем же
            // приглушённым цветом. Пустая — скрыта, чтобы не занимать место у
            // тех, кто про этот ярус вообще не спрашивал.
            dpiModuleState = muted(page, QString(), 0.9);
            dpiModuleState->setAlignment(Qt::AlignCenter);
            dpiModuleState->setWordWrap(true);
            dpiModuleState->hide();
            box->addSpacing(6);
            box->addWidget(dpiModuleState, 0, Qt::AlignHCenter);
        }
        box->addSpacing(26);

        auto *caption = muted(page, tr("ТЕКУЩИЙ СЕРВЕР"), 0.85);
        QFont cf = caption->font();
        cf.setBold(true);
        cf.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        caption->setFont(cf);
        box->addWidget(caption, 0, Qt::AlignHCenter);
        box->addSpacing(8);

        // Карточка вместо строки таблицы: имя крупно, подробности мелко и серым.
        // Протокол и адрес человеку не нужны, но нужны поддержке — поэтому они
        // не убраны совсем, а уведены в подпись.
        currentCard = new QWidget(page);
        currentCard->setFixedWidth(460);
        currentCard->setStyleSheet(QStringLiteral(
                                       "background: %1; border: 1px solid %2; border-radius: 12px;")
                                       .arg(kSurfaceUp, kLine));
        auto *cardBox = new QVBoxLayout(currentCard);
        cardBox->setContentsMargins(18, 14, 18, 14);
        cardBox->setSpacing(4);

        currentTitle = new QLabel(tr("Сервер не выбран"), currentCard);
        QFont nameFont = currentTitle->font();
        nameFont.setBold(true);
        nameFont.setPointSizeF(nameFont.pointSizeF() * 1.15);
        currentTitle->setFont(nameFont);
        currentTitle->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(kText));
        cardBox->addWidget(currentTitle);

        currentMeta = muted(currentCard, tr("Выберите его на вкладке «Серверы»"), 0.9);
        currentMeta->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(kMuted));
        cardBox->addWidget(currentMeta);

        // Метки протокола под именем: «VLESS · TCP · REALITY». Ряд создаётся
        // пустым и прячется, пока меток нет, — пустая полоса под именем читается
        // как недогруженное окно.
        tagRow = new QWidget(currentCard);
        auto *tagBox = new QHBoxLayout(tagRow);
        tagBox->setContentsMargins(0, 6, 0, 0);
        tagBox->setSpacing(6);
        // Рамка карточки распространяется на детей — ряду меток она не нужна.
        tagRow->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        tagRow->setVisible(false);
        cardBox->addWidget(tagRow);

        box->addWidget(currentCard, 0, Qt::AlignHCenter);

        // ВЫБОР СЕРВЕРА — ЗДЕСЬ ЖЕ, ПОД КНОПКОЙ. Страница стояла пустой, пока не
        // сходишь на «Серверы» и не нажмёшь там; место под кнопкой при этом
        // пустовало. Первая строка — автовыбор: подключит самый быстрый.
        box->addSpacing(10);
        serverPick = new QComboBox(page);
        serverPick->setFixedWidth(460);
        serverPick->setMinimumHeight(36);
        serverPick->setCursor(Qt::PointingHandCursor);
        // Стрелка рисуется своим знаком: тема прячет штатную, и поле выбора
        // читалось как обычная надпись, которую незачем нажимать.
        serverPick->setStyleSheet(QStringLiteral(
                                      "QComboBox { background: %1; color: %2; border: 1px solid %3;"
                                      " border-radius: 10px; padding: 4px 14px; }"
                                      "QComboBox:hover { border-color: %4; }"
                                      "QComboBox::drop-down { border: none; width: 28px; }"
                                      "QComboBox::down-arrow { image: none; border-left: 5px solid transparent;"
                                      " border-right: 5px solid transparent; border-top: 6px solid %4;"
                                      " width: 0; height: 0; margin-right: 10px; }"
                                      "QComboBox QAbstractItemView { background: %1; color: %2;"
                                      " selection-background-color: rgba(63,185,80,0.18); border: 1px solid %3; }")
                                      .arg(kSurfaceUp, kText, kLine, kAccent));
        serverPick->addItem(tr("Автовыбор — самый быстрый"), -1);
        // activated, а не currentIndexChanged: второе срабатывает и от нашей же
        // перестройки списка, и тогда выбор «менялся» сам, без человека.
        connect(serverPick, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            emit serverChosen(serverPick->itemData(index).toInt());
        });
        box->addWidget(serverPick, 0, Qt::AlignHCenter);

        // ПУСТОЙ СПИСОК — НЕ ПУСТОЙ ЭКРАН. Раньше человек, у которого ещё нет
        // серверов, видел просто пустоту и не понимал, чего от него хотят.
        emptyHint = new QWidget(page);
        auto *emptyBox = new QVBoxLayout(emptyHint);
        emptyBox->setContentsMargins(0, 0, 0, 0);
        emptyBox->setSpacing(6);
        auto *emptyTitle = new QLabel(tr("Серверов пока нет"), emptyHint);
        QFont ef = emptyTitle->font();
        ef.setBold(true);
        ef.setPointSizeF(ef.pointSizeF() * 1.1);
        emptyTitle->setFont(ef);
        emptyTitle->setAlignment(Qt::AlignHCenter);
        emptyBox->addWidget(emptyTitle);
        auto *emptyWhat = muted(emptyHint,
                                tr("Вставьте ссылку подписки — профили соберутся сами."), 0.95);
        emptyWhat->setAlignment(Qt::AlignHCenter);
        emptyBox->addWidget(emptyWhat);
        emptyHint->setVisible(false);
        box->addWidget(emptyHint, 0, Qt::AlignHCenter);

        box->addStretch(2);
        return page;
    }

    void MainShell::adopt(QWidget *servers, QWidget *logs) {
        // Виджеты переезжают, а не создаются заново: к ним привязана вся прежняя
        // проводка окна. addWidget сам меняет родителя.
        if (servers != nullptr) pages->insertWidget(1, servers);
        if (logs != nullptr) pages->insertWidget(2, logs);
        selectPage(0);
    }

    void MainShell::showPage(int index) { selectPage(index); }

    void MainShell::selectPage(int index) {
        if (index < 0 || index >= pages->count()) return;
        pages->setCurrentIndex(index);
        for (int i = 0; i < navButtons.size(); i++) navButtons[i]->setChecked(i == index);
    }

    void MainShell::setConnectionState(bool isConnected, const QString &server,
                                       const QString &latency) {
        connected = isConnected;
        if (isConnected && !server.isEmpty()) {
            currentTitle->setText(server);
            currentMeta->setText(latency.isEmpty() ? tr("задержка не измерена")
                                                   : tr("задержка %1").arg(latency));
        } else if (!isConnected) {
            // Не подключено — карточка отвечает на вопрос «а кого подключит»,
            // вместо того чтобы отправлять на другую вкладку.
            if (idleServerName.isEmpty()) {
                currentTitle->setText(tr("Сервер не выбран"));
                currentMeta->setText(tr("Выберите ниже или на вкладке «Серверы»"));
            } else {
                currentTitle->setText(idleServerName);
                currentMeta->setText(tr("подключится при нажатии"));
            }
        }
        setState(isConnected ? State::Connected : State::Idle);
    }

    void MainShell::setIdleServer(const QString &name) {
        idleServerName = name;
        if (!connected) setConnectionState(false, QString(), QString());
    }

    void MainShell::setServers(const QList<ServerItem> &servers, int currentId) {
        if (serverPick == nullptr) return;
        QStringList signature;
        for (const auto &s: servers) {
            signature << QString::number(s.id) + QLatin1Char('|') + s.name + QLatin1Char('|') + s.latency;
        }
        if (signature != serverSignature) {
            serverSignature = signature;
            serverPick->blockSignals(true);
            serverPick->clear();
            serverPick->addItem(tr("Автовыбор — самый быстрый"), -1);
            for (const auto &s: servers) {
                const QString text = s.latency.isEmpty()
                                         ? s.name
                                         : QStringLiteral("%1  ·  %2").arg(s.name, s.latency);
                serverPick->addItem(text, s.id);
            }
            serverPick->blockSignals(false);
        }
        const int at = serverPick->findData(currentId);
        serverPick->blockSignals(true);
        serverPick->setCurrentIndex(at >= 0 ? at : 0);
        serverPick->blockSignals(false);
    }

    void MainShell::setModes(bool tun, bool systemProxy, bool gamesViaTunnel, bool dpiFragment) {
        // setChecked не шлёт clicked, поэтому обратной волны сигналов нет.
        if (modeTun != nullptr) modeTun->setChecked(tun);
        if (modeProxy != nullptr) modeProxy->setChecked(systemProxy);
        if (gamesToggle != nullptr) gamesToggle->setChecked(gamesViaTunnel);
        if (dpiToggle != nullptr) dpiToggle->setChecked(dpiFragment);
    }

    void MainShell::setDpiModule(bool enabled, bool running, const QString &text) {
        if (dpiModuleToggle != nullptr) dpiModuleToggle->setChecked(enabled);
        if (dpiModuleState == nullptr) return;
        // Строка нужна ровно тогда, когда включено и НЕ работает: это и есть
        // расхождение между желанием и действительностью. Когда работает —
        // фишка уже всё сказала; когда выключено — говорить нечего.
        const bool show = enabled && !running && !text.isEmpty();
        dpiModuleState->setText(show ? tr("Усиленный обход: %1").arg(text) : QString());
        dpiModuleState->setVisible(show);
    }

    void MainShell::setState(State next, const QString &reason) {
        state = next;

        // Цвет, подпись и доступность кнопки идут одним набором: разойдись они —
        // и человек увидит зелёный круг с надписью «не вышло».
        QString ring = kMuted, fill = kSurfaceUp, glyph = kMuted, hint;
        bool enabled = true;
        switch (next) {
            case State::Idle:
                hint = tr("Нажмите для подключения");
                break;
            case State::Connecting:
                ring = fill = kAmber;
                fill = QStringLiteral("rgba(227,160,8,0.12)");
                glyph = kAmber;
                hint = tr("Подключаюсь…");
                enabled = false;
                break;
            case State::Connected:
                ring = glyph = kAccent;
                fill = QStringLiteral("rgba(63,185,80,0.12)");
                hint = tr("Подключено — нажмите, чтобы отключить");
                break;
            case State::Failed:
                ring = glyph = kRed;
                fill = QStringLiteral("rgba(229,72,77,0.10)");
                // Причина обязательна: «не удалось» без неё не говорит, что
                // делать, а делать надо разное — ждать, сменить сервер, продлить.
                hint = reason.isEmpty() ? tr("Не удалось подключиться")
                                        : tr("Не удалось подключиться: %1").arg(reason);
                break;
        }

        stateDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 5px;")
                                    .arg(next == State::Connected ? QString(kAccent)
                                                                  : QString(kLine)));
        power->setEnabled(enabled);
        power->setStyleSheet(
            QStringLiteral("QPushButton#grPower { min-width: 168px; max-width: 168px;"
                           " min-height: 168px; max-height: 168px; padding: 0;"
                           " border-radius: 84px; color: %1; background: %2;"
                           " border: 3px solid %3; }"
                           "QPushButton#grPower:hover { border-color: %4; }")
                .arg(glyph, fill, ring, next == State::Connected ? QString(kAccent)
                                                                 : QString(kAccent)));
        powerHint->setText(hint);
        powerHint->setStyleSheet(
            QStringLiteral("color: %1;").arg(next == State::Failed ? QString(kRed)
                                                                   : QString(kMuted)));
    }

    void MainShell::setServerTags(const QStringList &tags) {
        if (tagRow == nullptr) return;
        // Метки — то немногое из технических подробностей, что человеку не мешает:
        // они мелкие, серые и стоят под именем, а не вместо него.
        auto *box = qobject_cast<QHBoxLayout *>(tagRow->layout());
        if (box == nullptr) return;
        while (auto *item = box->takeAt(0)) {
            if (auto *w = item->widget()) w->deleteLater();
            delete item;
        }
        for (const auto &tag: tags) {
            auto *chip = new QLabel(tag.toUpper(), tagRow);
            QFont f = chip->font();
            f.setPointSizeF(f.pointSizeF() * 0.78);
            f.setBold(true);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
            chip->setFont(f);
            chip->setStyleSheet(QStringLiteral(
                                    "color: %1; background: rgba(63,185,80,0.14);"
                                    " border: none; border-radius: 6px; padding: 2px 7px;")
                                    .arg(kAccent));
            box->addWidget(chip);
        }
        box->addStretch(1);
        tagRow->setVisible(!tags.isEmpty());
    }

    void MainShell::setEmpty(bool empty) {
        if (emptyHint != nullptr) emptyHint->setVisible(empty);
        if (currentCard != nullptr) currentCard->setVisible(!empty);
    }
    void MainShell::setLive(int viaVpn, int direct, const QString &down, const QString &up) {
        if (liveVpn == nullptr) return;
        liveVpn->setText(QString::number(viaVpn));
        liveDirect->setText(QString::number(direct));
        liveTraffic->setText(down.isEmpty() && up.isEmpty()
                                 ? QStringLiteral("—")
                                 : QStringLiteral("↓ %1  ↑ %2").arg(down, up));
    }

    void MainShell::setBypassCount(int programs) {
        if (bypassLine == nullptr) return;
        // Число, а не список: список длинный, а человеку нужно понять «есть ли
        // вообще исключения» и нажать, если есть.
        bypassLine->setText(
            programs > 0 ? tr("мимо VPN: %1 %2")
                               .arg(programs)
                               .arg(plural(programs, tr("программа"), tr("программы"),
                                           tr("программ")))
                         : tr("мимо VPN: нет программ"));
    }

    void MainShell::setSubscription(const QString &summary, bool low) {
        if (subBlock == nullptr) return;
        // Пусто — блока нет вовсе. «Подписка: —» занимала бы место сообщением,
        // которое ничего не сообщает.
        subBlock->setVisible(!summary.isEmpty());
        if (summary.isEmpty()) return;

        subSummary->setText(summary);
        subSummary->setStyleSheet(
            QStringLiteral("color: %1;").arg(low ? QString(kAmber) : QString(kText)));
        // На исходе кнопка становится главной: лучше поторопить, чем дать
        // остаться без связи посреди дня.
        subButton->setText(low ? tr("Продлить сейчас") : tr("Продлить"));
        subButton->setStyleSheet(
            low ? QStringLiteral("QPushButton { background: %1; color: #1a1200;"
                                 " border: none; border-radius: 8px; font-weight: bold; }")
                      .arg(kAmber)
                : QString());
    }

    void MainShell::setBusy(bool isBusy) {
        // Оставлено ради прежних вызовов: занятость — это состояние «подключаюсь».
        if (isBusy) setState(State::Connecting);
    }

} // namespace GreenRhythm
