#include "ui/dialog_whatbroke.h"

#include "main/RunningPrograms.hpp"
#include "main/GameCatalog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QScrollArea>
#include <QThread>
#include <QVBoxLayout>
#include <memory>

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
    // finished приходит и при Escape, и при закрытии крестиком, и при accept.
    connect(this, &QDialog::finished, this, [this] { stopWatch(); });

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
    auto *loading = muted(page, tr("Загружаю список программ…"));
    // Process enumeration can wait on PowerShell for ten seconds. Keep it off
    // the GUI thread, including when this window opens for a known game.
    auto names = std::make_shared<QStringList>();
    auto *reader = QThread::create([names] { *names = GreenRhythm::runningPrograms(); });
    connect(reader, &QThread::finished, this, [this, filter, names, loading] {
        for (const auto &name : *names) {
            auto *item = new QListWidgetItem(name, programs);
            item->setHidden(!name.contains(filter->text(), Qt::CaseInsensitive));
        }
        loading->setText(names->isEmpty() ? tr("Список программ недоступен. Игру также можно выбрать в настройках «Игры и программы».") : QString());
        loading->setVisible(names->isEmpty());
    });
    connect(reader, &QThread::finished, reader, &QObject::deleteLater);
    reader->start();
    box->addWidget(programs, 1);
    box->addWidget(loading);

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
    verdictBody->setObjectName("grProgramVerdict");
    verdictBody->setWordWrap(true);
    verdictBody->setAlignment(Qt::AlignTop);
    auto *scroll = new QScrollArea(page); scroll->setObjectName("grProgramVerdictScroll");
    scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(verdictBody); box->addWidget(scroll, 1);

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

void DialogWhatBroke::setSystemNote(const QString &text) {
    if (text == systemNote) return;
    if (pages->currentIndex() == 2) {
        auto body = verdictBody->text();
        const auto separator = QStringLiteral("\n\n— — —\n\n");
        if (!systemNote.isEmpty() && body.endsWith(separator + systemNote))
            body.chop(separator.size() + systemNote.size());
        if (!text.isEmpty()) body += separator + text;
        verdictBody->setText(body);
    }
    systemNote = text;
}

void DialogWhatBroke::inspect(const QString &name) {
    if (name.isEmpty()) return;
    program = name;
    watch.clear();
    observing = true;
    observationStarted = QDateTime::currentDateTimeUtc();
    watchTitle->setText(tr("Смотрю, что делает «%1»").arg(program));
    watchCount->setText(tr("Пока ничего не замечено."));
    pages->setCurrentIndex(1);
}

void DialogWhatBroke::stopWatch() {
    observing = false;
    program.clear();
    watch.clear();
}

void DialogWhatBroke::feed(const QList<GreenRhythm::Seen> &batch) {
    if (!observing) return;
    QList<GreenRhythm::Seen> current;
    for (const auto &seen : batch) {
        // The core retains closed connections for two minutes. They are not
        // evidence of this user's new attempt; still-live older sockets are.
        if (seen.end > 0 && seen.end < observationStarted.toSecsSinceEpoch()) continue;
        current.append(seen);
    }
    watch.add(current);
    const auto f = watch.finish(program);
    const int mine = f.viaTunnel + f.direct;
    QString status = mine == 0
                            ? tr("Пока ничего не замечено.")
                            : tr("Замечено попыток связаться: %1").arg(mine);
    if (watch.truncated()) {
        status += tr("\nУчтены первые %1 соединений. Подведите итог или начните наблюдение заново.")
                      .arg(GreenRhythm::Watch::kMaxRecords);
    }
    watchCount->setText(status);
}

