#include "GameProgramsDialog.hpp"
#include "main/GameAppRules.hpp"
#include "main/GameCatalog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScreen>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace GreenRhythm {
GameProgramsDialog::GameProgramsDialog(const QString &saved, bool tunnelEnabled, QWidget *parent)
    : QDialog(parent), raw(saved) {
    setWindowTitle(tr("Игры и программы"));
    const auto *display = QGuiApplication::primaryScreen();
    const auto available = (display ? display->availableGeometry().size() : QSize(800, 600)) - QSize(32, 64);
    const QSize initial(qMax(320, qMin(680, available.width())), qMax(280, qMin(560, available.height())));
    resize(initial); setMinimumSize(qMin(480, initial.width()), qMin(380, initial.height()));
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(12, 12, 12, 12);
    scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget(scroll);
    auto *layout = new QVBoxLayout(canvas);
    layout->setContentsMargins(20, 20, 20, 20); layout->setSpacing(12);
    auto *intro = new QLabel(tr("Выберите, как подключать игру. В стандартной схеме игры идут напрямую, Discord — через VPN. "
                               "Свой выбор здесь имеет приоритет над общими правилами."), this);
    intro->setWordWrap(true); layout->addWidget(intro);
    auto *search = new QLineEdit(this); search->setObjectName("gameSearch");
    search->setPlaceholderText(tr("Найти игру или программу")); search->setClearButtonEnabled(true);
    search->setAccessibleName(tr("Поиск игры или программы")); layout->addWidget(search);
    auto *tools = new QHBoxLayout;
    auto *running = new QPushButton(tr("Из запущенных…"), this); running->setObjectName("gameRunning");
    auto *file = new QPushButton(tr("Выбрать .exe…"), this); file->setObjectName("gameFile");
    tools->addWidget(running); tools->addWidget(file); tools->addStretch(); layout->addLayout(tools);
    table = new QTableWidget(0, 2, this); table->setObjectName("gamePrograms");
    table->setHorizontalHeaderLabels({tr("Игра или программа"), tr("Подключение")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->verticalHeader()->hide(); table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table, 1);
    status = new QLabel(this); status->setObjectName("gameStatus"); status->setWordWrap(true); layout->addWidget(status);
    auto *hint = new QLabel(tunnelEnabled
        ? tr("Выбор сохранится для следующего подключения. Текущее соединение продолжит работать.")
        : tr("Эти правила действуют во встроенном TUN Windows. Включите туннель при следующем подключении."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto *names = new QLabel(tr("Правило действует для всех процессов с выбранным именем файла. "
                               "«Общие правила» убирает только выбор из этого окна. Оптимизация игрового UDP сохраняет обработку DNS."), this);
    names->setWordWrap(true); layout->addWidget(names);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    save = buttons->button(QDialogButtonBox::Save); save->setObjectName("gameSave");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    scroll->setWidget(canvas); outer->addWidget(scroll, 1); outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &GameProgramsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QString error;
    valid = GameAppRules::parse(saved, &original, &error); draft = original;
    if (!valid) {
        status->setText(error); running->setEnabled(false); file->setEnabled(false); table->setEnabled(false);
        auto *repair = new QPushButton(tr("Сбросить только выбор программ"), this); repair->setObjectName("gameRepair");
        layout->addWidget(repair);
        connect(repair, &QPushButton::clicked, this, [=] {
            valid = true; repaired = true; status->clear(); table->setEnabled(true);
            running->setEnabled(true); file->setEnabled(true); repair->hide(); updateSave();
        });
    }
    for (const auto &entry : GameCatalog::games()) addRow(entry.title, entry.processes);
    addRow("Steam", GameCatalog::steam());
    addRow("Discord", {"Discord.exe", "discord.exe", "DiscordPTB.exe", "DiscordCanary.exe"});
    for (const auto &name : original.keys()) {
        bool found = false;
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->data(Qt::UserRole).toStringList().contains(name, Qt::CaseInsensitive)) found = true;
        if (!found) addRow(name, {name});
    }
    connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto *item = table->item(row, 0);
            table->setRowHidden(row, !item->text().contains(text, Qt::CaseInsensitive)
                && !item->data(Qt::UserRole).toStringList().join(' ').contains(text, Qt::CaseInsensitive));
        }
    });
    connect(file, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Выберите исполняемый файл игры"), {}, tr("Программы Windows (*.exe)"));
        if (!path.isEmpty()) addExecutable(path);
    });
    connect(running, &QPushButton::clicked, this, &GameProgramsDialog::chooseRunning);
    updateSave();
}

