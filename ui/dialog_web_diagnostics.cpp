#include "dialog_web_diagnostics.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

DialogWebDiagnostics::DialogWebDiagnostics(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Помощь браузеру"));
    setObjectName(QStringLiteral("grWebDiagnostics"));
    resize(610, 480);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18); layout->setSpacing(12);
    auto *explanation = new QLabel(tr("Если сайт открывается в другом клиенте или внутренний ресурс не находится, сравните два пути подключения."), this);
    explanation->setWordWrap(true); layout->addWidget(explanation);
    hintsText = new QLabel(this); hintsText->setObjectName(QStringLiteral("grBrowserDnsHints")); hintsText->setWordWrap(true); layout->addWidget(hintsText);
    auto *limitation = new QLabel(tr("Проверка использует настройки прокси Windows и локальный прокси GreenRhythm. Она не воспроизводит защищённый DNS, расширения и вашу учётную запись браузера. Сайт может отвечать на проверку иначе, чем на открытие страницы. Cookies, заголовки и содержимое ответа не сохраняются."), this);
    limitation->setWordWrap(true); layout->addWidget(limitation);
    address = new QLineEdit(this); address->setObjectName(QStringLiteral("grWebAddress"));
    address->setPlaceholderText(tr("https://адрес-сайта")); address->setAccessibleName(tr("Адрес сайта для проверки"));
    layout->addWidget(address);
    addressError = new QLabel(this); addressError->setObjectName(QStringLiteral("grWebAddressError")); addressError->setWordWrap(true); addressError->hide(); layout->addWidget(addressError);
    auto *actions = new QHBoxLayout;
    check = new QPushButton(tr("Проверить сайт"), this); check->setObjectName(QStringLiteral("grWebCheck"));
    cancel = new QPushButton(tr("Отменить проверку"), this); cancel->setObjectName(QStringLiteral("grWebCancel")); cancel->hide();
    actions->addWidget(check); actions->addWidget(cancel); actions->addStretch(); layout->addLayout(actions);
    auto resultLabel = [this, layout](const QString &title, const char *name) {
        auto *label = new QLabel(title, this); label->setObjectName(QString::fromLatin1(name)); label->setWordWrap(true); label->setTextFormat(Qt::PlainText); layout->addWidget(label); return label;
    };
    systemResult = resultLabel(tr("Системный путь: ещё не проверен"), "grWebSystemResult");
    greenResult = resultLabel(tr("Через GreenRhythm: ещё не проверен"), "grWebGreenResult");
    comparison = resultLabel({}, "grWebComparison");
    layout->addStretch();
    auto *connectNote = new QLabel(tr("«Подключить браузеры» включает системный прокси GreenRhythm. Это влияет и на другие приложения, использующие прокси Windows. Настройки защищённого DNS браузера остаются прежними."), this);
    connectNote->setWordWrap(true); layout->addWidget(connectNote);
    auto *buttons = new QDialogButtonBox(this);
    connectBrowsers = buttons->addButton(tr("Подключить браузеры"), QDialogButtonBox::ActionRole);
    connectBrowsers->setObjectName(QStringLiteral("grConnectBrowsers"));
    auto *leave = buttons->addButton(tr("Оставить как есть"), QDialogButtonBox::RejectRole); leave->setObjectName(QStringLiteral("grLeaveBrowserSettings"));
    layout->addWidget(buttons);
    probe = new GreenRhythm::BrowserNetworkProbe(this);
    connect(check, &QPushButton::clicked, this, &DialogWebDiagnostics::beginCheck);
    connect(address, &QLineEdit::returnPressed, this, &DialogWebDiagnostics::beginCheck);
    connect(cancel, &QPushButton::clicked, probe, &GreenRhythm::BrowserNetworkProbe::cancel);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::finished, probe, &GreenRhythm::BrowserNetworkProbe::cancel);
    connect(connectBrowsers, &QPushButton::clicked, this, [this] { emit connectBrowsersRequested(); });
    connect(probe, &GreenRhythm::BrowserNetworkProbe::pathFinished, this, [this](const GreenRhythm::WebCheckResult &result) {
        const auto text = GreenRhythm::webCheckResultText(result);
        if (result.path == GreenRhythm::WebCheckPath::System) { system = result; systemResult->setText(tr("Системный путь: %1").arg(text)); }
        else { green = result; greenResult->setText(tr("Через GreenRhythm: %1").arg(text)); }
        if (system && green) comparison->setText(GreenRhythm::compareWebCheckResults(*system, *green));
    });
    connect(probe, &GreenRhythm::BrowserNetworkProbe::completed, this, [this] { check->setEnabled(true); address->setEnabled(true); cancel->hide(); });
    setProxyEndpoint(2080, {}, {}, false);
}

void DialogWebDiagnostics::setProxyEndpoint(quint16 port, const QString &user, const QString &password, bool profileRunning) {
    options.greenRhythmProxy = QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), port, user, password);
    options.profileRunning = profileRunning;
#ifdef Q_OS_WIN
    connectBrowsers->setEnabled(profileRunning && port > 0);
    connectBrowsers->setToolTip(profileRunning ? QString() : tr("Сначала подключите сервер GreenRhythm."));
#else
    connectBrowsers->setEnabled(false);
    connectBrowsers->setToolTip(tr("Кнопка подключения браузеров доступна в Windows."));
#endif
    setHints(GreenRhythm::readBrowserNetworkHints(port));
}

void DialogWebDiagnostics::setHints(const GreenRhythm::BrowserNetworkHints &hints) { hintsText->setText(GreenRhythm::browserNetworkHintsText(hints)); }

void DialogWebDiagnostics::beginCheck() {
    if (probe->isRunning()) return;
    QUrl url; QString reason;
    if (!GreenRhythm::parseWebCheckUrl(address->text(), &url, &reason)) { addressError->setText(reason); addressError->show(); return; }
    addressError->hide(); comparison->clear(); system.reset(); green.reset();
    systemResult->setText(tr("Системный путь: проверяем…")); greenResult->setText(tr("Через GreenRhythm: проверяем…"));
    check->setEnabled(false); address->setEnabled(false); cancel->show();
    probe->start(url, options);
}
