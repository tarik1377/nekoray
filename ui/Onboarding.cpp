#include "ui/mainwindow_common.hpp"

/**
 * Первый запуск: приветственная страница и приём ссылки одним щелчком.
 *
 * ВЫНЕСЕНО ИЗ mainwindow.cpp. Это то, что человек видит раньше всего
 * остального, и единственное место, где программа объясняет себя. Держать его
 * между обработчиками контекстного меню значило заведомо не пересматривать.
 *
 * Разбор ссылки лежит здесь же и намеренно недоверчив: ссылка приходит извне —
 * из браузера, из переписки, — и разбирается как чужой ввод.
 */

// Onboarding / empty-state page. Lives in the same layout cell as the profile table:
// while there are no profiles the table is hidden and this page takes its slot, so it
// can never overlap or clip anything at any window size. Branding help point only —
// GreenRhythm stays a universal client (✕ hides it for the session).
// Contract: greenrhythm://import/<percent-encoded payload>, payload = https
// subscription link OR a single vless:// profile. The payload is UNTRUSTED:
// decode exactly once, cap at 8 KB, reject control characters and any other
// scheme (file://, javascript:, http://, ...). Never passed to a shell.
static QString ParseGreenRhythmImport(const QString &raw, QString *errOut) {
    const auto prefix = QStringLiteral("greenrhythm://import/");
    if (!raw.startsWith(prefix, Qt::CaseInsensitive)) {
        *errOut = QObject::tr("неизвестный формат ссылки");
        return {};
    }
    const auto payload = QUrl::fromPercentEncoding(raw.mid(prefix.size()).toUtf8()).trimmed();
    if (payload.toUtf8().size() > 8 * 1024) {
        *errOut = QObject::tr("слишком длинная ссылка");
        return {};
    }
    // Reject control chars (C0/C1) and Unicode bidi/zero-width format characters:
    // the whole safety model is "show the user the real source host", so RTLO and
    // homograph-hiding code points must not survive into the confirmation dialog.
    for (const auto &ch: payload) {
        const auto u = ch.unicode();
        const bool control = u < 0x20 || (u >= 0x7F && u <= 0x9F);
        const bool bidiOrZeroWidth = u == 0x200B || u == 0x200C || u == 0x200D || u == 0xFEFF ||
                                     (u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069);
        if (control || bidiOrZeroWidth) {
            *errOut = QObject::tr("недопустимые символы");
            return {};
        }
    }
    const bool schemeOk = payload.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
                          payload.startsWith(QStringLiteral("vless://"), Qt::CaseInsensitive);
    if (!schemeOk || !QUrl(payload).isValid()) {
        *errOut = QObject::tr("поддерживаются только https-ссылки подписок и vless-профили");
        return {};
    }
    return payload;
}

