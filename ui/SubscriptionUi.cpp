#include "ui/mainwindow_common.hpp"

/**
 * Подписка: остаток, QR и приём ссылки.
 *
 * ВЫНЕСЕНО ИЗ mainwindow.cpp. Это единственное место, где программа говорит с
 * человеком о деньгах и сроке, и потому у здешних текстов особая цена: «13
 * дней» и «истекла» человек читает буквально и по ним принимает решение.
 * Ссылка подписки при этом секрет — в QR она попадает, в журнал не должна.
 */

// QR bridge: show the subscription link as a QR code to scan in a mobile client —
// one subscription across devices. The image is decoded back with the bundled ZXing
// reader before it is shown: a QR we cannot read ourselves never reaches the screen.
void MainWindow::show_subscription_qr() {
    std::shared_ptr<NekoGui::Group> gr;
    {
        QMutexLocker locker(&NekoGui::profileManager->mutex);
        for (const auto &[gid, g]: NekoGui::profileManager->groups) {
            if (g == nullptr || g->url.isEmpty()) continue;
            if (g->name == GreenRhythm::kServiceName || g->url.contains(QStringLiteral("verdantvibe"), Qt::CaseInsensitive)) {
                gr = g;
                break;
            }
        }
    }
    if (gr == nullptr) {
        auto cg = NekoGui::profileManager->CurrentGroup();
        if (cg != nullptr && !cg->url.isEmpty()) gr = cg;
    }
    if (gr == nullptr) {
        MessageBoxWarning(tr("QR подписки"), tr("Нет группы-подписки. Импортируйте подписку «Зелёный Ритм»."));
        return;
    }

    QImage im;
    try {
        const auto qr = qrcodegen::QrCode::encodeText(gr->url.toUtf8().data(), qrcodegen::QrCode::Ecc::MEDIUM);
        constexpr qint32 pad = 2;
        const qint32 sz = qr.getSize();
        im = QImage(sz + pad * 2, sz + pad * 2, QImage::Format_RGB32);
        im.fill(qRgb(255, 255, 255));
        for (int y = 0; y < sz; y++)
            for (int x = 0; x < sz; x++)
                if (qr.getModule(x, y)) im.setPixel(x + pad, y + pad, qRgb(0, 0, 0));
    } catch (const std::exception &ex) {
        MessageBoxWarning(tr("QR подписки"), ex.what());
        return;
    }

#ifndef NKR_NO_ZXING
    {
        using namespace ZXingQt;
        auto hints = DecodeHints()
                         .setFormats(BarcodeFormat::QRCode)
                         .setTryRotate(false)
                         .setBinarizer(Binarizer::FixedThreshold);
        const auto scaled = im.scaled(im.width() * 4, im.height() * 4, Qt::KeepAspectRatio, Qt::FastTransformation);
        if (ReadBarcode(scaled, hints).text() != gr->url) {
            MessageBoxWarning(tr("QR подписки"), tr("Самопроверка QR-кода не прошла — код не показан."));
            return;
        }
        MW_show_log(tr("QR подписки: самопроверка декодирования пройдена."));
    }
#endif

    auto w = new QDialog(this);
    w->setWindowTitle(tr("QR подписки"));
    auto lay = new QVBoxLayout(w);
    auto pic = new QLabel(w);
    pic->setPixmap(QPixmap::fromImage(im.scaled(340, 340, Qt::KeepAspectRatio, Qt::FastTransformation), Qt::MonoOnly));
    pic->setAlignment(Qt::AlignCenter);
    pic->setMargin(8);
    lay->addWidget(pic);
    auto hint = new QLabel(tr("Отсканируйте в мобильном клиенте — одна подписка на всех устройствах."), w);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    auto copyBtn = new QPushButton(tr("Копировать ссылку"), w);
    connect(copyBtn, &QPushButton::clicked, w, [url = gr->url] { QApplication::clipboard()->setText(url); });
    lay->addWidget(copyBtn);
    w->exec();
    w->deleteLater();
}

// Onboarding / paste import that offers to connect on success — mirrors the deep-link
// flow so the first-run funnel doesn't dead-end at an empty table. Diffs the profile
// set around the async import to find what was added; offers the first new server.
void MainWindow::import_link_offer_connect(const QString &link) {
    const auto trimmed = link.trimmed();
    if (trimmed.isEmpty()) return;
    auto before = std::make_shared<QSet<int>>();
    {
        QMutexLocker locker(&NekoGui::profileManager->mutex);
        for (const auto &[pid, p]: NekoGui::profileManager->profiles) before->insert(pid);
    }
    NekoGui_sub::groupUpdater->AsyncUpdate(trimmed, -1, [this, before] {
        runOnUiThread([this, before] {
            refresh_proxy_list();
            int first = -1, added = 0;
            {
                QMutexLocker locker(&NekoGui::profileManager->mutex);
                for (const auto &[pid, p]: NekoGui::profileManager->profiles) {
                    if (before->contains(pid)) continue;
                    added++;
                    if (first < 0) first = pid; // std::map iterates ascending → lowest new id
                }
            }
            if (first < 0) return; // nothing added (import cancelled or failed)
            if (QMessageBox::question(GetMessageBoxParent(), GreenRhythm::kServiceName,
                                      tr("Добавлено серверов: %1. Подключиться сейчас?").arg(added)) == QMessageBox::Yes) {
                neko_start(first);
            }
        });
    });
}