void GameProgramsDialog::addRow(const QString &title, const QStringList &programs) {
    const int row = table->rowCount(); table->insertRow(row);
    auto *item = new QTableWidgetItem(title); item->setData(Qt::UserRole, programs);
    item->setToolTip(programs.join('\n')); table->setItem(row, 0, item);
    auto *choice = new QComboBox(table); choice->setAccessibleName(tr("Подключение: %1").arg(title));
    choice->addItem(tr("Общие правила"), "auto"); choice->addItem(tr("Напрямую"), "bypass"); choice->addItem(tr("Через VPN"), "proxy");
    auto current = GameAppRules::mode(draft, programs.first());
    for (const auto &name : programs) if (GameAppRules::mode(draft, name) != current) current = "mixed";
    if (current == "mixed") choice->addItem(tr("Разные правила"), "mixed");
    choice->setCurrentIndex(choice->findData(current)); table->setCellWidget(row, 1, choice);
    table->setRowHeight(row, qMax(42, choice->sizeHint().height() + 8));
    connect(choice, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, choice, programs] {
        const auto route = choice->currentData().toString(); if (route == "mixed") return;
        auto candidate = draft;
        for (const auto &name : programs) if (!GameAppRules::setMode(&candidate, name, route)) {
            status->setText(tr("Не удалось добавить программу. Допустимо до 256 имён .exe."));
            QSignalBlocker blocker(choice);
            auto previous = GameAppRules::mode(draft, programs.first());
            for (const auto &other : programs) if (GameAppRules::mode(draft, other) != previous) previous = "mixed";
            choice->setCurrentIndex(choice->findData(previous)); return;
        }
        draft = candidate; status->clear(); updateSave();
    });
}

bool GameProgramsDialog::addExecutable(const QString &path) {
    const auto name = QFileInfo(path).fileName();
    if (!valid || !GameAppRules::executableName(name)) { status->setText(tr("Выберите файл программы с расширением .exe.")); return false; }
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->item(row, 0)->data(Qt::UserRole).toStringList().contains(name, Qt::CaseInsensitive)) continue;
        table->setRowHidden(row, false); table->selectRow(row); table->scrollToItem(table->item(row, 0));
        auto *choice = static_cast<QComboBox *>(table->cellWidget(row, 1)); choice->setCurrentIndex(choice->findData("bypass"));
        return true;
    }
    addRow(name, {name});
    return addExecutable(name);
}

QString GameProgramsDialog::serializedRules() const { return changed() ? GameAppRules::serialize(draft) : raw; }
void GameProgramsDialog::updateSave() { save->setText(changed() ? tr("Сохранить") : tr("Готово")); }
void GameProgramsDialog::accept() {
    if (!valid) { reject(); return; }
    if (changed() && commit) {
        QString error;
        if (!commit(serializedRules(), &error)) { status->setText(error); scroll->ensureWidgetVisible(status); return; }
    }
    QDialog::accept();
}

void GameProgramsDialog::chooseRunning() {
    QDialog picker(this); picker.setWindowTitle(tr("Запущенные программы")); picker.resize(460, 440);
    auto *layout = new QVBoxLayout(&picker);
    auto *search = new QLineEdit(&picker); search->setPlaceholderText(tr("Найти программу")); layout->addWidget(search);
    auto *message = new QLabel(tr("Получаю список программ…"), &picker); message->setWordWrap(true); layout->addWidget(message);
    auto *list = new QListWidget(&picker); list->setObjectName("gameRunningList"); layout->addWidget(list, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &picker);
    auto *ok = buttons->button(QDialogButtonBox::Ok); ok->setText(tr("Добавить напрямую")); ok->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена")); layout->addWidget(buttons);
    auto fill = [=](const QStringList &programs) {
        QSet<QString> seen;
        for (auto name : programs) {
            name = name.trimmed(); if (!name.endsWith(".exe", Qt::CaseInsensitive)) name += ".exe";
            if (!GameAppRules::executableName(name) || seen.contains(name.toCaseFolded())) continue;
            seen.insert(name.toCaseFolded()); list->addItem(name);
        }
        list->sortItems(); message->setText(list->count() ? tr("Выберите именно процесс игры, затем добавьте его напрямую.")
                                                                       : tr("Список недоступен. Можно выбрать файл .exe в предыдущем окне."));
    };
    connect(search, &QLineEdit::textChanged, &picker, [list, ok](const QString &text) {
        for (int i = 0; i < list->count(); ++i) list->item(i)->setHidden(!list->item(i)->text().contains(text, Qt::CaseInsensitive));
        ok->setEnabled(list->currentItem() && !list->currentItem()->isHidden());
    });
    connect(list, &QListWidget::currentRowChanged, &picker, [list, ok] { ok->setEnabled(list->currentItem() && !list->currentItem()->isHidden()); });
    connect(buttons, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
    QProcess process; QTimer timeout; timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &picker, [&] { process.kill(); message->setText(tr("Получение списка заняло слишком долго. Выберите файл .exe.")); });
    connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &picker, [&](int code, QProcess::ExitStatus exit) {
        timeout.stop();
        if (code == 0 && exit == QProcess::NormalExit) fill(QString::fromUtf8(process.readAllStandardOutput()).split('\n'));
        else message->setText(tr("Не удалось получить список. Выберите файл .exe."));
    });
    connect(&process, &QProcess::errorOccurred, &picker, [&](QProcess::ProcessError) { timeout.stop(); message->setText(tr("Не удалось получить список. Выберите файл .exe.")); });
    if (runningProvider) fill(runningProvider());
    else {
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; }); // CREATE_NO_WINDOW
        const auto powershell = qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0/powershell.exe";
        process.start(powershell, {"-NoProfile", "-NonInteractive", "-Command", "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); Get-Process | ForEach-Object { $_.ProcessName } | Sort-Object -Unique"});
        timeout.start(10000);
#else
        fill({});
#endif
    }
    const bool accepted = picker.exec() == QDialog::Accepted;
    timeout.stop(); if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1500); }
    if (accepted && list->currentItem() && !list->currentItem()->isHidden()) addExecutable(list->currentItem()->text());
}
} // namespace GreenRhythm
