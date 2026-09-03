#include "ui/dialog_greenrhythm.h"

#include "main/RunningPrograms.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

namespace {

// Токены темы — в одном месте, а не россыпью по виджетам.
//
// Почему литералом, а не палитрой: таблица стилей Qt перекрывает QPalette, и
// цвет, выставленный палитрой, молча не применяется. Ровно на этом «Подключено»
// осталось белым, хотя код честно просил акцентный цвет. Значения те же, что в
// res/theme/feiyangqingyun/qss/modern.css.
constexpr auto kAccent = "#3fb950";

/** Заголовок раздела: прописные, разрежённые, приглушённые. */
QLabel *sectionTitle(QWidget *parent, const QString &text) {
    auto *l = new QLabel(text.toUpper(), parent);
    QFont f = l->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    l->setFont(f);
    // Цвет берём из палитры темы, а не вписываем: тем в проекте две, и вписанный
    // серый в одной из них становится либо невидимым, либо кричащим.
    auto pal = l->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
    l->setPalette(pal);
    return l;
}

/**
 * Строка-действие: крупное имя, под ним объяснение, справа кнопка.
 *
 * Объяснение обязательно. Ровно его отсутствие и сделало наши функции
 * невидимыми: десять пунктов подряд, и ни один не говорит, что он даёт.
 */
QWidget *actionRow(QWidget *parent, const QString &title, const QString &note,
                   const QString &button, const std::function<void()> &go,
                   bool primary = false) {
    auto *box = new QWidget(parent);
    auto *line = new QHBoxLayout(box);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(16);

    auto *texts = new QVBoxLayout();
    texts->setSpacing(2);
    auto *name = new QLabel(title, box);
    QFont bold = name->font();
    bold.setBold(true);
    name->setFont(bold);
    texts->addWidget(name);

    auto *hint = new QLabel(note, box);
    hint->setWordWrap(true);
    auto pal = hint->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
    hint->setPalette(pal);
    texts->addWidget(hint);
    line->addLayout(texts, 1);

    auto *act = new QPushButton(button, box);
    act->setCursor(Qt::PointingHandCursor);
    act->setMinimumWidth(132);
    if (primary) act->setDefault(true);
    QObject::connect(act, &QPushButton::clicked, box, [go] { go(); });
    line->addWidget(act, 0, Qt::AlignTop);
    return box;
}

QFrame *separator(QWidget *parent) {
    auto *f = new QFrame(parent);
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Plain);
    auto pal = f->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Mid));
    f->setPalette(pal);
    return f;
}

} // namespace

DialogGreenRhythm::DialogGreenRhythm(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Зелёный Ритм"));
    setMinimumWidth(560);
    // Размер задаём сами: содержимое в прокрутке, и его sizeHint — вся длина
    // панели разом. Без этого окно открывается выше экрана, а нижние кнопки
    // оказываются за его краем.
    resize(600, 720);

    // Панель длиннее экрана ноутбука, поэтому содержимое живёт в прокрутке, а
    // кнопки — снаружи: иначе «Сохранить» уезжает вниз и его не находят.
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *area = new QScrollArea(this);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget(area);
    auto *page = new QVBoxLayout(canvas);
    page->setContentsMargins(24, 20, 24, 20);
    page->setSpacing(10);

    buildHeader(page);
    buildConnection(page);
    buildGames(page);
    buildSubscription(page);
    buildHelp(page);
    page->addStretch(1);

    area->setWidget(canvas);
    outer->addWidget(area, 1);
    buildButtons(outer);
}

void DialogGreenRhythm::buildHeader(QVBoxLayout *page) {
    auto *title = new QLabel(tr("Зелёный Ритм"), this);
    QFont big = title->font();
    big.setBold(true);
    big.setPointSizeF(big.pointSizeF() * 1.7);
    title->setFont(big);

    // Тот же знак, что на кнопке в панели инструментов. Человек нажал зелёный
    // значок — он же встречает его в шапке: связь кнопки и окна видна без слов.
    auto *mark = new QLabel(this);
    mark->setPixmap(QIcon(QStringLiteral(":/icon/gr-panel.svg")).pixmap(34, 34));

    auto *head = new QHBoxLayout();
    head->setSpacing(12);
    head->addWidget(mark, 0, Qt::AlignVCenter);
    head->addWidget(title, 1, Qt::AlignVCenter);
    page->addLayout(head);

    state = new QLabel(tr("Не подключено"), this);
    QFont f = state->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    state->setFont(f);
    page->addWidget(state);

    stateWhere = new QLabel(QString(), this);
    stateWhere->setWordWrap(true);
    auto pal = stateWhere->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
    stateWhere->setPalette(pal);
    page->addWidget(stateWhere);

    page->addSpacing(14);
}

