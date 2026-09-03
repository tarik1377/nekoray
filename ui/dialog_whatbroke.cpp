#include "ui/dialog_whatbroke.h"

#include "main/RunningPrograms.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

    QLabel *muted(QWidget *parent, const QString &text) {
        auto *l = new QLabel(text, parent);
        l->setWordWrap(true);
        auto pal = l->palette();
        pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
        l->setPalette(pal);
        return l;
    }

    QLabel *heading(QWidget *parent, const QString &text) {
        auto *l = new QLabel(text, parent);
        l->setWordWrap(true);
        QFont f = l->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 1.25);
        l->setFont(f);
        return l;
    }

} // namespace

DialogWhatBroke::DialogWhatBroke(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Что-то не работает"));
    resize(560, 520);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 20, 24, 16);
    outer->setSpacing(12);

    pages = new QStackedWidget(this);
    outer->addWidget(pages, 1);

    buildPick();
    buildWatch();
    buildVerdict();

    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    close->button(QDialogButtonBox::Close)->setText(tr("Закрыть"));
    connect(close, &QDialogButtonBox::rejected, this, [this] {
        stopWatch();
        reject();
    });
    outer->addWidget(close);
}

void DialogWhatBroke::buildPick() {
    auto *page = new QWidget(pages);
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(10);

    box->addWidget(heading(page, tr("Что именно не работает?")));
    box->addWidget(muted(page, tr("Выберите программу, с которой беда. Запустите её заранее — "
                                  "здесь показано только то, что работает прямо сейчас.")));

    auto *filter = new QLineEdit(page);
    filter->setPlaceholderText(tr("Поиск по названию"));
    filter->setClearButtonEnabled(true);
    box->addWidget(filter);

    programs = new QListWidget(page);
    for (const auto &name: GreenRhythm::runningPrograms()) new QListWidgetItem(name, programs);
    box->addWidget(programs, 1);

    connect(filter, &QLineEdit::textChanged, programs, [this](const QString &text) {
        for (int i = 0; i < programs->count(); i++) {
            programs->item(i)->setHidden(!programs->item(i)->text().contains(text, Qt::CaseInsensitive));
        }
    });

    begin = new QPushButton(tr("Посмотреть, что с ней происходит"), page);
    begin->setEnabled(false);
    begin->setCursor(Qt::PointingHandCursor);
    connect(programs, &QListWidget::itemSelectionChanged, this,
            [this] { begin->setEnabled(programs->currentItem() != nullptr); });
    connect(programs, &QListWidget::itemDoubleClicked, this, [this] { if (programs->currentItem()) inspect(programs->currentItem()->text()); });
    connect(begin, &QPushButton::clicked, this, [this] { if (programs->currentItem()) inspect(programs->currentItem()->text()); });
    box->addWidget(begin);

    pages->addWidget(page);
}

void DialogWhatBroke::buildWatch() {
    auto *page = new QWidget(pages);
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(10);

    watchTitle = heading(page, QString());
    box->addWidget(watchTitle);

    // Инструкция обязана стоять ДО ожидания: без неё человек молча смотрит на
    // счётчик, программа в это время ничего не делает, и разбор заканчивается
    // ветвью «ничего не увидели» — самой бесполезной из всех.
    box->addWidget(muted(page, tr("Переключитесь в программу и нажмите в ней то, что не выходит: "
                                  "обновите список серверов, наберите собеседника, откройте страницу.\n\n"
                                  "Это окно можно свернуть — наблюдение не прервётся. Возвращайтесь, "
                                  "когда попробуете.")));

    watchCount = new QLabel(page);
    box->addWidget(watchCount);
    box->addStretch(1);

    auto *done = new QPushButton(tr("Я попробовал — покажите, что видно"), page);
    done->setCursor(Qt::PointingHandCursor);
    connect(done, &QPushButton::clicked, this, [this] { conclude(); });
    box->addWidget(done);

    pages->addWidget(page);
}

void DialogWhatBroke::buildVerdict() {
    auto *page = new QWidget(pages);
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(10);

    verdictTitle = heading(page, QString());
    box->addWidget(verdictTitle);

    verdictBody = new QLabel(page);
    verdictBody->setWordWrap(true);
    box->addWidget(verdictBody);
    box->addStretch(1);

    fixButton = new QPushButton(page);
    fixButton->setCursor(Qt::PointingHandCursor);
    connect(fixButton, &QPushButton::clicked, this, [this] {
        if (applied) {
            emit reconnectRequested();
            fixButton->setEnabled(false);
            verdictBody->setText(verdictBody->text() + tr("\n\nПереподключаемся. Потом снова нажмите "
                                                          "в программе то, что не выходило."));
            return;
        }
        emit fixRequested(program);
        applied = true;
        fixButton->setText(tr("Переподключить"));
        verdictTitle->setText(tr("Готово — осталось переподключиться"));
        verdictBody->setText(tr("«%1» больше не будет выходить через VPN. Правила задаются в момент "
                                "подключения, поэтому нужно переподключиться — связь прервётся "
                                "на пару секунд.")
                                 .arg(program));
    });
    box->addWidget(fixButton);

    auto *again = new QPushButton(tr("Выбрать другую программу"), page);
    connect(again, &QPushButton::clicked, this, [this] {
        stopWatch();
        applied = false;
        pages->setCurrentIndex(0);
    });
    box->addWidget(again);

    pages->addWidget(page);
}

