#include "dialog_service_health.h"
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

DialogServiceHealth::DialogServiceHealth(QWidget *parent, const QList<GreenRhythm::HealthTarget> &targets)
    : QDialog(parent), probeTargets(targets) {
    setObjectName("grServiceHealth"); setWindowTitle(tr("Проверка подключения и сервисов")); resize(740, 650);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Проверка не прерывает подключение и не меняет маршруты. Повторные запросы помогут увидеть кратковременные сбои. Отчёт можно скопировать в поддержку; автоматически он никуда не отправляется."), this);
    intro->setWordWrap(true); layout->addWidget(intro);
    service = new QComboBox(this); service->setObjectName("grHealthService");
    service->addItem(tr("Подписка и подключение"), "general"); service->addItem(tr("Discord: доступ и проблема с голосом"), "discord");
    layout->addWidget(service);
    stability = new QCheckBox(tr("Наблюдать дольше — 10 серий запросов вместо 3"), this); stability->setObjectName("grHealthStability"); layout->addWidget(stability);
    auto *actions = new QHBoxLayout;
    start = new QPushButton(tr("Начать проверку"), this); start->setObjectName("grHealthStart");
    cancel = new QPushButton(tr("Остановить проверку"), this); cancel->setObjectName("grHealthCancel"); cancel->setEnabled(false);
    actions->addWidget(start); actions->addWidget(cancel); layout->addLayout(actions);
    progress = new QProgressBar(this); progress->setObjectName("grHealthProgress"); progress->setRange(0, 1); progress->setValue(0); layout->addWidget(progress);
    verdict = new QLabel(tr("Выберите проверку и нажмите «Начать проверку»."), this);
    verdict->setTextFormat(Qt::PlainText); verdict->setWordWrap(true); verdict->setObjectName("grHealthVerdict"); layout->addWidget(verdict);
    results = new QPlainTextEdit(this); results->setObjectName("grHealthResults"); results->setReadOnly(true); layout->addWidget(results, 1);
    observe = new QPushButton(tr("Наблюдать за Discord во время попытки войти в голос"), this); observe->setObjectName("grHealthObserveDiscord"); observe->hide(); observe->setEnabled(false);
    observe->setToolTip(tr("Сначала завершите проверку подключения. Затем наблюдение дополнит тот же отчёт.")); layout->addWidget(observe);
    auto *note = new QLabel(tr("Для Discord успешный HTTPS не подтверждает работу голоса. Наблюдение покажет замеченные соединения программы, но само по себе не измеряет доставку UDP. По одной машине нельзя достоверно отличить сбой провайдера от общего сбоя сервера."), this);
    note->setWordWrap(true); layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(this);
    copy = buttons->addButton(tr("Скопировать отчёт без ключей"), QDialogButtonBox::ActionRole); copy->setObjectName("grHealthCopy"); copy->setEnabled(false);
    buttons->addButton(tr("Закрыть"), QDialogButtonBox::RejectRole); layout->addWidget(buttons);
    health = new GreenRhythm::ServiceHealth(this);
    connect(start, &QPushButton::clicked, this, &DialogServiceHealth::begin);
    connect(cancel, &QPushButton::clicked, health, &GreenRhythm::ServiceHealth::cancel);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::finished, health, &GreenRhythm::ServiceHealth::cancel);
    connect(service, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const auto report = health->report();
        const bool matches = report.value("service").toString() == service->currentData().toString()
            && !report.value("finished_utc").toString().isEmpty();
        observe->setVisible(service->currentData().toString() == "discord");
        observe->setEnabled(matches && report.value("profile_running").toBool() && !report.value("stale").toBool()
                            && !report.value("cancelled").toBool());
        copy->setEnabled(matches);
    });
    connect(observe, &QPushButton::clicked, this, &DialogServiceHealth::observeDiscordRequested);
    connect(copy, &QPushButton::clicked, this, [this] {
        if (contextStillCurrent && !contextStillCurrent()) health->invalidate();
        QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(health->report()).toJson()));
    });
    connect(health, &GreenRhythm::ServiceHealth::updated, this, [this](int done, int total) { progress->setRange(0, total); progress->setValue(done); refresh(); });
    connect(health, &GreenRhythm::ServiceHealth::completed, this, [this] {
        if (contextStillCurrent && !contextStillCurrent()) health->invalidate();
        contextTimer.stop(); start->setEnabled(true); service->setEnabled(true); stability->setEnabled(true);
        cancel->setEnabled(false); copy->setEnabled(true); refresh();
        observe->setEnabled(health->report().value("profile_running").toBool() && !health->report().value("stale").toBool()
                            && !health->report().value("cancelled").toBool());
    });
    contextTimer.setInterval(250);
    connect(&contextTimer, &QTimer::timeout, this, [this] { if (contextStillCurrent && !contextStillCurrent()) health->invalidate(); });
}
void DialogServiceHealth::begin() {
    if (health->isRunning() || !captureContext) return;
    const auto context = captureContext();
    copy->setEnabled(false); observe->setEnabled(false); start->setEnabled(false); service->setEnabled(false); stability->setEnabled(false); cancel->setEnabled(true);
    if (!health->start(context, service->currentData().toString(), stability->isChecked() ? 10 : 3, probeTargets)) {
        start->setEnabled(true); service->setEnabled(true); stability->setEnabled(true); cancel->setEnabled(false);
        verdict->setText(tr("Не удалось начать проверку. Проверьте параметры подключения.")); return;
    }
    contextTimer.start(); refresh();
}
void DialogServiceHealth::refresh() { verdict->setText(health->summary()); results->setPlainText(health->details()); }