void DialogGreenRhythm::buildConnection(QVBoxLayout *page) {
    page->addWidget(sectionTitle(this, tr("Подключение")));
    page->addWidget(separator(this));
    page->addSpacing(6);

    page->addWidget(actionRow(
        this, tr("Быстрое подключение"),
        tr("Выберет сервер сам и подключится — не заглядывая в список."),
        tr("Подключить"), [this] { emit connectRequested(); }, true));
    page->addSpacing(10);

    page->addWidget(actionRow(
        this, tr("Резервное подключение"),
        tr("Запасной путь на случай, когда обычные серверы перестают отвечать. "
           "Скорость ниже обычной; игры и звонки через него не идут."),
        tr("Открыть…"), [this] { emit relayRequested(); }));
    page->addSpacing(10);

    autopilotBox = new QCheckBox(tr("Автопилот: сам сменит сервер, если этот перестал отвечать"), this);
    autopilotBox->setCursor(Qt::PointingHandCursor);
    connect(autopilotBox, &QCheckBox::toggled, this, &DialogGreenRhythm::autopilotChanged);
    page->addWidget(autopilotBox);

    page->addSpacing(18);
}

void DialogGreenRhythm::buildGames(QVBoxLayout *page) {
    page->addWidget(sectionTitle(this, tr("Игры и звонки мимо туннеля")));
    page->addWidget(separator(this));
    page->addSpacing(6);

    auto *why = new QLabel(
        tr("Игры и звонки через туннель работают плохо, и это не поломка. Их пакеты идут "
           "кругом через другую страну, а игровые службы не любят, когда адрес меняется "
           "на полпути: список серверов может не грузиться, пинг — показываться прочерком.\n\n"
           "Отмеченные здесь программы пойдут напрямую, со своим настоящим адресом."),
        this);
    why->setWordWrap(true);
    page->addWidget(why);
    page->addSpacing(8);

    bypass = new QPlainTextEdit(this);
    bypass->setPlaceholderText(tr("Пока пусто — нажмите «Выбрать из запущенных…»"));
    bypass->setMinimumHeight(96);
    // Потолок обязателен: без него поле растягивается на всю свободную высоту и
    // забирает панель себе, а список там из двух-трёх строк.
    bypass->setMaximumHeight(112);
    page->addWidget(bypass);
    page->addSpacing(6);

    auto *pick = new QPushButton(tr("Выбрать из запущенных…"), this);
    pick->setCursor(Qt::PointingHandCursor);
    connect(pick, &QPushButton::clicked, this, &DialogGreenRhythm::pickFromRunning);

    auto *hint = new QLabel(
        tr("Запустите игру, потом откройте список: имя файла у каждой игры своё, "
           "и вписанное наугад не совпадёт ни с чем."),
        this);
    hint->setWordWrap(true);
    auto pal = hint->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
    hint->setPalette(pal);

    auto *row = new QHBoxLayout();
    row->setSpacing(16);
    row->addWidget(hint, 1);
    row->addWidget(pick, 0, Qt::AlignTop);
    page->addLayout(row);

    page->addSpacing(18);
}

void DialogGreenRhythm::buildSubscription(QVBoxLayout *page) {
    page->addWidget(sectionTitle(this, tr("Подписка")));
    page->addWidget(separator(this));
    page->addSpacing(6);

    page->addWidget(actionRow(
        this, tr("Продлить или сменить тариф"),
        tr("Откроется страница оплаты в браузере."),
        tr("Открыть"), [this] { emit buyRequested(); }));
    page->addSpacing(10);

    page->addWidget(actionRow(
        this, tr("Перенести на телефон"),
        tr("Код на экране — навести камерой из приложения на телефоне."),
        tr("Показать код"), [this] { emit qrRequested(); }));

    page->addSpacing(18);
}

void DialogGreenRhythm::buildHelp(QVBoxLayout *page) {
    page->addWidget(sectionTitle(this, tr("Если что-то не работает")));
    page->addWidget(separator(this));
    page->addSpacing(6);

    page->addWidget(actionRow(
        this, tr("Что-то не работает"),
        tr("Назовите программу — посмотрю, что с ней происходит, и предложу лечение. "
           "Ничего вписывать не нужно."),
        tr("Разобраться"), [this] { emit troubleRequested(); }, true));
    page->addSpacing(10);

    page->addWidget(actionRow(
        this, tr("Диагностика"),
        tr("Проверит интернет, имена, сервер и проход трафика — и скажет, что именно "
           "сломалось, вместо общего «нет соединения»."),
        tr("Проверить"), [this] { emit diagnosticsRequested(); }));
    page->addSpacing(10);

#ifdef Q_OS_WIN
    page->addWidget(actionRow(
        this, tr("Починить сеть"),
        tr("Вернёт системные настройки и сбросит кэш имён. Чужие туннели не трогает."),
        tr("Починить"), [this] { emit fixNetRequested(); }));
    page->addSpacing(10);

    page->addWidget(actionRow(
        this, tr("Сторонние туннели"),
        tr("Покажет чужие туннели и их маршруты. Ничего не выключает без спроса."),
        tr("Показать"), [this] { emit adaptersRequested(); }));
    page->addSpacing(10);
#endif

    page->addWidget(actionRow(
        this, tr("Поддержка в Telegram"),
        tr("Напишите нам. Отчёт диагностики попросят первым делом — соберите его заранее."),
        tr("Написать"), [this] { emit telegramRequested(); }));
}