void DialogWhatBroke::conclude() {
    if (!observing) return;
    const auto f = watch.finish(program);
    const bool limited = watch.truncated();
    // Имя ещё нужно кнопке исправления, но ответы ядра больше не собираем.
    observing = false;
    watch.clear();
    const bool covered = alreadyDirect.contains(program, Qt::CaseInsensitive);

    fixButton->setVisible(false);
    fixButton->setEnabled(true);
    applied = false;

    const auto executable = program.section('\\', -1).section('/', -1).toLower();
    if (QStringList{"discord", "discord.exe", "discordptb.exe", "discordcanary.exe"}.contains(executable)) {
        verdictTitle->setText(tr("Наблюдение за Discord: причина ещё не установлена"));
        verdictBody->setText(tr("Замечено соединений через VPN: %1; напрямую: %2; UDP через VPN: %3. "
            "Это сведения о маршруте, а не доказательство доставки пакетов или успешного голосового подключения.\n\n"
            "Если Discord застревает на «Подключение» и отключается от голоса, сохраните время сбоя и отчёт проверки сервисов. "
            "Успешный HTTPS не подтверждает работу голосового UDP и WebSocket. Автоматически выводить Discord напрямую по этим данным нельзя.")
            .arg(f.viaTunnel).arg(f.direct).arg(f.udpViaTunnel));
        if (limited) verdictBody->setText(verdictBody->text() + tr("\n\nДостигнут предел наблюдения; часть соединений не учтена."));
        emit discordObserved(f.viaTunnel, f.direct, f.udpViaTunnel, limited, observationStarted, QDateTime::currentDateTimeUtc());
        pages->setCurrentIndex(2);
        return;
    }

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
            verdictTitle->setText(tr("Замечен прямой маршрут"));
            verdictBody->setText(
                tr("«%1»: замечено %2 соединений с выходом без VPN-сервера, через профиль — 0. "
                   "При включённом TUN трафик всё ещё может обрабатываться внутри него. Доставка данных и причина сбоя этим наблюдением не установлены.")
                    .arg(program)
                    .arg(f.direct));
            break;
        }
        case GreenRhythm::Verdict::ThroughTunnel:
        case GreenRhythm::Verdict::Mixed: {
            verdictTitle->setText(tr("Замечен маршрут через VPN"));
            QString body = tr("«%1» выходит в интернет через VPN — %2 попыток из %3. "
                              "По маршруту нельзя определить, дошли ли данные и чем вызван сбой.")
                               .arg(program)
                               .arg(f.viaTunnel)
                               .arg(f.viaTunnel + f.direct);
            if (f.udpViaTunnel > 0) {
                // Для игр решает именно это: их живой обмен идёт по UDP, и когда
                // в туннель уходит он, ломается ровно то, на что жалуются.
                body += tr("\n\nЗамечены UDP-соединения через VPN: %1. Наличие записи не подтверждает получение ответа.")
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

    if (GreenRhythm::GameCatalog::processes().contains(executable, Qt::CaseInsensitive)) {
        verdictBody->setText(verdictBody->text() + tr("\n\nИгровой UDP: напрямую — %1, через VPN — %2. ")
            .arg(f.udpDirect).arg(f.udpViaTunnel)
            + (f.udpDirect + f.udpViaTunnel == 0
                ? tr("Игровой UDP не замечен: соединения магазина и меню не подтверждают маршрут матча. Начните наблюдение во время игры.")
                : tr("Это маршруты соединений, а не измерение пинга или потерь. Сравнивайте пинг на одном игровом сервере или релее; смена региона меняет путь. Для Dota 2 и CS2 данные игры о задержке и потерях полезнее обычного ping Windows.")));
    } else if (f.verdict != GreenRhythm::Verdict::NotSeen) {
        verdictBody->setText(verdictBody->text() +
                             tr("\n\nОбычная команда ping Windows использует ICMP и не проверяет UDP программы. "
                                "Игры могут измерять задержку своим протоколом; её нельзя определить по числу соединений."));
    }

    if (limited) {
        verdictBody->setText(verdictBody->text() +
                            tr("\n\nНаблюдение ограничено первыми %1 соединениями всей машины. "
                               "Последующие соединения не учтены. При необходимости начните наблюдение заново.")
                                .arg(GreenRhythm::Watch::kMaxRecords));
    }

    // Замечание о машине — последним и всегда: оно не про названную программу,
    // а про условия, в которых ломается любая. Сегодня именно оно и было
    // ответом, а искали его полночи.
    if (!systemNote.isEmpty()) {
        verdictBody->setText(verdictBody->text() + QStringLiteral("\n\n— — —\n\n") + systemNote);
    }

    pages->setCurrentIndex(2);
}
