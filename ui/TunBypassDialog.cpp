#include "TunBypassDialog.hpp"
#include "main/TunBypass.hpp"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollArea>

namespace GreenRhythm {
TunBypassDialog::TunBypassDialog(const QString &saved, QWidget *parent) : QDialog(parent), original(saved) {
    setWindowTitle(tr("Рабочие VPN и виртуальные машины"));
    const auto *screen = QGuiApplication::primaryScreen();
    const auto available = screen ? screen->availableGeometry().size() : QSize(800, 600);
    resize(qMax(320, qMin(560, available.width() - 32)), qMax(280, qMin(500, available.height() - 64)));
    auto *outer = new QVBoxLayout(this);
    scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget(scroll);
    auto *layout = new QVBoxLayout(canvas);
    auto label = [&](const QString &text) { auto *item = new QLabel(text, this); item->setWordWrap(true); layout->addWidget(item); };
    label(tr("Добавьте адрес шлюза рабочего VPN. Он пойдёт мимо TUN через обычные маршруты Windows, в том числе из VMware NAT. Это отличается от правила «Напрямую» внутри туннеля."));
    editor = new QPlainTextEdit(saved, this); editor->setObjectName("tunBypassEntries");
    editor->setMinimumHeight(140);
    editor->setPlaceholderText(tr("Домен, IP или CIDR, каждый с новой строки\nНапример: vpn.company.ru\n212.164.137.29/32"));
    editor->setAccessibleName(tr("Шлюзы и сети мимо TUN")); layout->addWidget(editor, 1);
    label(tr("IP доменов определяются системным DNS заново перед каждым подключением. Неответивший DNS остановит запуск TUN с ошибкой. Исключение действует на все порты и сервисы полученного IP."));
    label(tr("Сохранение не прерывает связь. Изменения вступят в силу при следующем подключении. Введённые сети и прежние исключения останутся в этом списке."));
    status = new QLabel(this); status->setObjectName("tunBypassStatus"); status->setWordWrap(true); layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    save = buttons->button(QDialogButtonBox::Save); save->setObjectName("tunBypassSave"); save->setText(tr("Готово"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    scroll->setWidget(canvas); outer->addWidget(scroll, 1); outer->addWidget(buttons);
    connect(editor, &QPlainTextEdit::textChanged, this, [this] { save->setText(editor->toPlainText() == original ? tr("Готово") : tr("Сохранить")); status->clear(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &TunBypassDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
void TunBypassDialog::accept() {
    const auto text = editor->toPlainText();
    if (text != original) {
        QString error; QStringList entries;
        if (!TunBypass::parse(text, &entries, &error) || (commit && !commit(text, &error))) { status->setText(error); editor->setFocus(); scroll->ensureWidgetVisible(status); return; }
    }
    QDialog::accept();
}
}
