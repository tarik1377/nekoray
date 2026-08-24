#include "dialog_relay_activate.h"
#include "ui_dialog_relay_activate.h"

#include "main/DeviceCredentials.hpp"
#include "main/NekoGui_Utils.hpp"
#include "main/RelayActivation.hpp"

#include <QDesktopServices>
#include <QMessageBox>
#include <QUrl>

namespace {
    /**
     * Состояние — своими словами и с действием, а не кодом ответа.
     *
     * «402» человеку не говорит ничего; «подписка закончилась» плюс кнопка
     * «Продлить» отвечают и на «что случилось», и на «что делать». Это тот же
     * приём, что на Android, и заведён он там ровно потому, что поддержка
     * тонула в скриншотах с числами.
     */
    QString stateLine() {
        const auto detail = DeviceCredentials::StateDetail();
        switch (DeviceCredentials::CurrentState()) {
            case DeviceCredentials::Active:
                return QObject::tr("Подключено к вашей подписке.");
            case DeviceCredentials::Expired:
                return detail.isEmpty() ? QObject::tr("Подписка закончилась.") : detail;
            case DeviceCredentials::Limit:
                return detail.isEmpty() ? QObject::tr("Достигнут лимит устройств по тарифу.") : detail;
            case DeviceCredentials::Closed:
                return detail.isEmpty() ? QObject::tr("Пока не открыто для вашего аккаунта.") : detail;
            case DeviceCredentials::SignedOut:
            case DeviceCredentials::Unknown:
            default:
                return QObject::tr("Не активировано на этом устройстве.");
        }
    }
} // namespace

DialogRelayActivate::DialogRelayActivate(QWidget *parent) : QDialog(parent), ui(new Ui::DialogRelayActivate) {
    ui->setupUi(this);

    // Код на сайте — восемь знаков в верхнем регистре. Приводим на лету, чтобы
    // человек, вставивший его строчными, не получил «код не подошёл» от того,
    // с чем мы справились бы сами.
    connect(ui->code, &QLineEdit::textChanged, this, [this](const QString &t) {
        const auto tidy = t.trimmed().toUpper();
        if (tidy != t) {
            const int at = ui->code->cursorPosition();
            ui->code->setText(tidy);
            ui->code->setCursorPosition(at);
        }
    });
    connect(ui->code, &QLineEdit::returnPressed, ui->activate, &QPushButton::click);

    connect(ui->getCode, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(RelayActivation::ProfileUrl()));
    });

    connect(ui->activate, &QPushButton::clicked, this, [this] {
        if (busy) return;
        const auto code = ui->code->text().trimmed();
        if (code.isEmpty()) {
            showResult(tr("Введите код из личного кабинета."), true);
            showAction(tr("Взять код"), RelayActivation::ProfileUrl());
            ui->code->setFocus();
            return;
        }

        setBusy(true);
        showResult(tr("Проверяем код…"), false);
        showAction({}, {});

        // В отдельном потоке: оба запроса блокирующие, а замерший на десять
        // секунд диалог человек считает зависшим и закрывает — посреди обмена
        // одноразового кода, который после этого уже потрачен.
        runOnNewThread([this, code] {
            auto out = RelayActivation::Redeem(code);
            if (out.ok) out = RelayActivation::Provision();

            runOnUiThread([this, out] {
                setBusy(false);
                repaintState();
                if (out.ok) {
                    showResult(tr("Готово. Резервное подключение активировано."), false);
                    showAction({}, {});
                    ui->code->clear();
                    return;
                }
                showResult(out.detail, true);
                showAction(out.actionText, out.actionUrl);
            }, this);
        });
    });

    connect(ui->forget, &QPushButton::clicked, this, [this] {
        if (busy) return;
        // Переспрашиваем: реквизиты после этого придётся получать новым кодом,
        // а обратной кнопки нет.
        QMessageBox ask(QMessageBox::Question, tr("Отключить резервное подключение"),
                        tr("Ключи этого устройства будут забыты. Чтобы включить снова, "
                           "понадобится новый код из личного кабинета.\n\nОтключить?"),
                        QMessageBox::NoButton, this);
        auto *go = ask.addButton(tr("Отключить"), QMessageBox::AcceptRole);
        ask.addButton(tr("Отмена"), QMessageBox::RejectRole);
        ask.exec();
        if (ask.clickedButton() != go) return;

        RelayActivation::Forget();
        repaintState();
        showResult(tr("Отключено. Ключи этого устройства забыты."), false);
        showAction({}, {});
    });

    connect(ui->close, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->action, &QPushButton::clicked, this, [this] {
        if (!actionUrl.isEmpty()) QDesktopServices::openUrl(QUrl(actionUrl));
    });

    repaintState();
    ui->code->setFocus();
}

DialogRelayActivate::~DialogRelayActivate() { delete ui; }

void DialogRelayActivate::repaintState() {
    ui->state->setText(stateLine());

    const bool on = DeviceCredentials::IsProvisioned();
    ui->forget->setEnabled(on);
    // Ввод кода не прячется у активированного: перенести устройство на другой
    // аккаунт — обычное дело, и заставлять ради этого сначала «отключить»
    // значит требовать двух шагов там, где хватает одного.
    ui->activate->setText(on ? tr("Активировать заново") : tr("Активировать"));
}

void DialogRelayActivate::setBusy(bool on) {
    busy = on;
    ui->activate->setEnabled(!on);
    ui->forget->setEnabled(!on && DeviceCredentials::IsProvisioned());
    ui->code->setEnabled(!on);
}

/**
 * Действие показывается КНОПКОЙ, а не ссылкой в тексте.
 *
 * Ссылка была первой попыткой, и на снимке вёрстки сразу стало видно, во что
 * она превращается: серый текст на тёмном фоне, неотличимый от выключенного, —
 * то есть человек с кончившейся подпиской не увидел бы, что продлить можно
 * отсюда. Цвет ссылки задаёт палитра, а тема у приложения своя и вдобавок
 * бывает светлой; полагаться на неё в единственном действии экрана нельзя.
 * Кнопку тема оформляет сама, и выглядит она действием, потому что действие и
 * есть.
 */
void DialogRelayActivate::showAction(const QString &text, const QString &url) {
    actionUrl = url;
    ui->action->setText(text);
    ui->action->setVisible(!text.isEmpty() && !url.isEmpty());
}

void DialogRelayActivate::showResult(const QString &text, bool bad) {
    ui->result->setTextFormat(Qt::RichText);
    // ОТКАЗ ВЫДЕЛЯЕТСЯ НАЧЕРТАНИЕМ, А НЕ ЦВЕТОМ, и это не вкусовщина.
    //
    // Своего цвета для ошибки тема проекта не определяет (modern.css знает
    // текст #e4e6eb, приглушённый #9aa0a8 и зелёный акцент #3fb950), а жёсткий
    // #RRGGBB подошёл бы только к одной из тем: приложение умеет и светлую.
    // Первый вариант брал palette(link-visited) — на тёмной теме получился
    // серый, неотличимый от выключенного, и ссылка «Продлить» рядом читалась
    // как недоступная. Полужирный работает в обеих темах и ничего не обещает
    // про палитру.
    ui->result->setText(bad ? QStringLiteral("<b>%1</b>").arg(text) : text);
}