// «Зелёный Ритм» subscription badge in the bottom status row: days + traffic left,
// parsed from the Subscription-UserInfo the server already sends (no extra request,
// no telemetry). Green normally; amber + a «Продлить» link when nearly out. Only
// shows for the brand's own subscription group — other services are untouched.
void MainWindow::refresh_subscription_status() {
    if (ui->label_sub_status == nullptr) return;

    std::shared_ptr<NekoGui::Group> gr;
    {
        QMutexLocker locker(&NekoGui::profileManager->mutex);
        for (const auto &[gid, g]: NekoGui::profileManager->groups) {
            if (g == nullptr || g->url.isEmpty()) continue;
            if (g->name == GreenRhythm::kServiceName || g->url.contains(QStringLiteral("verdantvibe"), Qt::CaseInsensitive)) {
                gr = g;
                break;
            }
        }
    }
    if (gr == nullptr || gr->info.trimmed().isEmpty()) {
        ui->label_sub_status->clear();
        ui->label_sub_status->setVisible(false);
        if (shell != nullptr) shell->setSubscription(QString(), false);
        return;
    }

    auto grab = [&](const QString &key) -> long long {
        auto m = QRegularExpression(key + "=([0-9]+)").match(gr->info);
        return m.hasMatch() ? m.captured(1).toLongLong() : -1;
    };
    const long long total = grab("total"), up = grab("upload"), down = grab("download"), expire = grab("expire");
    const long long used = (up < 0 ? 0 : up) + (down < 0 ? 0 : down);

    QStringList parts;
    bool low = false;
    if (expire > 0) {
        long long days = (expire - QDateTime::currentSecsSinceEpoch()) / 86400;
        if (days < 0) days = 0;
        parts << tr("%1 дн.").arg(days);
        if (days <= 3) low = true;
    }
    if (total > 0) {
        long long left = total - used;
        if (left < 0) left = 0;
        parts << ReadableSize(left);
        if (static_cast<double>(left) / total <= 0.10) low = true;
    }
    if (parts.isEmpty()) {
        ui->label_sub_status->clear();
        ui->label_sub_status->setVisible(false);
        if (shell != nullptr) shell->setSubscription(QString(), false);
        return;
    }

    const QString color = low ? QStringLiteral("#E3A008") : QStringLiteral("#3FB950");
    QString text = QStringLiteral("<span style='color:%1;'>%2 %3</span>")
                       .arg(color, QString::fromUtf8("\xF0\x9F\x8C\xBF"), parts.join(QStringLiteral(" \xC2\xB7 "))); // 🌿 ·
    if (low) {
        text += QStringLiteral(" <a href='%1' style='color:#E3A008;text-decoration:none;'>%2</a>")
                    .arg(GreenRhythm::kRenewUrl, tr("Продлить"));
    }
    ui->label_sub_status->setText(text);
    ui->label_sub_status->setToolTip(tr("Подписка «Зелёный Ритм»"));
    ui->label_sub_status->setVisible(true);

    // Та же правда — в боковую колонку. Строка внизу окна остаётся для тех, кто
    // к ней привык, но искать остаток подписки там никто не догадывался.
    if (shell != nullptr) shell->setSubscription(parts.join(QStringLiteral(" · ")), low);
}

void MainWindow::display_qr_link(bool nkrFormat) {
    auto ents = get_now_selected_list();
    if (ents.count() != 1) return;

    class W : public QDialog {
    public:
        QLabel *l = nullptr;
        QCheckBox *cb = nullptr;
        //
        QPlainTextEdit *l2 = nullptr;
        QImage im;
        //
        QString link;
        QString link_nk;

        void show_qr(const QSize &size) const {
            auto side = size.height() - 20 - l2->size().height() - cb->size().height();
            l->setPixmap(QPixmap::fromImage(im.scaled(side, side, Qt::KeepAspectRatio, Qt::FastTransformation),
                                            Qt::MonoOnly));
            l->resize(side, side);
        }

        void refresh(bool is_nk) {
            auto link_display = is_nk ? link_nk : link;
            l2->setPlainText(link_display);
            constexpr qint32 qr_padding = 2;
            //
            try {
                qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(link_display.toUtf8().data(), qrcodegen::QrCode::Ecc::MEDIUM);
                qint32 sz = qr.getSize();
                im = QImage(sz + qr_padding * 2, sz + qr_padding * 2, QImage::Format_RGB32);
                QRgb black = qRgb(0, 0, 0);
                QRgb white = qRgb(255, 255, 255);
                im.fill(white);
                for (int y = 0; y < sz; y++)
                    for (int x = 0; x < sz; x++)
                        if (qr.getModule(x, y))
                            im.setPixel(x + qr_padding, y + qr_padding, black);
                show_qr(size());
            } catch (const std::exception &ex) {
                QMessageBox::warning(nullptr, "error", ex.what());
            }
        }

        W(const QString &link_, const QString &link_nk_) {
            link = link_;
            link_nk = link_nk_;
            //
            setLayout(new QVBoxLayout);
            setMinimumSize(256, 256);
            QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            sizePolicy.setHeightForWidth(true);
            setSizePolicy(sizePolicy);
            //
            l = new QLabel();
            l->setMinimumSize(256, 256);
            l->setMargin(6);
            l->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            l->setScaledContents(true);
            layout()->addWidget(l);
            cb = new QCheckBox;
            cb->setText("GreenRhythm Links");
            layout()->addWidget(cb);
            l2 = new QPlainTextEdit();
            l2->setReadOnly(true);
            layout()->addWidget(l2);
            //
            connect(cb, &QCheckBox::toggled, this, &W::refresh);
            refresh(false);
        }

        void resizeEvent(QResizeEvent *resizeEvent) override {
            show_qr(resizeEvent->size());
        }
    };

    auto link = ents.first()->bean->ToShareLink();
    auto link_nk = ents.first()->bean->ToNekorayShareLink(ents.first()->type);
    auto w = new W(link, link_nk);
    w->setWindowTitle(ents.first()->bean->DisplayTypeAndName());
    w->exec();
    w->deleteLater();
}