void MainWindow::build_onboarding_panel() {
    auto *panel = new QFrame(this);
    panel->setObjectName("onboardingPanel");
    panel->hide();
    onboarding_panel = panel;

    // Translucent neutral card so the page follows any theme (dark or light);
    // the only hardcoded color is the brand-green primary button.
    panel->setStyleSheet(QStringLiteral(
        "#onboardingPanel{background:transparent;}"
        "#onboardCard{background-color:rgba(127,134,147,0.10);border:1px solid rgba(127,134,147,0.28);border-radius:12px;}"
        "QPushButton#onboardPrimary{background-color:#2ea043;color:#ffffff;border:none;border-radius:8px;padding:6px 22px;font-weight:600;}"
        "QPushButton#onboardPrimary:hover{background-color:#3fb950;}"
        "QPushButton#onboardPrimary:pressed{background-color:#2c974b;}"));

    auto *outer = new QVBoxLayout(panel);
    outer->setContentsMargins(24, 6, 24, 12);
    outer->setSpacing(0);

    auto *topRow = new QHBoxLayout();
    topRow->addStretch();
    auto *closeBtn = new QToolButton(panel);
    closeBtn->setText(QString::fromUtf8("\xE2\x9C\x95")); // ✕
    closeBtn->setAutoRaise(true);
    closeBtn->setToolTip(tr("Скрыть"));
    connect(closeBtn, &QToolButton::clicked, this, [=] {
        onboarding_dismissed = true;
        refresh_onboarding();
    });
    topRow->addWidget(closeBtn);
    outer->addLayout(topRow);

    outer->addStretch(2);

    auto *title = new QLabel(tr("Добро пожаловать в GreenRhythm") + QString::fromUtf8(" \xF0\x9F\x8C\xBF"), panel); // 🌿
    title->setAlignment(Qt::AlignHCenter);
    { QFont f = title->font(); f.setPointSizeF(f.pointSizeF() * 1.5); f.setBold(true); title->setFont(f); }
    outer->addWidget(title);
    outer->addSpacing(6);

    auto *subtitle = new QLabel(tr("Вставьте ссылку подписки — или получите доступ за пару минут"), panel);
    subtitle->setAlignment(Qt::AlignHCenter);
    subtitle->setEnabled(false); // dimmed via the theme's disabled palette
    outer->addWidget(subtitle);
    outer->addSpacing(16);

    // One centered card, width-capped so it reads like a dialog, not a stretched bar.
    auto *card = new QFrame(panel);
    card->setObjectName("onboardCard");
    card->setMaximumWidth(620);
    auto *cardL = new QVBoxLayout(card);
    cardL->setContentsMargins(18, 16, 18, 16);
    cardL->setSpacing(10);

    auto *rowA = new QHBoxLayout();
    rowA->setSpacing(8);
    auto *subEdit = new QLineEdit(card);
    subEdit->setPlaceholderText(tr("Ссылка подписки или профиля…"));
    subEdit->setMinimumHeight(32);
    rowA->addWidget(subEdit, 1);
    auto *importBtn = new QPushButton(tr("Импорт"), card);
    importBtn->setObjectName("onboardPrimary");
    importBtn->setMinimumHeight(32);
    importBtn->setCursor(Qt::PointingHandCursor);
    connect(importBtn, &QPushButton::clicked, this, [=] { import_link_offer_connect(subEdit->text()); });
    connect(subEdit, &QLineEdit::returnPressed, importBtn, &QPushButton::click);
    rowA->addWidget(importBtn);
    cardL->addLayout(rowA);

    auto *rowB = new QHBoxLayout();
    rowB->setSpacing(8);
    auto *pasteBtn = new QPushButton(tr("Вставить из буфера"), card);
    connect(pasteBtn, &QPushButton::clicked, this, [=] {
        const auto clip = QApplication::clipboard()->text().trimmed();
        if (clip.isEmpty()) return;
        import_link_offer_connect(clip);
    });
    rowB->addWidget(pasteBtn);
    rowB->addStretch();
    auto *links = new QLabel(card);
    links->setTextFormat(Qt::RichText);
    links->setOpenExternalLinks(true);
    links->setText(QStringLiteral("<a href=\"%1\" style=\"color:#3fb950;text-decoration:none;\">%2</a>"
                                  "&nbsp;&nbsp;·&nbsp;&nbsp;"
                                  "<a href=\"%3\" style=\"color:#3fb950;text-decoration:none;\">Telegram</a>")
                       .arg(GreenRhythm::kBuyUrl, tr("Нет подписки? Получить"), GreenRhythm::kTelegramUrl));
    rowB->addWidget(links);
    cardL->addLayout(rowB);

    auto *cardRow = new QHBoxLayout();
    cardRow->addStretch();
    cardRow->addWidget(card, 1);
    cardRow->addStretch();
    outer->addLayout(cardRow);

    outer->addStretch(3);
}

// Empty-state switcher: with zero profiles the table hides and the welcome page takes
// its layout slot; with any profile (or after ✕) the table is restored. The page follows
// the table across group tabs — both live in the current tab's layout.
void MainWindow::refresh_onboarding() {
    if (onboarding_panel == nullptr) return;
    const bool empty = NekoGui::profileManager->profiles.empty();
    if (!empty && !NekoGui::dataStore->onboarding_completed) {
        NekoGui::dataStore->onboarding_completed = true;
        NekoGui::dataStore->Save();
    }
    const bool show = empty && !onboarding_dismissed;
    if (show) {
        auto *host = ui->proxyListTable->parentWidget();
        if (host != nullptr && host->layout() != nullptr && onboarding_panel->parentWidget() != host) {
            host->layout()->addWidget(onboarding_panel);
        }
    }
    onboarding_panel->setVisible(show);
    ui->proxyListTable->setVisible(!show);
}