void DialogGreenRhythm::buildButtons(QVBoxLayout *outer) {
    auto *bar = new QWidget(this);
    auto *line = new QHBoxLayout(bar);
    line->setContentsMargins(24, 12, 24, 16);

    auto *note = new QLabel(tr("Изменения вступят в силу при следующем подключении."), bar);
    note->setWordWrap(true);
    auto pal = note->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
    note->setPalette(pal);
    line->addWidget(note, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, bar);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Сохранить"));
    buttons->button(QDialogButtonBox::Close)->setText(tr("Закрыть"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        emit bypassChanged(bypass->toPlainText().trimmed());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    line->addWidget(buttons, 0);

    outer->addWidget(bar);
}

void DialogGreenRhythm::setBypassList(const QString &text) {
    bypass->setPlainText(text);
}

QString DialogGreenRhythm::bypassList() const {
    return bypass->toPlainText().trimmed();
}

void DialogGreenRhythm::setAutopilot(bool on) {
    QSignalBlocker block(autopilotBox);
    autopilotBox->setChecked(on);
}

void DialogGreenRhythm::setConnectionState(bool connected, const QString &where) {
    state->setText(connected ? tr("Подключено") : tr("Не подключено"));
    state->setStyleSheet(connected ? QStringLiteral("color: %1;").arg(kAccent)
                                   : QString());
    stateWhere->setText(where);
    stateWhere->setVisible(!where.isEmpty());
}

void DialogGreenRhythm::pickFromRunning() {
    // Перечисление вынесено в main/RunningPrograms: тот же список нужен окну
    // разбора поломок, а два разных списка одних и тех же программ — это две
    // разные правды об одной машине.
    const auto names = GreenRhythm::runningPrograms();
    if (names.isEmpty()) return;
    QDialog d(this);
    d.setWindowTitle(tr("Запущенные программы"));
    d.setMinimumSize(440, 520);
    auto *box = new QVBoxLayout(&d);
    box->setContentsMargins(20, 16, 20, 16);
    box->setSpacing(8);

    auto *note = new QLabel(tr("Отмеченные пойдут мимо туннеля. Здесь только то, что запущено "
                               "прямо сейчас, — поэтому игру надо запустить до открытия списка."),
                            &d);
    note->setWordWrap(true);
    box->addWidget(note);

    auto *filter = new QLineEdit(&d);
    filter->setPlaceholderText(tr("Поиск по названию"));
    filter->setClearButtonEnabled(true);
    box->addWidget(filter);

    auto *list = new QListWidget(&d);
    const auto already = bypass->toPlainText().split(QChar('\n'), Qt::SkipEmptyParts);
    QSet<QString> have;
    for (const auto &line: already) have.insert(line.trimmed().toLower());
    for (const auto &name: names) {
        auto *item = new QListWidgetItem(name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // Уже добавленные отмечены и заметны: иначе человек добавляет одно и то же
        // по второму разу и решает, что список не сохраняется.
        item->setCheckState(have.contains(name.toLower()) ? Qt::Checked : Qt::Unchecked);
    }
    box->addWidget(list, 1);

    connect(filter, &QLineEdit::textChanged, list, [list](const QString &text) {
        for (int i = 0; i < list->count(); i++) {
            list->item(i)->setHidden(!list->item(i)->text().contains(text, Qt::CaseInsensitive));
        }
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Добавить отмеченные"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    box->addWidget(buttons);

    if (d.exec() != QDialog::Accepted) return;
    for (int i = 0; i < list->count(); i++) {
        if (list->item(i)->checkState() == Qt::Checked) addLine(list->item(i)->text());
    }
}

void DialogGreenRhythm::addLine(const QString &name) {
    // Повтор не добавляется: список читает человек, и две одинаковые строки в нём
    // выглядят ошибкой, хотя вреда не несут.
    for (const auto &line: bypass->toPlainText().split(QChar('\n'), Qt::SkipEmptyParts)) {
        if (line.trimmed().compare(name, Qt::CaseInsensitive) == 0) return;
    }
    auto text = bypass->toPlainText().trimmed();
    if (!text.isEmpty()) text += QChar('\n');
    bypass->setPlainText(text + name);
}
