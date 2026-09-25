#include "ui/MainShell.hpp"
#include "ui/Icons.hpp"
#include "ui/Palette.hpp"
#include "ui/SubscriptionSchedule.hpp"

#include <QGridLayout>
#include <QPainter>
#include <QRadialGradient>

#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QScrollArea>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace GreenRhythm {

    namespace {

        // Токены темы — из ui/Palette.hpp, единственной копии для кода (вторая —
        // modern.css). Здесь только короткие имена, которыми пишет оболочка.
        constexpr auto kAccent = Palette::kAccent;
        constexpr auto kAccentDim = Palette::kAccentDim;
        constexpr auto kSurface = Palette::kSurface;
        constexpr auto kSurfaceUp = Palette::kSurfaceUp;
        constexpr auto kSidebar = Palette::kSidebar;
        constexpr auto kText = Palette::kText;
        constexpr auto kMuted = Palette::kMuted;
        constexpr auto kLine = Palette::kLine;
        constexpr auto kAmber = Palette::kAmber;
        constexpr auto kRed = Palette::kRed;


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
        class ModeSwitch final : public QPushButton {
        public:
            explicit ModeSwitch(QWidget *parent) : QPushButton(parent) {
                setCheckable(true);
                setCursor(Qt::PointingHandCursor);
                setFocusPolicy(Qt::StrongFocus);
                setStyleSheet("min-width: 52px; max-width: 52px; min-height: 30px; max-height: 30px; padding: 0; border: none;");
                setFixedSize(52, 30);
            }
        protected:
            void paintEvent(QPaintEvent *) override {
                QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
                const QRectF track(2, 3, width() - 4, height() - 6);
                QColor fill(isChecked() ? kAccent : kLine);
                if (!isEnabled()) fill.setAlpha(100);
                painter.setPen(Qt::NoPen); painter.setBrush(fill); painter.drawRoundedRect(track, 12, 12);
                painter.setBrush(QColor(isChecked() ? kSidebar : kText));
                const qreal x = isChecked() ? width() - 16 : 16;
                painter.drawEllipse(QPointF(x, height() / 2.0), isDown() ? 8.0 : 9.0, 9.0);
                if (hasFocus()) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(QColor(kAccent), 1)); painter.drawRoundedRect(QRectF(0.5, 0.5, width()-1, height()-1), 14, 14); }
            }
        };

        QPushButton *toggle(QWidget *parent, const QString &text) {
            auto *button = new ModeSwitch(parent);
            button->setAccessibleName(text);
            return button;
        }

        /**
         * Ссылка-кнопка: текст без заливки и рамки. Для действий второго ряда,
         * которые должны быть под рукой, но не должны выглядеть кнопкой:
         * «мимо VPN: 22 программы», «Что-то не работает», «Продлить».
         */
        QString linkStyle(const char *color, bool bold = false) {
            return QStringLiteral("QPushButton { border: none; background: transparent; padding: 2px 4px;"
                                  " color: %1; %2 }"
                                  "QPushButton:hover { color: %3; }")
                .arg(QString::fromLatin1(color), bold ? QStringLiteral("font-weight: 600;") : QString(),
                     QString::fromLatin1(kAccent));
        }

        /**
         * Плитка с числом: значение крупно, подпись мелко и серым. Три такие в
         * ряд под сервером — это «работает ли защита» с одного взгляда.
         */
        QWidget *tile(QWidget *p, const QString &caption, QLabel **value, const char *valueColor, double scale) {
            auto *card = new QWidget(p);
            card->setObjectName(QStringLiteral("grTile"));
            card->setStyleSheet(QStringLiteral("QWidget#grTile { background: transparent; border-radius: 12px; }"));
            auto *v = new QVBoxLayout(card);
            v->setContentsMargins(14, 10, 14, 10);
            v->setSpacing(2);
            *value = new QLabel(QStringLiteral("—"), card);
            QFont vf = (*value)->font();
            vf.setBold(true);
            vf.setPointSizeF(vf.pointSizeF() * scale);
            (*value)->setFont(vf);
            (*value)->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                        .arg(QString::fromLatin1(valueColor)));
            v->addWidget(*value);
            auto *c = new QLabel(caption, card);
            QFont cf = c->font();
            cf.setPointSizeF(cf.pointSizeF() * 0.85);
            c->setFont(cf);
            c->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(QString::fromLatin1(kMuted)));
            v->addWidget(c);
            return card;
        }

        /**
         * Инструмент: строка списка с подписью слева.
         *
         * Плоская, без рамки — тот же язык, что у пунктов навигации, но без
         * значка и на тон тише: инструменты нужны реже, чем страницы, и не
         * должны спорить с ними за внимание.
         */
        QPushButton *tool(QWidget *p, const QString &text) {
            auto *b = new QPushButton(text, p);
            b->setCursor(Qt::PointingHandCursor);
            b->setMinimumHeight(27);
            QFont f = b->font();
            f.setPointSizeF(f.pointSizeF() * 0.92);
            b->setFont(f);
            b->setStyleSheet(QStringLiteral(
                                 "QPushButton { text-align: left; padding-left: 14px; border: none;"
                                 " border-radius: 7px; color: %1; background: transparent; }"
                                 "QPushButton:hover { background: %2; color: %3; }")
                                 .arg(kMuted, kSurfaceUp, kText));
            return b;
        }

    } // namespace

    /**
     * Свечение под кнопкой питания.
     *
     * Плоское кольцо на плоском фоне — это схема, а не состояние. Мягкий
     * радиальный ореол цвета состояния под кнопкой отвечает на «работает ли»
     * ещё до того, как прочитана подпись, — так устроен главный экран у всех
     * современных VPN. QSS теней не умеет, поэтому рисуем сами.
     */
    class PowerGlow : public QWidget {
    public:
        // Qt::WA_TransparentForMouseEvents здесь стоять НЕ ДОЛЖЕН, хоть и просится.
        // Кнопка питания — ребёнок ореола, а Qt снимает доставку мыши с виджета
        // ВМЕСТЕ С ДЕТЬМИ: с этим флагом кнопка не нажималась мышью с 1.8.0 по
        // 1.8.2. Нажатие мимо кнопки ореол и без флага не удерживает —
        // необработанное, оно уходит родителю. Сторож: test/PowerButtonTest.cpp.
        explicit PowerGlow(QWidget *parent) : QWidget(parent) {}
        void set(const QColor &c, bool on) {
            color = c;
            active = on;
            update();
        }

    protected:
        void paintEvent(QPaintEvent *) override {
            if (!active) return;
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            const QPointF c(width() / 2.0, height() / 2.0);
            QRadialGradient g(c, width() / 2.0);
            QColor a = color;
            a.setAlpha(64);
            g.setColorAt(0.0, a);
            a.setAlpha(22);
            g.setColorAt(0.55, a);
            a.setAlpha(0);
            g.setColorAt(1.0, a);
            p.fillRect(rect(), g);
        }

    private:
        QColor color;
        bool active = false;
    };

    MainShell::MainShell(QWidget *parent) : QWidget(parent) {
        // СТОРОЖ ПОПЫТКИ. Каждый известный выход из подключения заканчивает
        // попытку сам (окно, neko_start), а сторож — для того, что потеряется:
        // кнопка не имеет права остаться выключенной навсегда. Минута — с
        // запасом против настоящего старта, который занимает секунды. Создаётся
        // первым: setState обращается к нему, и вызов при сборке страниц не
        // должен упасть на пустом указателе.
        connectWatchdog = new QTimer(this);
        connectWatchdog->setSingleShot(true);
        connectWatchdog->setInterval(60 * 1000);
        connect(connectWatchdog, &QTimer::timeout, this, [this] {
            if (!attempt) return;
            attempt = false;
            failed = true;
            failureReason = tr("нет ответа");
            const State next = shown();
            paint(next, next == State::Failed ? failureReason : QString());
        });

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);

        row->addWidget(buildSidebar(), 0);

        pages = new QStackedWidget(this);
        pages->setObjectName("grPages");
        pages->setStyleSheet(QStringLiteral("QStackedWidget#grPages { background: %1; }").arg(kSurface));
        row->addWidget(pages, 1);

        pages->addWidget(buildConnectPage()); // 0 — подключение
        pages->addWidget(buildSettingsPage()); // moved to 3 when legacy pages are adopted
        selectPage(0);
    }

    QWidget *MainShell::buildSidebar() {
        auto *bar = new QWidget(this);
        bar->setFixedWidth(208);
        bar->setObjectName(QStringLiteral("grSidebar"));
        bar->setStyleSheet(QStringLiteral("QWidget#grSidebar { background: %1; border-right: 1px solid %2; }").arg(kSidebar, kLine));
        auto *box = new QVBoxLayout(bar);
        box->setContentsMargins(16, 26, 16, 18);
        box->setSpacing(8);
        auto *brand = new QLabel(QStringLiteral("GreenRhythm"), bar);
        QFont font = brand->font(); font.setBold(true); font.setPointSizeF(font.pointSizeF() * 1.2);
        brand->setFont(font);
        brand->setStyleSheet(QStringLiteral("color: %1; padding: 8px 4px;").arg(kText));
        box->addWidget(brand);
        box->addSpacing(24);
        struct Nav { QString label; QString icon; int page; };
        for (const auto &item : QList<Nav>{{tr("Подключение"), "gr-shield-check", 0},
                                          {tr("Серверы"), "gr-nav-servers", 1},
                                          {tr("Настройки"), "gr-sliders", 3}}) {
            auto *button = new QPushButton(Icons::icon(item.icon, QColor(kMuted), QColor(kAccent), 20), "  " + item.label, bar);
            button->setProperty("page", item.page);
            button->setCheckable(true);
            button->setMinimumHeight(44);
            button->setCursor(Qt::PointingHandCursor);
            button->setStyleSheet(QStringLiteral(
                "QPushButton { text-align: left; min-height: 44px; padding: 0 14px; border: 1px solid transparent; border-radius: 12px; background: transparent; color: %1; }"
                "QPushButton:hover { background: %2; color: %3; }"
                "QPushButton:checked { background: rgba(186,214,91,0.10); color: %4; }"
                "QPushButton:focus { border-color: %4; }").arg(kMuted, kSurfaceUp, kText, kAccent));
            connect(button, &QPushButton::clicked, this, [this, item] { selectPage(item.page); });
            navButtons << button;
            box->addWidget(button);
        }
        box->addStretch();
        // Новая версия — блоком, а не окном: окно посреди работы раздражает, а
        // блок ждёт, пока человек решит сам. Устроен как подписка ниже: одной
        // кнопкой строка в колонку не влезала. «Обновить» — прежняя проверка с
        // кнопками «Обновить» и «Открыть в браузере».
        updateNotice = new QWidget(bar); updateNotice->setObjectName("grUpdateNotice");
        auto *upd = new QVBoxLayout(updateNotice); upd->setContentsMargins(4, 8, 4, 8);
        updateNoticeText = new QLabel(updateNotice); updateNoticeText->setObjectName("grUpdateNoticeText"); updateNoticeText->setWordWrap(true);
        updateNoticeText->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(kAccent));
        auto *updateButton = new QPushButton(tr("Обновить"), updateNotice); updateButton->setObjectName("grUpdateNoticeButton");
        updateButton->setStyleSheet(linkStyle(kAccent)); updateButton->setCursor(Qt::PointingHandCursor);
        connect(updateButton, &QPushButton::clicked, this, &MainShell::checkUpdateRequested);
        upd->addWidget(updateNoticeText); upd->addWidget(updateButton); updateNotice->hide(); box->addWidget(updateNotice);
        subBlock = new QWidget(bar);
        auto *sub = new QVBoxLayout(subBlock); sub->setContentsMargins(4, 8, 4, 8);
        subSummary = muted(subBlock, QString()); subSummary->setWordWrap(true);
        subButton = new QPushButton(tr("Продлить подписку"), subBlock);
        subButton->setStyleSheet(linkStyle(kAccent));
        connect(subButton, &QPushButton::clicked, this, &MainShell::renewRequested);
        sub->addWidget(subSummary); sub->addWidget(subButton); subBlock->hide(); box->addWidget(subBlock);
        auto *status = new QWidget(bar);
        auto *statusRow = new QHBoxLayout(status); statusRow->setContentsMargins(10, 12, 10, 12);
        stateDot = new QLabel(status); stateDot->setFixedSize(8, 8);
        sidebarStatus = muted(status, tr("Не подключено"), 0.9);
        statusRow->addWidget(stateDot); statusRow->addWidget(sidebarStatus, 1);
        box->addWidget(status);
        moreButton = new QPushButton(Icons::icon("gr-more", QColor(kMuted), QColor(kText), 20), tr("  Ещё"), bar);
        moreButton->setMinimumHeight(40); moreButton->setCursor(Qt::PointingHandCursor);
        connect(moreButton, &QPushButton::clicked, this, [this] {
            QMenu menu(this);
            menu.addAction(Icons::icon("gr-nav-log", QColor(kText), QColor(kAccent), 18), tr("Журнал и соединения"), this, [this] { selectPage(2); });
            menu.addAction(tr("Проверить обновления"), this, &MainShell::checkUpdateRequested);
            menu.addAction(tr("Помощь с подключением"), this, &MainShell::troubleRequested);
            menu.addSeparator();
            menu.addAction(tr("Все команды"), this, [this] { emit moreRequested(moreButton->mapToGlobal(QPoint(0, moreButton->height()))); });
            menu.exec(moreButton->mapToGlobal(QPoint(0, moreButton->height())));
        });
        box->addWidget(moreButton);
        return bar;
    }

    QWidget *MainShell::buildConnectPage() {
        auto *page = new QWidget(this);
        auto *outer = new QVBoxLayout(page);
        outer->setContentsMargins(28, 24, 28, 24);
        auto *heading = new QLabel(tr("Подключение"), page);
        QFont headingFont = heading->font(); headingFont.setPointSizeF(headingFont.pointSizeF() * 1.45); headingFont.setBold(true);
        heading->setFont(headingFont); outer->addWidget(heading);
        outer->addStretch(1);
        auto *body = new QWidget(page); body->setFixedWidth(480);
        auto *box = new QVBoxLayout(body); box->setContentsMargins(0, 0, 0, 0); box->setSpacing(12);
        statusTitle = new QLabel(tr("Готов к подключению"), body);
        statusTitle->setObjectName(QStringLiteral("grStatusTitle"));
        statusTitle->setAlignment(Qt::AlignCenter);
        QFont titleFont = statusTitle->font(); titleFont.setBold(true); titleFont.setPointSizeF(titleFont.pointSizeF() * 1.35);
        statusTitle->setFont(titleFont); box->addWidget(statusTitle);
        powerHint = muted(body, tr("Выберите сервер и подключитесь"), 0.95);
        powerHint->setObjectName(QStringLiteral("grPowerHint"));
        powerHint->setAlignment(Qt::AlignCenter); powerHint->setWordWrap(true); box->addWidget(powerHint);
        glow = new PowerGlow(body); glow->setFixedSize(192, 192);
        auto *glowBox = new QGridLayout(glow); glowBox->setContentsMargins(0, 0, 0, 0);
        power = new QPushButton(glow); power->setObjectName("grPower"); power->setFixedSize(144, 144);
        power->setCursor(Qt::PointingHandCursor); power->setIconSize(QSize(52, 52));
        // Нажатие — действие человека: прежний отказ снимается сразу. Иначе он
        // остался бы висеть, если нажатие ничего не начнёт (например, серверов
        // нет и окно ответило сообщением).
        connect(power, &QPushButton::clicked, this, [this] {
            dismissFailure();
            emit connectToggled();
        });
        glowBox->addWidget(power, 0, 0, Qt::AlignCenter); box->addWidget(glow, 0, Qt::AlignCenter);

        auto *card = new QPushButton(body); currentCard = card;
        card->setObjectName("grServerChoice"); card->setCursor(Qt::PointingHandCursor);
        card->setAccessibleName(tr("Выбрать сервер")); card->setMinimumHeight(86);
        card->setStyleSheet(QStringLiteral(
            "QPushButton#grServerChoice { min-height: 100px; background: %1; border: 1px solid %2; border-radius: 16px; padding: 0; }"
            "QPushButton#grServerChoice:hover, QPushButton#grServerChoice:focus { border-color: %3; }").arg(kSurfaceUp, kLine, kAccent));
        auto *cardRow = new QHBoxLayout(card); cardRow->setContentsMargins(18, 16, 18, 16); cardRow->setSpacing(14);
        auto *serverIcon = new QLabel(card); serverIcon->setPixmap(Icons::pixmap("gr-nav-servers", QColor(kAccent), 24));
        cardRow->addWidget(serverIcon);
        auto *labels = new QVBoxLayout(); labels->setSpacing(4);
        currentTitle = new QLabel(tr("Автовыбор сервера"), card); currentTitle->setWordWrap(true);
        QFont nameFont = currentTitle->font(); nameFont.setBold(true); currentTitle->setFont(nameFont);
        currentMeta = muted(card, tr("Выбрать из списка"), 0.9); currentMeta->setWordWrap(true);
        labels->addWidget(currentTitle); labels->addWidget(currentMeta);
        tagRow = new QWidget(card); auto *tags = new QHBoxLayout(tagRow); tags->setContentsMargins(0, 0, 0, 0); tags->setSpacing(6);
        tagRow->setStyleSheet("background: transparent; border: none;"); tagRow->hide(); labels->addWidget(tagRow); cardRow->addLayout(labels, 1);
        auto *arrow = new QLabel(card); arrow->setPixmap(Icons::pixmap("gr-arrow-right", QColor(kMuted), 18)); cardRow->addWidget(arrow);
        for (auto *label : card->findChildren<QLabel *>()) label->setAttribute(Qt::WA_TransparentForMouseEvents);
        tagRow->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(card, &QPushButton::clicked, this, &MainShell::chooseServer); box->addWidget(card);

        auto *mode = new QPushButton(body); mode->setObjectName("grConnectionOptions"); mode->setMinimumHeight(44);
        mode->setAccessibleName(tr("Параметры подключения")); mode->setCursor(Qt::PointingHandCursor);
        mode->setStyleSheet(QStringLiteral("QPushButton#grConnectionOptions { min-height: 44px; background: %1; border: 1px solid transparent; border-radius: 12px; padding: 0; } QPushButton#grConnectionOptions:hover, QPushButton#grConnectionOptions:focus { border-color: %2; }").arg(kSurfaceUp, kLine));
        auto *modeRow = new QHBoxLayout(mode); modeRow->setContentsMargins(14, 6, 14, 6);
        auto *modeIcon = new QLabel(mode); modeIcon->setPixmap(Icons::pixmap("gr-sliders", QColor(kMuted), 18)); modeRow->addWidget(modeIcon);
        modeSummary = muted(mode, tr("Параметры подключения"), 0.95); modeRow->addWidget(modeSummary, 1);
        auto *modeArrow = new QLabel(mode); modeArrow->setPixmap(Icons::pixmap("gr-arrow-right", QColor(kMuted), 16)); modeRow->addWidget(modeArrow);
        for (auto *label : mode->findChildren<QLabel *>()) label->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(mode, &QPushButton::clicked, this, [this] { selectPage(3); }); box->addWidget(mode);

        emptyHint = new QWidget(body); auto *emptyBox = new QVBoxLayout(emptyHint); emptyBox->setContentsMargins(0, 8, 0, 8);
        auto *emptyText = muted(emptyHint, tr("Добавьте сервер или ссылку подписки")); emptyText->setAlignment(Qt::AlignCenter); emptyBox->addWidget(emptyText);
        auto *add = new QPushButton(Icons::icon("gr-plus", QColor(kAccent), QColor(kAccent), 18), tr("Добавить подключение"), emptyHint);
        add->setMinimumHeight(44); connect(add, &QPushButton::clicked, this, &MainShell::addServerRequested); emptyBox->addWidget(add); emptyHint->hide(); box->addWidget(emptyHint);
        auto *metrics = new QHBoxLayout(); metrics->setContentsMargins(0, 10, 0, 0); metrics->setSpacing(6);
        metrics->addWidget(tile(body, tr("соединений VPN"), &liveVpn, kText, 1.1), 1);
        metrics->addWidget(tile(body, tr("напрямую"), &liveDirect, kText, 1.1), 1);
        metrics->addWidget(tile(body, tr("получено / отправлено"), &liveTraffic, kText, 0.95), 2);
        box->addLayout(metrics);
        outer->addWidget(body, 0, Qt::AlignHCenter); outer->addStretch(2);
        troubleLink = new QPushButton(tr("Нужна помощь с подключением?"), page); troubleLink->setStyleSheet(linkStyle(kMuted));
        troubleLink->setCursor(Qt::PointingHandCursor); connect(troubleLink, &QPushButton::clicked, this, &MainShell::troubleRequested);
        outer->addWidget(troubleLink, 0, Qt::AlignCenter);
        return page;
    }

    QWidget *MainShell::buildSettingsPage() {
        auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        auto *page = new QWidget(scroll); auto *box = new QVBoxLayout(page); box->setContentsMargins(28, 24, 28, 24); box->setSpacing(18);
        auto *heading = new QLabel(tr("Настройки"), page); QFont f = heading->font(); f.setBold(true); f.setPointSizeF(f.pointSizeF() * 1.45); heading->setFont(f); box->addWidget(heading);
        box->addWidget(muted(page, tr("Как подключаться и что направлять через VPN")));
        auto addRow = [&](const QString &title, const QString &description, const QString &icon, QPushButton *button) {
            auto *card = new QWidget(page); card->setObjectName("grSettingRow");
            card->setStyleSheet(QStringLiteral("QWidget#grSettingRow { background: %1; border-radius: 14px; }").arg(kSurfaceUp));
            auto *row = new QHBoxLayout(card); row->setContentsMargins(16, 14, 16, 14); row->setSpacing(14);
            auto *glyph = new QLabel(card); glyph->setPixmap(Icons::pixmap(icon, QColor(kAccent), 22)); row->addWidget(glyph);
            auto *labels = new QVBoxLayout(); auto *name = new QLabel(title, card); QFont font = name->font(); font.setBold(true); name->setFont(font); labels->addWidget(name);
            auto *detail = muted(card, description, 0.9); detail->setWordWrap(true); labels->addWidget(detail); row->addLayout(labels, 1);
            if (!button->isCheckable()) button->setStyleSheet(QStringLiteral("QPushButton { min-height: 36px; padding: 0 14px; background: %1; border-radius: 9px; color: %2; border: 1px solid transparent; } QPushButton:hover, QPushButton:focus { border-color: %3; }").arg(kSurface, kText, kAccent));
            button->setCursor(Qt::PointingHandCursor); row->addWidget(button); box->addWidget(card);
            return detail;
        };
        box->addWidget(caption(page, tr("ПОДКЛЮЧЕНИЕ")));
        modeTun = toggle(page, tr("Включить")); modeTun->setObjectName("grModeTun"); modeTun->setAccessibleName(tr("Туннель для всего устройства"));
        connect(modeTun, &QPushButton::clicked, this, &MainShell::tunToggled);
        addRow(tr("Туннель"), tr("Трафик всего устройства. Для запуска нужны права администратора."), "gr-shield-check", modeTun);
        // Окно параметров туннеля открывалось только из «Ещё → Все команды». Всё в
        // нём техническое, поэтому оно остаётся окном, но вход — рядом с туннелем.
        auto *tunnelSettings = new QPushButton(tr("Настроить"), page); tunnelSettings->setObjectName("grTunnelSettings"); tunnelSettings->setAccessibleName(tr("Параметры туннеля"));
        connect(tunnelSettings, &QPushButton::clicked, this, &MainShell::tunnelSettingsRequested);
        addRow(tr("Параметры туннеля"), tr("Стек, MTU, IPv6 и что пускать мимо туннеля: адреса и программы."), "gr-sliders", tunnelSettings);
        modeProxy = toggle(page, tr("Включить")); modeProxy->setAccessibleName(tr("Системный прокси"));
        connect(modeProxy, &QPushButton::clicked, this, &MainShell::systemProxyToggled);
        addRow(tr("Системный прокси"), tr("Для браузеров и приложений, использующих настройки прокси системы."), "gr-nav-connect", modeProxy);
        // Автопилот, резерв и раздача жили только в «Ещё → Все команды»: автопилот
        // включён у всех, резерв — отдельная услуга, и о них никто не знал.
        autopilotToggle = toggle(page, tr("Автопилот соединения")); autopilotToggle->setObjectName("grAutopilot");
        connect(autopilotToggle, &QPushButton::clicked, this, &MainShell::autopilotToggled);
        addRow(tr("Автопилот соединения"), tr("Проверяет, что интернет через VPN правда работает. При обрыве сам обновит подписку или переключит сервер."), "gr-activity", autopilotToggle);
        auto *relay = new QPushButton(tr("Подключить"), page); relay->setObjectName("grRelay"); relay->setAccessibleName(tr("Резервное подключение"));
        connect(relay, &QPushButton::clicked, this, &MainShell::relayRequested);
        addRow(tr("Резервное подключение"), tr("Работает там, где обычные серверы перестают проходить. Отдельная услуга, включается кодом из личного кабинета."), "gr-arrows-updown", relay);
        shareToLanToggle = toggle(page, tr("Раздавать VPN другим устройствам")); shareToLanToggle->setObjectName("grShareToLan");
        connect(shareToLanToggle, &QPushButton::clicked, this, &MainShell::shareToLanToggled);
        shareToLanDetail = addRow(tr("Раздавать VPN другим устройствам"), QString(), "gr-devices", shareToLanToggle); shareToLanDetail->setObjectName("grShareToLanDetail");
        box->addWidget(caption(page, tr("МАРШРУТИЗАЦИЯ")));
        gamesToggle = toggle(page, tr("Включить")); gamesToggle->setAccessibleName(tr("Игры через VPN"));
        connect(gamesToggle, &QPushButton::clicked, this, &MainShell::gamesViaTunnelToggled);
        addRow(tr("Игры через VPN"), tr("Обычно игры подключаются напрямую. Включите, если игровые серверы недоступны."), "gr-routes", gamesToggle);
        bypassLine = new QPushButton(tr("Настроить"), page); connect(bypassLine, &QPushButton::clicked, this, &MainShell::bypassListRequested);
        addRow(tr("Исключения"), tr("Приложения и адреса, которые подключаются напрямую."), "gr-list", bypassLine);
        auto *routes = new QPushButton(tr("Открыть"), page); connect(routes, &QPushButton::clicked, this, &MainShell::routesRequested);
        addRow(tr("Правила маршрутизации"), tr("Собственные правила для доменов, адресов и приложений."), "gr-routes", routes);
        box->addWidget(caption(page, tr("ОБХОД ФИЛЬТРАЦИИ")));
        dpiToggle = toggle(page, tr("Включить")); dpiToggle->setAccessibleName(tr("Обход фильтрации"));
        connect(dpiToggle, &QPushButton::clicked, this, &MainShell::dpiFragmentToggled);
        addRow(tr("Обход фильтрации"), tr("Дробление TLS для прямых соединений. Используйте, если провайдер блокирует сайты."), "gr-shield-alert", dpiToggle);
        dpiModuleToggle = toggle(page, tr("Включить")); connect(dpiModuleToggle, &QPushButton::clicked, this, &MainShell::dpiModuleToggled);
#ifdef Q_OS_WIN
        addRow(tr("Усиленный обход"), tr("Модуль winws. Требуется отдельная загрузка и системный драйвер."), "gr-shield-alert", dpiModuleToggle);
        auto *interference = new QPushButton(tr("Проверить"), page); connect(interference, &QPushButton::clicked, this, &MainShell::interferenceRequested);
        addRow(tr("Конфликты приложений"), tr("Проверить, что мешает подключению."), "gr-help", interference);
#else
        dpiModuleToggle->hide();
#endif
        dpiModuleState = muted(page, QString()); dpiModuleState->setWordWrap(true); dpiModuleState->hide(); box->addWidget(dpiModuleState);
        box->addWidget(caption(page, tr("ПРИЛОЖЕНИЕ")));
        // Автозапуск жил в меню «Все команды», «свёрнутым» и автообновление — в
        // «Дополнительных настройках» между адресом прокси и User-Agent, и там их
        // не находили. Правду о них знает окно, оно же кладёт её сюда: setAppOptions.
        autostartToggle = toggle(page, tr("Запуск вместе с системой")); autostartToggle->setObjectName("grAutostart");
        connect(autostartToggle, &QPushButton::clicked, this, &MainShell::autostartToggled);
#ifdef Q_OS_MACOS
        // Пункт входа macOS запускает программу без доводов, окно откроется.
        addRow(tr("Запуск вместе с системой"), tr("Программа запустится сама, когда вы войдёте в систему."), "gr-power", autostartToggle);
#else
        // Автозапуск Windows и Linux передаёт -tray (sys/AutoRun.cpp): окно не откроется.
        addRow(tr("Запуск вместе с системой"), tr("При входе в систему программа запустится сама и будет ждать в трее."), "gr-power", autostartToggle);
#endif
        // Пункт назывался «Запомнить последний профиль», и из названия не понять,
        // что программа при старте подключится сама. Пара к автозапуску.
        connectOnStartToggle = toggle(page, tr("Подключаться при запуске")); connectOnStartToggle->setObjectName("grConnectOnStart");
        connect(connectOnStartToggle, &QPushButton::clicked, this, &MainShell::connectOnStartToggled);
        addRow(tr("Подключаться при запуске"), tr("Программа сама подключится к последнему серверу в том же режиме, что был перед выходом."), "gr-history", connectOnStartToggle);
        startHiddenToggle = toggle(page, tr("Запускать свёрнутым")); startHiddenToggle->setObjectName("grStartHidden");
        connect(startHiddenToggle, &QPushButton::clicked, this, &MainShell::startHiddenToggled);
        addRow(tr("Запускать свёрнутым"), tr("При запуске окно не открывается, программа ждёт в трее."), "gr-tray", startHiddenToggle);
        subscriptionToggle = toggle(page, tr("Обновлять подписку автоматически")); subscriptionToggle->setObjectName("grSubscriptionAutoUpdate");
        connect(subscriptionToggle, &QPushButton::clicked, this, &MainShell::subscriptionAutoUpdateToggled);
        subscriptionDetail = addRow(tr("Обновлять подписку автоматически"), QString(), "gr-refresh", subscriptionToggle); subscriptionDetail->setObjectName("grSubscriptionAutoUpdateDetail");
        auto *updates = new QPushButton(tr("Проверить"), page); updates->setObjectName("grCheckUpdates"); updates->setAccessibleName(tr("Проверить обновления"));
        checkUpdatesButton = updates;
        connect(updates, &QPushButton::clicked, this, &MainShell::checkUpdateRequested);
        updatesDetail = addRow(tr("Обновления программы"), QString(), "gr-download", updates); updatesDetail->setObjectName("grCheckUpdatesDetail");
        setAppOptions(false, false, 0); setAppVersion(QString());
        auto *advanced = new QPushButton(tr("Открыть"), page); connect(advanced, &QPushButton::clicked, this, &MainShell::settingsRequested);
        addRow(tr("Дополнительные настройки"), tr("Язык и оформление, локальный прокси, проверка серверов, тонкая настройка подписки и ядра."), "gr-sliders", advanced);
        box->addWidget(caption(page, tr("ПОМОЩЬ")));
        auto *support = new QPushButton(tr("Написать"), page); support->setObjectName("grSupport"); support->setAccessibleName(tr("Поддержка в Telegram"));
        connect(support, &QPushButton::clicked, this, &MainShell::supportRequested);
        addRow(tr("Поддержка в Telegram"), tr("Напишите нам, если что-то не работает или непонятно."), "gr-chat", support);
        auto *diagnostics = new QPushButton(tr("Проверить"), page); diagnostics->setObjectName("grDiagnostics"); diagnostics->setAccessibleName(tr("Диагностика соединения"));
        connect(diagnostics, &QPushButton::clicked, this, &MainShell::diagnosticsRequested);
        addRow(tr("Диагностика соединения"), tr("Проверит интернет, DNS и доступность сервера и скажет, что не так."), "gr-help", diagnostics);
        auto *about = new QPushButton(tr("Открыть"), page); about->setObjectName("grAbout"); about->setAccessibleName(tr("О программе"));
        connect(about, &QPushButton::clicked, this, &MainShell::aboutRequested);
        addRow(tr("О программе"), tr("Версия, сайт, лицензия и исходный код."), "gr-info", about);
        setConnectionOptions(false, false, false, QString(), 0);
        box->addStretch(); scroll->setWidget(page); return scroll;
    }

    void MainShell::chooseServer() {
        QDialog dialog(this); dialog.setWindowTitle(tr("Выбрать сервер")); dialog.resize(480, 420);
        auto *box = new QVBoxLayout(&dialog); box->setContentsMargins(20, 20, 20, 20); box->setSpacing(12);
        auto *search = new QLineEdit(&dialog); search->setPlaceholderText(tr("Найти сервер")); search->setClearButtonEnabled(true); search->setAccessibleName(tr("Поиск сервера")); box->addWidget(search);
        auto *list = new QListWidget(&dialog); list->setObjectName("grServerPicker"); list->setSpacing(4);
        auto append = [&](int id, const QString &text) { auto *item = new QListWidgetItem(text, list); item->setData(Qt::UserRole, id); item->setSizeHint(QSize(0, 48)); if (id == selectedServerId) list->setCurrentItem(item); };
        append(-1, tr("Автовыбор — самый быстрый"));
        for (const auto &server : availableServers) append(server.id, server.latency.isEmpty() ? server.name : server.name + "  ·  " + server.latency);
        box->addWidget(list, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); buttons->button(QDialogButtonBox::Ok)->setText(tr("Выбрать")); buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена")); box->addWidget(buttons);
        const auto updateChoice = [list, buttons] { buttons->button(QDialogButtonBox::Ok)->setEnabled(list->currentItem() && !list->currentItem()->isHidden()); };
        connect(search, &QLineEdit::textChanged, &dialog, [list, updateChoice](const QString &text) { for (int i = 0; i < list->count(); ++i) list->item(i)->setHidden(!list->item(i)->text().contains(text, Qt::CaseInsensitive)); updateChoice(); });
        connect(list, &QListWidget::currentRowChanged, &dialog, [updateChoice] { updateChoice(); });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(list, &QListWidget::itemActivated, &dialog, [&dialog](QListWidgetItem *) { dialog.accept(); }); updateChoice(); search->setFocus();
        if (dialog.exec() == QDialog::Accepted && list->currentItem() && !list->currentItem()->isHidden()) {
            // Выбран сервер — причина прошлого отказа к нему не относится.
            dismissFailure();
            emit serverChosen(list->currentItem()->data(Qt::UserRole).toInt());
            selectPage(0);
        }
    }

    QWidget *MainShell::framed(QWidget *content, const QString &title, bool serverPage) {
        // ПОЛЯ И ЗАГОЛОВОК — У КАЖДОЙ СТРАНИЦЫ, А НЕ ТОЛЬКО У ПОДКЛЮЧЕНИЯ. Список
        // серверов и таблица соединений упирались в край окна с зазором в шесть
        // точек, пока у подключения поля были в сорок; страницы выглядели из
        // разных программ. Заголовок — потому что у страницы должно быть имя,
        // которое читается раньше содержимого.
        auto *page = new QWidget(this);
        auto *box = new QVBoxLayout(page);
        box->setContentsMargins(28, 22, 28, 20);
        box->setSpacing(14);
        auto *h = new QLabel(title, page);
        QFont hf = h->font();
        hf.setBold(true);
        hf.setPointSizeF(hf.pointSizeF() * 1.55);
        h->setFont(hf);
        h->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(kText));
        box->addWidget(h);
        if (serverPage) {
            serverSearch = new QLineEdit(page); serverSearch->setObjectName("grServerSearch");
            serverSearch->setPlaceholderText(tr("Поиск по названию, адресу или протоколу"));
            serverSearch->setAccessibleName(tr("Поиск серверов")); serverSearch->setMinimumHeight(38); serverSearch->setClearButtonEnabled(true);
            connect(serverSearch, &QLineEdit::textChanged, this, &MainShell::serverSearchChanged); box->addWidget(serverSearch);
            auto *actions = new QHBoxLayout(); actions->setSpacing(8);
            auto action = [&](const QString &label, const QString &icon, auto signal) {
                auto *button = new QPushButton(Icons::icon(icon, QColor(kMuted), QColor(kAccent), 18), label, page);
                button->setMinimumHeight(36); button->setCursor(Qt::PointingHandCursor); button->setAccessibleName(label);
                connect(button, &QPushButton::clicked, this, signal); actions->addWidget(button); return button;
            };
            action(tr("Выбрать"), "gr-nav-servers", &MainShell::chooseSelectedRequested);
            action(tr("Добавить"), "gr-plus", &MainShell::addServerRequested);
            action(tr("Вставить"), "gr-clipboard", &MainShell::pasteRequested);
            auto *scan = action(tr("QR-код"), "gr-qr", &MainShell::scanRequested);
#ifdef NKR_NO_ZXING
            scan->setEnabled(false); scan->setToolTip(tr("Сканирование QR недоступно в этой сборке"));
#endif
            actions->addStretch();
            auto *test = action(tr("Проверить задержку серверов"), "gr-activity", &MainShell::testServersRequested);
            test->setText(QString()); test->setToolTip(tr("Проверить задержку серверов")); test->setFixedWidth(40);
            auto *refresh = action(tr("Обновить подписки"), "gr-refresh", &MainShell::updateSubscriptionRequested);
            refresh->setText(QString()); refresh->setToolTip(tr("Обновить подписки")); refresh->setFixedWidth(40);
            box->addLayout(actions);
            searchResult = muted(page, QString(), 0.9);
            searchResult->setWordWrap(true); box->addWidget(searchResult);
        }
        box->addWidget(content, 1);
        return page;
    }

    void MainShell::adopt(QWidget *servers, QWidget *logs) {
        // Виджеты переезжают, а не создаются заново: к ним привязана вся прежняя
        // проводка окна. addWidget сам меняет родителя.
        if (servers != nullptr) pages->insertWidget(1, framed(servers, tr("Серверы"), true));
        if (logs != nullptr) pages->insertWidget(2, framed(logs, tr("Журнал")));
        selectPage(0);
    }

    void MainShell::showPage(int index) { selectPage(index); }

    void MainShell::selectPage(int index) {
        if (index < 0 || index >= pages->count()) return;
        pages->setCurrentIndex(index);
        for (auto *button : navButtons) button->setChecked(button->property("page").toInt() == index);
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
                currentMeta->setText(tr("Нажмите, чтобы выбрать"));
            } else {
                currentTitle->setText(idleServerName);
                currentMeta->setText(tr("Нажмите, чтобы сменить сервер"));
            }
        }
        // ОПРОС НЕ ЗАКАНЧИВАЕТ ПОПЫТКУ И НЕ СТИРАЕТ ОТКАЗ. Прежде здесь стоял
        // setState(подключено ? Connected : Idle), и раз в две секунды он
        // перебивал оба: причина отказа исчезала раньше, чем её успевали
        // прочесть, а на медленном старте кнопка включалась снова, и второе
        // нажатие упиралось в «Another profile is starting...». Сторож:
        // test/PowerStateTest.cpp.
        //
        // Меняет учёт опрос ровно в одном случае — подключение без попытки:
        // это успех, и старый отказ больше не правда.
        if (connected && !attempt) {
            failed = false;
            failureReason.clear();
        }
        const State next = shown();
        paint(next, next == State::Failed ? failureReason : QString());
    }

    void MainShell::setAppOptions(bool autostart, bool startHidden, int subscriptionMinutes) {
        // setChecked не шлёт clicked, поэтому обратной волны сигналов нет.
        if (autostartToggle != nullptr) autostartToggle->setChecked(autostart);
        if (startHiddenToggle != nullptr) startHiddenToggle->setChecked(startHidden);
        // «Вкл» — только когда таймер окна правда заведётся: при 10 минутах
        // переключатель стоял бы включённым, а подписка не обновлялась бы.
        const bool on = SubscriptionSchedule::isOn(subscriptionMinutes);
        if (subscriptionToggle != nullptr) subscriptionToggle->setChecked(on);
        if (subscriptionDetail == nullptr) return;
        if (!on) {
            subscriptionDetail->setText(tr("Выключено: обновить можно вручную на странице «Серверы»."));
            return;
        }
        const int minutes = subscriptionMinutes;
        const int hours = minutes / 60;
        const QString every = minutes == 60     ? tr("раз в час")
                              : minutes == 1440 ? tr("раз в сутки")
                              : minutes % 60 == 0
                                  ? tr("раз в %1 %2").arg(hours).arg(plural(hours, tr("час"), tr("часа"), tr("часов")))
                                  : tr("раз в %1 %2").arg(minutes).arg(plural(minutes, tr("минуту"), tr("минуты"), tr("минут")));
        subscriptionDetail->setText(tr("Серверы и остаток трафика обновляются %1.").arg(every));
    }

    void MainShell::setAppVersion(const QString &version) {
        installedVersion = version;
        paintUpdates();
    }

    void MainShell::setUpdateAvailable(bool available, const QString &version) {
        updateAvailable = available;
        availableVersion = version;
        paintUpdates();
    }

    void MainShell::paintUpdates() {
        const QString fresh = availableVersion.isEmpty() ? tr("новая версия") : tr("версия %1").arg(availableVersion);
        if (updateNotice != nullptr) {
            updateNoticeText->setText(tr("Доступна %1").arg(fresh));
            updateNotice->setVisible(updateAvailable);
        }
        if (checkUpdatesButton != nullptr) checkUpdatesButton->setText(updateAvailable ? tr("Обновить") : tr("Проверить"));
        if (updatesDetail == nullptr) return;
        if (updateAvailable) {
            updatesDetail->setText(installedVersion.isEmpty() ? tr("Доступна %1.").arg(fresh)
                                                              : tr("Доступна %1, установлена %2.").arg(fresh, installedVersion));
        } else {
            updatesDetail->setText(installedVersion.isEmpty() ? tr("Проверить, не вышла ли новая версия.")
                                                              : tr("Установлена версия %1.").arg(installedVersion));
        }
    }

    void MainShell::setConnectionOptions(bool connectOnStart, bool autopilot, bool shareToLan,
                                         const QString &lanAddress, int port) {
        // setChecked не шлёт clicked, поэтому обратной волны сигналов нет.
        if (connectOnStartToggle != nullptr) connectOnStartToggle->setChecked(connectOnStart);
        if (autopilotToggle != nullptr) autopilotToggle->setChecked(autopilot);
        if (shareToLanToggle != nullptr) shareToLanToggle->setChecked(shareToLan);
        if (shareToLanDetail == nullptr) return;
        // Прокси без пароля открыт всей сети — в кафе или гостинице это чужие люди.
        const QString publicWifi = tr("Не включайте в общественном Wi-Fi.");
        if (!shareToLan) {
            shareToLanDetail->setText(tr("Телефон, телевизор или приставка в этой сети смогут выходить через этот компьютер. %1").arg(publicWifi));
        } else if (!lanAddress.isEmpty()) {
            shareToLanDetail->setText(tr("На другом устройстве укажите прокси %1, порт %2 — HTTP или SOCKS5. %3").arg(lanAddress).arg(port).arg(publicWifi));
        } else {
            shareToLanDetail->setText(tr("На другом устройстве укажите прокси: адрес этого компьютера в сети, порт %1 — HTTP или SOCKS5. %2").arg(port).arg(publicWifi));
        }
    }

    void MainShell::setIdleServer(const QString &name) {
        idleServerName = name;
        if (!connected) setConnectionState(false, QString(), QString());
    }

    void MainShell::setServers(const QList<ServerItem> &servers, int currentId) {
        availableServers = servers;
        selectedServerId = currentId;
    }

    void MainShell::setSearchResultCount(int visible, int total) {
        if (!searchResult) return;
        searchResult->setText(total == 0 ? tr("Серверов пока нет. Добавьте ссылку или вставьте её из буфера.") :
                              visible == 0 ? tr("Ничего не найдено. Попробуйте другое название или адрес.") :
                              tr("Показано %1 из %2").arg(visible).arg(total));
    }

    void MainShell::focusServerSearch() {
        selectPage(1);
        if (serverSearch) serverSearch->setFocus();
    }

    void MainShell::setModes(bool tun, bool systemProxy, bool gamesViaTunnel, bool dpiFragment) {
        // setChecked не шлёт clicked, поэтому обратной волны сигналов нет.
        if (modeTun != nullptr) modeTun->setChecked(tun);
        if (modeProxy != nullptr) modeProxy->setChecked(systemProxy);
        if (gamesToggle != nullptr) gamesToggle->setChecked(gamesViaTunnel);
        if (dpiToggle != nullptr) dpiToggle->setChecked(dpiFragment);
        for (auto *button : {modeTun, modeProxy, gamesToggle, dpiToggle}) if (button) button->setText(button->isChecked() ? tr("Включено") : tr("Включить"));
        if (modeSummary) modeSummary->setText(tun ? (systemProxy ? tr("Туннель и системный прокси") : tr("Туннель для всего устройства")) : (systemProxy ? tr("Системный прокси") : tr("Режим подключения · настроить")));
    }

    void MainShell::setDpiModule(bool enabled, bool running, const QString &text) {
        if (dpiModuleToggle != nullptr) { dpiModuleToggle->setChecked(enabled); dpiModuleToggle->setText(enabled ? tr("Включено") : tr("Включить")); }
        if (dpiModuleState == nullptr) return;
        // Строка нужна ровно тогда, когда включено и НЕ работает: это и есть
        // расхождение между желанием и действительностью. Когда работает —
        // фишка уже всё сказала; когда выключено — говорить нечего.
        const bool show = enabled && !running && !text.isEmpty();
        dpiModuleState->setText(show ? tr("Усиленный обход: %1").arg(text) : QString());
        dpiModuleState->setVisible(show);
    }

    void MainShell::setState(State next, const QString &reason) {
        // Учёт — здесь, рисование — в paint(): опрос рисует из учёта, поэтому
        // попытку и отказ ему не сбить.
        switch (next) {
            case State::Connecting:
                attempt = true;
                failed = false;
                failureReason.clear();
                connectWatchdog->start();
                break;
            case State::Failed:
                attempt = false;
                failed = true;
                failureReason = reason;
                connectWatchdog->stop();
                break;
            case State::Connected:
            case State::Idle:
                attempt = false;
                failed = false;
                failureReason.clear();
                connectWatchdog->stop();
                break;
        }
        paint(next, reason);
    }

    MainShell::State MainShell::shown() const {
        if (attempt) return State::Connecting;
        if (connected) return State::Connected;
        if (failed) return State::Failed;
        return State::Idle;
    }

    void MainShell::finishConnecting() {
        if (!attempt) return;
        attempt = false;
        connectWatchdog->stop();
        const State next = shown();
        paint(next, next == State::Failed ? failureReason : QString());
    }

    bool MainShell::isConnecting() const { return attempt; }

    void MainShell::setConnectingTimeout(int ms) { connectWatchdog->setInterval(ms); }

    void MainShell::dismissFailure() {
        if (!failed) return;
        failed = false;
        failureReason.clear();
        if (!attempt) paint(shown(), QString());
    }

    void MainShell::paint(State next, const QString &reason) {
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
                fill = QStringLiteral("rgba(186,214,91,0.12)");
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
        power->setAccessibleName(hint);
        power->setToolTip(hint);
        power->setStyleSheet(
            QStringLiteral("QPushButton#grPower { min-width: 144px; max-width: 144px;"
                           " min-height: 144px; max-height: 144px; padding: 0;"
                           " border-radius: 72px; color: %1; background: %2;"
                           " border: 2px solid %3; }"
                           "QPushButton#grPower:hover { border-color: %4; }")
                .arg(glyph, fill, ring, next == State::Connected ? QString(kAccent)
                                                                 : QString(kAccent)));
        power->setIcon(QIcon(Icons::pixmap(next == State::Connected ? QStringLiteral("gr-shield-check") : QStringLiteral("gr-power"), QColor(glyph), 64)));
        // Ореол только у живых состояний: подключено — акцент, подключаюсь —
        // янтарь, не вышло — красный. В покое кнопка стоит на ровном фоне.
        if (glow != nullptr) glow->set(QColor(glyph), next != State::Idle);
        powerHint->setText(next == State::Idle ? tr("Выберите сервер и нажмите кнопку") : hint);
        const QString status = next == State::Connected ? tr("Подключено") : next == State::Connecting ? tr("Подключение…") : next == State::Failed ? tr("Не удалось подключиться") : tr("Не подключено");
        if (statusTitle) statusTitle->setText(status);
        if (sidebarStatus) sidebarStatus->setText(status);
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
                                    "color: %1; background: rgba(186,214,91,0.14);"
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
            programs > 0 ? tr("%1 %2")
                               .arg(programs)
                               .arg(plural(programs, tr("программа"), tr("программы"),
                                           tr("программ")))
                         : tr("Настроить"));
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
        subButton->setStyleSheet(linkStyle(low ? kAmber : kMuted, low));
    }

    void MainShell::setBusy(bool isBusy) {
        // Оставлено ради прежних вызовов: занятость — это состояние «подключаюсь».
        if (isBusy) {
            setState(State::Connecting);
        } else {
            finishConnecting();
        }
    }

} // namespace GreenRhythm