void MainWindow::import_scheme_url(const QString &raw) {
    ActivateWindow(this);
    QString err;
    const auto payload = ParseGreenRhythmImport(raw, &err);
    if (payload.isEmpty()) {
        MessageBoxWarning(tr("Импорт по ссылке"), tr("Ссылка не добавлена: %1.").arg(err));
        return;
    }
    // Reentrancy guard: QMessageBox::question below pumps a nested event loop, so a
    // rapid second deep link for the same payload could re-enter before the first
    // group is created and duplicate it. Ignore identical in-flight imports.
    if (scheme_import_inflight.contains(payload)) return;
    scheme_import_inflight.insert(payload);
    struct InflightGuard {
        QSet<QString> &set;
        QString key;
        ~InflightGuard() { set.remove(key); }
    } inflightGuard{scheme_import_inflight, payload};
    const QUrl url(payload);
    if (payload.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        // Subscription. Idempotent: the same link updates its existing group.
        std::shared_ptr<NekoGui::Group> group;
        {
            QMutexLocker locker(&NekoGui::profileManager->mutex);
            for (const auto &[gid, g]: NekoGui::profileManager->groups) {
                if (g != nullptr && g->url == payload) {
                    group = g;
                    break;
                }
            }
        }
        if (QMessageBox::question(GetMessageBoxParent(), tr("Зелёный Ритм — импорт"),
                                  tr("Добавить подписку с %1 и загрузить список серверов?").arg(url.host())) != QMessageBox::Yes) return;
        if (group == nullptr) {
            group = NekoGui::ProfileManager::NewGroup();
            group->name = GreenRhythm::kServiceName;
            group->url = payload;
            NekoGui::profileManager->AddGroup(group);
            refresh_groups();
        }
        const int gid = group->id;
        NekoGui_sub::groupUpdater->AsyncUpdate(payload, gid, [this, gid] {
            runOnUiThread([this, gid] {
                auto g = NekoGui::profileManager->GetGroup(gid);
                if (g == nullptr) return;
                const auto profiles = g->Profiles();
                refresh_groups();
                refresh_proxy_list();
                if (profiles.isEmpty()) {
                    MessageBoxWarning(tr("Зелёный Ритм"),
                                      tr("Подписка недоступна — возможно, срок истёк.\nПродлить: %1").arg(GreenRhythm::kRenewUrl));
                    return;
                }
                if (QMessageBox::question(GetMessageBoxParent(), tr("Зелёный Ритм"),
                                          tr("Подписка добавлена (профилей: %1). Подключиться сейчас?").arg(profiles.size())) == QMessageBox::Yes) {
                    neko_start(profiles.first()->id);
                }
            });
        });
    } else {
        // Single vless:// profile into the current group.
        if (QMessageBox::question(GetMessageBoxParent(), tr("Зелёный Ритм — импорт"),
                                  tr("Добавить профиль сервера %1?").arg(url.host())) != QMessageBox::Yes) return;
        // Snapshot existing profile ids so we start exactly the one this link added,
        // not whatever a concurrent update happened to give the highest id.
        auto before = std::make_shared<QSet<int>>();
        {
            QMutexLocker locker(&NekoGui::profileManager->mutex);
            for (const auto &[pid, p]: NekoGui::profileManager->profiles) before->insert(pid);
        }
        NekoGui_sub::groupUpdater->AsyncUpdate(payload, -1, [this, before] {
            runOnUiThread([this, before] {
                refresh_proxy_list();
                int added = -1;
                {
                    QMutexLocker locker(&NekoGui::profileManager->mutex);
                    for (const auto &[pid, p]: NekoGui::profileManager->profiles) {
                        if (!before->contains(pid)) { added = pid; break; }
                    }
                }
                if (added < 0) return;
                if (QMessageBox::question(GetMessageBoxParent(), tr("Зелёный Ритм"),
                                          tr("Профиль добавлен. Подключиться сейчас?")) == QMessageBox::Yes) {
                    neko_start(added);
                }
            });
        });
    }
}