void DialogWhatBroke::setFixAvailable(bool can, const QString &whyNot) {
    canFix = can;
    cannotFixWhy = whyNot;
}

void DialogWhatBroke::inspect(const QString &name) {
    if (name.isEmpty()) return;
    program = name;
    watch.clear();
    watchTitle->setText(tr("Смотрю, что делает «%1»").arg(program));
    watchCount->setText(tr("Пока ничего не замечено."));
    pages->setCurrentIndex(1);
}

void DialogWhatBroke::stopWatch() {
    program.clear();
    watch.clear();
}

void DialogWhatBroke::feed(const QList<GreenRhythm::Seen> &batch) {
    if (program.isEmpty()) return;
    watch.add(batch);
    const auto f = watch.finish(program);
    const int mine = f.viaTunnel + f.direct;
    watchCount->setText(mine == 0
                            ? tr("Пока ничего не замечено.")
                            : tr("Замечено попыток связаться: %1").arg(mine));
}

void DialogWhatBroke::conclude() {
    const auto f = watch.finish(program);
    const bool covered = alreadyDirect.contains(program, Qt::CaseInsensitive);

    fixButton->setVisible(false);
    fixButton->setEnabled(true);
    applied = false;

    switch (f.verdict) {
        case GreenRhythm::Verdict::NotSeen: {
            verdictTitle->setText(tr("Ничего не увидел"));
            QString body = tr("За это время «%1» ни разу не пыталась связаться. Скорее всего "
                              "она ещё не пробовала — вернитесь в неё и нажмите то, что не выходит.")
                               .arg(program);
            if (!f.companions.isEmpty()) {
                // Частый случай: назвали лаунчер, а в туннель ходит сама игра.
                body += tr("\n\nЗато через VPN сейчас выходят: %1. Может быть, дело в одной из них?")
                            .arg(f.companions.join(QStringLiteral(", ")));
            }
            verdictBody->setText(body);
            break;
        }
        case GreenRhythm::Verdict::Direct: {
            verdictTitle->setText(tr("С нашей стороны всё в порядке"));
            verdictBody->setText(
                tr("«%1» и так выходит в интернет напрямую, минуя нас: %2 попыток, и ни одна "
                   "не шла через VPN. Значит причина не в подключении — стоит проверить саму "
                   "программу, её сервер или интернет.")
                    .arg(program)
                    .arg(f.direct));
            break;
        }
        case GreenRhythm::Verdict::ThroughTunnel:
        case GreenRhythm::Verdict::Mixed: {
            verdictTitle->setText(tr("Нашёл причину"));
            QString body = tr("«%1» выходит в интернет через VPN — %2 попыток из %3. "
                              "Её пакеты идут кругом через другую страну, и службы, которым важен "
                              "постоянный адрес, на это отвечают плохо.")
                               .arg(program)
                               .arg(f.viaTunnel)
                               .arg(f.viaTunnel + f.direct);
            if (f.udpViaTunnel > 0) {
                // Для игр решает именно это: их живой обмен идёт по UDP, и когда
                // в туннель уходит он, ломается ровно то, на что жалуются.
                body += tr("\n\nПричём через VPN идёт и живой обмен с сервером (%1) — "
                           "у игр от этого не грузится список серверов и не считается пинг.")
                            .arg(f.udpViaTunnel);
            }
            if (!canFix) {
                // Причина названа, кнопки нет: предложить починку, которая ничего
                // не изменит, — худшее из возможного, потому что второй заход
                // скажет «уже в списке» и замкнёт человека в петле.
                body += QStringLiteral("\n\n") + cannotFixWhy;
            } else if (covered) {
                body += tr("\n\nНо эта программа уже в списке тех, кто выходит напрямую. Значит менять "
                           "нечего — похоже, изменения ещё не вступили в силу: переподключитесь.");
                fixButton->setText(tr("Переподключить"));
                applied = true;
            } else {
                body += tr("\n\nМожно пустить её напрямую, со своим настоящим адресом. Остальное "
                           "останется под защитой.");
                fixButton->setText(tr("Пустить эту программу напрямую"));
            }
            verdictBody->setText(body);
            fixButton->setVisible(canFix);
            break;
        }
    }

    // Про проверки связи говорим отдельно и всегда: к программе они не
    // привязываются в принципе, и обещать обратное нельзя.
    if (f.verdict != GreenRhythm::Verdict::NotSeen) {
        verdictBody->setText(verdictBody->text() +
                             tr("\n\nЕсли в игре не считается пинг — это чинится отдельно и сразу "
                                "для всех программ: у проверок связи нет владельца, и разобрать их "
                                "по программам нельзя."));
    }

    pages->setCurrentIndex(2);
}
