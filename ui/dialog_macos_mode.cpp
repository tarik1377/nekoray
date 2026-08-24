#include "dialog_macos_mode.h"

#include <QCheckBox>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

    /**
     * Приглушение — ПРОЗРАЧНОСТЬЮ, а не цветом.
     *
     * Тот же приём, что в диалоге активации, и по той же причине: жёсткий цвет
     * подходит ровно к одной теме, а тема здесь бывает и тёмной, и светлой.
     * Прозрачность отсчитывается от того цвета, который тема уже назначила
     * тексту, поэтому работает в обеих.
     */
    void dim(QWidget *w, qreal amount) {
        auto *fade = new QGraphicsOpacityEffect(w);
        fade->setOpacity(amount);
        w->setGraphicsEffect(fade);
    }

    QLabel *line(const QString &text, QWidget *parent) {
        auto *l = new QLabel(text, parent);
        l->setTextFormat(Qt::PlainText);
        l->setWordWrap(true);
        return l;
    }

} // namespace

QWidget *DialogMacosMode::modeCard(const QString &title, const QString &lead,
                                   const QStringList &points, const QString &cost,
                                   const QString &button, Choice value, bool primary) {
    // Рамка, а не голая колонка: два режима — это выбор, и он должен читаться
    // как два предмета, между которыми выбирают, а не как один длинный текст.
    auto *card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);
    auto *lay = new QVBoxLayout(card);
    lay->setSpacing(8);

    const auto base = font();

    auto *head = line(title, card);
    QFont headFont = base;
    headFont.setPointSize(base.pointSize() + 3);
    headFont.setBold(true);
    head->setFont(headFont);
    lay->addWidget(head);

    // Одна фраза о сути — крупнее пунктов, но мельче заголовка. Именно её
    // человек прочитает, если не станет читать ничего больше.
    auto *leadLabel = line(lead, card);
    QFont leadFont = base;
    leadFont.setBold(true);
    leadLabel->setFont(leadFont);
    lay->addWidget(leadLabel);

    QFont small = base;
    small.setPointSize(qMax(base.pointSize() - 1, 7));

    for (const auto &p : points) {
        auto *item = line(QStringLiteral("• ") + p, card);
        item->setFont(small);
        lay->addWidget(item);
    }

    // Отступ перед ценой: без него она читается как ещё один пункт списка
    // достоинств, а это ровно наоборот.
    lay->addSpacing(6);

    // ЦЕНА РЕЖИМА НАЗЫВАЕТСЯ ВСЛУХ и стоит отдельной строкой, а не теряется
    // среди достоинств. Умолчать о ней — значит получить эту же строку в
    // переписке с поддержкой, только через неделю и раздражённо.
    auto *costLabel = line(cost, card);
    costLabel->setFont(small);
    dim(costLabel, 0.72);
    lay->addWidget(costLabel);

    lay->addStretch();

    auto *go = new QPushButton(button, card);
    // ГЛАВНАЯ КНОПКА НАЗНАЧАЕТСЯ ЯВНО. На снимке вёрстки одна из двух вышла
    // акцентной сама по себе — тема красит кнопку по умолчанию, а ею стала
    // просто первая созданная. Получилась рекомендация, которую никто не
    // выбирал. Раз уж выделение всё равно будет, пусть оно достанется туннелю
    // осознанно: это основной режим продукта и ровно то, что делает сборка под
    // Windows. Словом «рекомендуем» при этом ничего не помечено — карточки для
    // того и написаны, чтобы человек решил сам.
    go->setDefault(primary);
    go->setAutoDefault(primary);
    connect(go, &QPushButton::clicked, this, [this, value] {
        chosen = value;
        accept();
    });
    lay->addWidget(go);

    return card;
}

DialogMacosMode::DialogMacosMode(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Как вести трафик"));

    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);

    const auto base = font();

    auto *title = line(tr("Выберите, как вести трафик"), this);
    QFont titleFont = base;
    titleFont.setPointSize(base.pointSize() + 5);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);

    auto *sub = line(tr("Поменять можно в любой момент — это не разовое решение."), this);
    dim(sub, 0.72);
    root->addWidget(sub);

    auto *cards = new QHBoxLayout();
    cards->setSpacing(12);

    cards->addWidget(modeCard(
        tr("Туннель"),
        tr("Через канал идёт всё"),
        {tr("ничего не проходит мимо — ни игры, ни терминал"),
         tr("работает так же, как на Windows")},
        tr("Цена: macOS спрашивает пароль администратора при каждом включении и "
           "выключении. Это ограничение системы — кэша прав между запусками там нет."),
        tr("Включить туннель"), Tunnel, /*primary=*/true));

    cards->addWidget(modeCard(
        tr("Системный прокси"),
        tr("Через канал — браузеры и обычные программы"),
        {tr("пароль не спрашивается вовсе"),
         tr("домашняя сеть и принтер остаются доступны")},
        tr("Цена: терминал (curl, git, ssh), Docker и часть игр пойдут МИМО канала — "
           "они системный прокси не уважают."),
        tr("Включить прокси"), SystemProxy, /*primary=*/false));

    root->addLayout(cards);

    // Про соседний туннель — здесь же. Это второй по частоте вопрос после
    // «почему просит пароль», и отвечать на него в переписке дороже, чем
    // строкой на экране.
    auto *neighbour = line(
        tr("Если рядом поднят туннель до дома или до работы — мы его не выключаем. "
           "Приложение покажет найденное в «Диагностике соединения» и ничего не "
           "тронет без вашего согласия."),
        this);
    QFont small = base;
    small.setPointSize(qMax(base.pointSize() - 1, 7));
    neighbour->setFont(small);
    dim(neighbour, 0.62);
    root->addWidget(neighbour);

    auto *later = new QPushButton(tr("Решу позже"), this);
    connect(later, &QPushButton::clicked, this, [this] {
        reject();
    });
    auto *bottom = new QHBoxLayout();
    bottom->addStretch();
    bottom->addWidget(later);
    root->addLayout(bottom);
}
