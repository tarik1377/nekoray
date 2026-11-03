#include "dialog_web_diagnostics.h"

#include <QDialogButtonBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

DialogWebDiagnostics::DialogWebDiagnostics(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Помощь браузеру"));
    setObjectName(QStringLiteral("grWebDiagnostics"));
    resize(610, 480);
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this); scroll->setObjectName("grWebDiagnosticsScroll"); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget(scroll); scroll->setWidget(body); outer->addWidget(scroll);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(20, 18, 20, 18); layout->setSpacing(12);
    auto *explanation = new QLabel(tr("Если сайт открывается в другом клиенте или внутренний ресурс не находится, сравните два пути подключения."), this);
    explanation->setWordWrap(true); layout->addWidget(explanation);
    hintsText = new QLabel(this); hintsText->setObjectName(QStringLiteral("grBrowserDnsHints")); hintsText->setWordWrap(true); layout->addWidget(hintsText);
    routingHint = new QLabel(this); routingHint->setObjectName("grBrowserRoutingHint"); routingHint->setWordWrap(true); routingHint->setTextFormat(Qt::PlainText); layout->addWidget(routingHint);
    auto *routingActions = new QHBoxLayout;
    routingBrowser = new QComboBox(this); routingBrowser->setObjectName("grRoutingBrowser"); routingBrowser->setAccessibleName(tr("Браузер с отдельным правилом"));
    siteRules = new QPushButton(tr("По правилам сайтов"), this); siteRules->setObjectName("grBrowserSiteRules"); siteRules->setEnabled(false);
    routingActions->addWidget(routingBrowser); routingActions->addWidget(siteRules); routingActions->addStretch(); layout->addLayout(routingActions);
    routingStatus = new QLabel(this); routingStatus->setObjectName("grBrowserRoutingStatus"); routingStatus->setWordWrap(true); routingStatus->setTextFormat(Qt::PlainText); layout->addWidget(routingStatus);
    connect(siteRules, &QPushButton::clicked, this, [this] {
        const auto name = routingBrowser->currentData().toString();
        GreenRhythm::ProgramRouting::State replacement; QString error;
        if (!routingCommit || !GreenRhythm::ProgramRouting::restore(routingState, name, &replacement, &error)) {
            routingStatus->setText(error); return;
        }
        if (!routingCommit(name, routingState, &error)) { routingStatus->setText(error); return; }
        setProgramRouting(replacement, tunActive);
        routingStatus->setText(tr("Правило выбранного браузера сохранено. Применится при следующем подключении. Текущее подключение работает дальше; остальные программы и маршруты сохранены."));
    });
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
    outer->addWidget(buttons);
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
    setProgramRouting({}, false);
}

void DialogWebDiagnostics::setRoutingCommitter(std::function<bool(const QString &, const GreenRhythm::ProgramRouting::State &, QString *)> callback) {
    routingCommit = std::move(callback);
    setProgramRouting(routingState, tunActive);
}

void DialogWebDiagnostics::setProgramRouting(const GreenRhythm::ProgramRouting::State &state, bool internalTunActive) {
    routingState = state; tunActive = internalTunActive;
    const auto selected = routingBrowser->currentData().toString();
    const auto overrides = GreenRhythm::ProgramRouting::browserOverrides(state);
    routingBrowser->clear();
    for (const auto &name : overrides) {
        const auto mode = GreenRhythm::ProgramRouting::overrideMode(state, name);
        const auto direction = mode == "bypass" ? tr("напрямую") : mode == "proxy" ? tr("через VPN") : tr("не определён");
        routingBrowser->addItem((name == "browser.exe" ? tr("Яндекс Браузер") : name) + " · " + direction, name);
    }
    if (routingBrowser->findData(selected) >= 0) routingBrowser->setCurrentIndex(routingBrowser->findData(selected));
    routingBrowser->setVisible(!overrides.isEmpty()); siteRules->setVisible(!overrides.isEmpty());
    GreenRhythm::ProgramRouting::State ignored; QString error;
    siteRules->setEnabled(bool(routingCommit) && !overrides.isEmpty()
        && GreenRhythm::ProgramRouting::restore(state, routingBrowser->currentData().toString(), &ignored, &error));
    QJsonObject checkedPrograms; QString parseError;
    if (!GreenRhythm::GameAppRules::parse(state.applications, &checkedPrograms, &parseError)) {
        routingHint->setText(parseError + '\n' + tr("Маршрут браузеров по спискам программ определить полностью не удалось."));
        siteRules->setEnabled(false);
    } else if (state.whitelist) {
        routingHint->setText(tr("Включён белый список TUN: выбор по сайтам для всего браузера этим действием не восстанавливается. Режим подключения сохранён."));
    } else if (!overrides.isEmpty()) {
        routingHint->setText(tr("Для %1 сохранён маршрут всей программы. При внутреннем TUN он имеет приоритет над правилами сайтов и может отправлять ChatGPT напрямую или российские сайты через VPN. «По правилам сайтов» уберёт отдельный выбор только у выбранного браузера. Будет сохранена резервная копия. %2")
            .arg(overrides.join(", "), tunActive ? tr("TUN сейчас включён; применённый ядром список мог отличаться, если правила меняли без переподключения.") : tr("Внутренний TUN сейчас выключен; это предупреждение о сохранённых правилах.")));
        if (!error.isEmpty()) routingHint->setText(routingHint->text() + '\n' + error);
    } else {
        routingHint->setText(tr("В сохранённых списках нет отдельного маршрута браузеров. Для них заданы общие правила; действующий маршрут проверяйте в соединениях. Другие правила схемы и расширения браузера могут менять путь сайта."));
    }
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
