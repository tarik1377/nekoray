#include "ui/mainwindow_common.hpp"

/**
 * Диагностика: что собрать и показать, когда «не работает».
 *
 * ВЫНЕСЕНО ИЗ mainwindow.cpp. Здесь собирается то, что человек присылает в
 * поддержку, и потому у этих функций есть общее свойство, которое легко
 * потерять из виду среди обработчиков меню: они обязаны отчитываться по факту,
 * а не по намерению. Держать их вместе — значит держать это правило на виду.
 *
 * Отдельно про потоки: foreign_tunnels_line запускает powershell и ждёт его,
 * поэтому звать её можно только из рабочего потока. Предупреждение стоит и в
 * mainwindow.h, у объявления.
 */

void MainWindow::run_diagnostics() {
    QString host;
    int port = 0;
    {
        std::shared_ptr<NekoGui::ProxyEntity> ent;
        auto sel = get_now_selected_list();
        if (!sel.isEmpty()) {
            ent = sel.first();
        } else if (NekoGui::dataStore->started_id >= 0) {
            ent = NekoGui::profileManager->GetProfile(NekoGui::dataStore->started_id);
        } else {
            auto cg = NekoGui::profileManager->CurrentGroup();
            if (cg != nullptr) {
                auto ps = cg->ProfilesWithOrder();
                if (!ps.isEmpty()) ent = ps.first();
            }
        }
        if (ent != nullptr && ent->bean != nullptr) {
            host = ent->bean->serverAddress;
            port = ent->bean->serverPort;
        }
    }

    // Whether the tunnel is up, and where to reach it, are UI-thread facts — capture them
    // before the worker so it can probe THROUGH the tunnel, not just around it.
    const bool up = (running != nullptr);
    const QString proxyAddr = NekoGui::dataStore->inbound_address;
    const quint16 proxyPort = quint16(NekoGui::dataStore->inbound_socks_port);
    const QString header = diagnostics_header();

    auto *dlg = new QProgressDialog(tr("Проверка соединения…"), QString(), 0, 0, this);
    dlg->setWindowTitle(tr("Диагностика соединения"));
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setCancelButton(nullptr);
    dlg->setMinimumDuration(0);
    dlg->show();

    runOnNewThread([this, host, port, up, proxyAddr, proxyPort, header, dlg] {
        // Чужие туннели ищутся ЗДЕСЬ, в рабочем потоке: поиск запускает
        // powershell и ждёт его, а в потоке интерфейса эти секунды выглядят
        // как «программа не отвечает».
        const QString foreign = foreign_tunnels_line();
        // Состояние туннеля спрашивается здесь же: внутри powershell/ifconfig,
        // и в потоке интерфейса эти секунды выглядели бы как зависание.
        const QString tunAdapter = NekoGui_sys::DescribeTunAdapter();

        bool net, dns, tcp = false, tls = false;
        {
            QTcpSocket s;
            s.connectToHost(QStringLiteral("1.1.1.1"), 443);
            net = s.waitForConnected(4000);
            s.abort();
        }
        {
            auto hi = QHostInfo::fromName(QStringLiteral("www.google.com"));
            dns = hi.error() == QHostInfo::NoError && !hi.addresses().isEmpty();
        }
        if (!host.isEmpty() && port > 0) {
            {
                QTcpSocket s;
                s.connectToHost(host, port);
                tcp = s.waitForConnected(5000);
                s.abort();
            }
            if (tcp) {
                QSslSocket ss;
                ss.setPeerVerifyMode(QSslSocket::VerifyNone); // Reality/self-signed nodes
                ss.connectToHostEncrypted(host, port);
                tls = ss.waitForEncrypted(6000);
                ss.abort();
            }
        }

        // The decisive test support could never run: does real traffic actually pass THROUGH
        // the running tunnel, and what exit IP does the world see? A 204 through the local
        // inbound proves the whole path; cloudflare's trace gives the exit IP for free.
        bool tunnelTested = false, tunnelOk = false;
        QString exitIp;
        if (up) {
            tunnelTested = true;
            QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, proxyAddr, proxyPort);
            QNetworkAccessManager nam;
            nam.setProxy(proxy);
            {
                auto *r = nam.get(QNetworkRequest(QUrl(QStringLiteral("http://cp.cloudflare.com/generate_204"))));
                QEventLoop loop;
                QTimer::singleShot(7000, r, &QNetworkReply::abort);
                QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                loop.exec();
                tunnelOk = r->error() == QNetworkReply::NoError;
                r->deleteLater();
            }
            if (tunnelOk) {
                auto *r = nam.get(QNetworkRequest(QUrl(QStringLiteral("https://cp.cloudflare.com/cdn-cgi/trace"))));
                QEventLoop loop;
                QTimer::singleShot(7000, r, &QNetworkReply::abort);
                QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                loop.exec();
                if (r->error() == QNetworkReply::NoError) {
                    for (const auto &ln: QString::fromUtf8(r->readAll()).split('\n')) {
                        if (ln.startsWith("ip=")) { exitIp = ln.mid(3).trimmed(); break; }
                    }
                }
                r->deleteLater();
            }
        }

        runOnUiThread([=] {
            dlg->close();
            dlg->deleteLater();

            QString verdict;
            if (!net)
                verdict = tr("Нет интернета — проверьте подключение к сети.");
            else if (!dns)
                verdict = tr("Интернет есть, но DNS не отвечает — попробуйте сменить DNS-сервер.");
            else if (host.isEmpty())
                verdict = tr("Сеть в порядке. Сервер не выбран — импортируйте подписку и выберите сервер.");
            else if (!tcp)
                verdict = tr("Сервер недоступен из вашей сети — возможно, блокировка провайдера или сервер выключен. Попробуйте другой сервер.");
            else if (!tls)
                verdict = tr("Сервер отвечает, но TLS не проходит — вероятна фильтрация (DPI). Попробуйте другой сервер или порт 443.");
            else if (tunnelTested && !tunnelOk)
                verdict = tr("VPN подключён, но трафик через него не проходит. Частые причины: QUIC/DoH "
                             "в браузере или посторонний перехватчик (Zapret/GoodbyeDPI/WARP).")
#ifdef Q_OS_WIN
                          // Только на Windows: вне её этого пункта в меню нет.
                          + tr(" Нажмите «Починить сеть Windows» в меню «Зелёный Ритм».")
#else
                          + tr(" Проверьте, не запущен ли рядом другой VPN, и отключите его.")
#endif
                    ;
            else if (tunnelTested && tunnelOk)
                verdict = tr("Всё работает — трафик идёт через VPN, внешний IP: %1.").arg(exitIp.isEmpty() ? "—" : exitIp);
            else
                verdict = tr("Сеть и сервер доступны. Нажмите «Подключиться», чтобы пустить трафик через VPN.");

            auto mark = [](bool b) { return b ? QString::fromUtf8("\xE2\x9C\x94") : QString::fromUtf8("\xE2\x9C\x95"); }; // ✔ / ✕
            QStringList rows;
            rows << tr("Интернет: %1").arg(mark(net));
            rows << tr("DNS: %1").arg(mark(dns));
            if (!host.isEmpty()) {
                rows << tr("Сервер (TCP): %1").arg(mark(tcp));
                rows << tr("TLS-соединение: %1").arg(mark(tls));
            }
            if (tunnelTested) {
                rows << tr("Трафик через VPN: %1").arg(mark(tunnelOk));
                if (tunnelOk) rows << tr("Внешний IP: %1").arg(exitIp.isEmpty() ? "—" : exitIp);
            }

            // Support report — no secrets: server host:port is public and helps support,
            // but the subscription token / keys are never included.
            QString report = header + tun_diagnostics_block(tunAdapter) + foreign;
            report += QStringLiteral("Internet: %1\nDNS: %2\n").arg(net ? "OK" : "FAIL", dns ? "OK" : "FAIL");
            if (!host.isEmpty()) {
                report += QStringLiteral("Server %1:%2 TCP: %3\nTLS: %4\n")
                              .arg(host).arg(port).arg(tcp ? "OK" : "FAIL", tls ? "OK" : "FAIL");
            }
            if (tunnelTested) {
                report += QStringLiteral("Tunnel: %1\nExitIP: %2\n").arg(tunnelOk ? "OK" : "FAIL", exitIp.isEmpty() ? "-" : exitIp);
            }
            report += QStringLiteral("Verdict: %1\n").arg(verdict);

            QMessageBox box(QMessageBox::Information, tr("Диагностика соединения"),
                            rows.join("\n") + "\n\n" + verdict, QMessageBox::Ok, GetMessageBoxParent());
            auto *copyBtn = box.addButton(tr("Скопировать отчёт"), QMessageBox::ActionRole);
            auto *tgBtn = box.addButton(tr("Написать в поддержку"), QMessageBox::ActionRole);
            box.exec();
            if (box.clickedButton() == copyBtn) {
                QApplication::clipboard()->setText(report);
            } else if (box.clickedButton() == tgBtn) {
                QApplication::clipboard()->setText(report); // report is on the clipboard to paste
                QDesktopServices::openUrl(QUrl(GreenRhythm::kTelegramUrl));
            }
        });
    });
}

// A short, secret-free preamble for the copyable report and the saved log: what version,
// OS, server and mode — the facts support asks for first. Never the token or keys.
/**
 * Блок про туннель для отчёта в поддержку.
 *
 * ЗАЧЕМ ОН ЕСТЬ. Диагностика умела сказать «трафик через VPN не идёт» и на
 * этом кончалась. Дальше шла переписка на несколько кругов: какой стек, поднят
 * ли адаптер, кто им владеет, что сказало ядро. Каждый круг — это день, и
 * половина ответов приходила про другую сборку. Теперь всё это собирается
 * одной кнопкой и одинаково у всех.
 *
 * Настройки берутся здесь, в потоке интерфейса, — это чтение полей. Систему
 * спрашивает DescribeTunAdapter из рабочего потока, и её строка приходит
 * готовым параметром.
 */
QString MainWindow::tun_diagnostics_block(const QString &adapter) const {
    QStringList v;

    // Внутренний туннель против внешнего. На маке внутреннего нет вовсе, и
    // человек об этом знать не обязан — а поддержке разница нужна сразу.
    v << QStringLiteral("tun-requested=%1").arg(NekoGui::dataStore->spmode_vpn ? "yes" : "no");
    v << QStringLiteral("tun-internal=%1 platform-allows=%2")
             .arg(NekoGui::dataStore->vpn_internal_tun ? "yes" : "no",
                  NekoGui::PlatformSupportsInternalTun() ? "yes" : "no");

    /*
     * СТЕК ПИШЕТСЯ ДВАЖДЫ, И ЭТО НЕ ИЗБЫТОЧНОСТЬ.
     *
     * Ядро переписывает выбор: normalizeTunStack превращает «gvisor» в
     * «mixed» (чистый gvisor рвёт соединения на части машин с Windows), а
     * «system» уважает. Отчёт с одним лишь выбранным значением расходится с
     * журналом ядра, и на выяснение этого уходит лишний круг переписки.
     */
    const auto chosen = Preset::SingBox::VpnImplementation.value(NekoGui::dataStore->vpn_implementation);
    const auto effective = (chosen == QStringLiteral("gvisor") || chosen.isEmpty())
                               ? QStringLiteral("mixed")
                               : chosen;
    v << QStringLiteral("stack=%1%2 mtu=%3 strict-route=%4 ipv6=%5")
             .arg(chosen,
                  chosen == effective ? QString() : QStringLiteral(" (ядро применит %1)").arg(effective))
             .arg(NekoGui::dataStore->vpn_mtu)
             .arg(NekoGui::dataStore->vpn_strict_route ? "on" : "off",
                  NekoGui::dataStore->vpn_ipv6 ? "on" : "off");

    v << QStringLiteral("system=%1").arg(adapter.isEmpty() ? QStringLiteral("-") : adapter);

#ifdef Q_OS_MACOS
    /*
     * Перемещённая копия — самая частая причина «на маке ничего не работает», и
     * снаружи она неотличима от поломки туннеля. Система запускает скачанное
     * приложение из временной копии только на чтение: настройки не пишутся,
     * туннель не поднимается, а человек видит лишь то, что ничего не вышло.
     */
    v << QStringLiteral("translocated=%1")
             .arg(QApplication::applicationDirPath().contains(QStringLiteral("/AppTranslocation/")) ? "YES (перетащите в Программы)" : "no");
#endif

    return QStringLiteral("TUN: ") + v.join(QStringLiteral("; ")) + "\n";
}

QString MainWindow::diagnostics_header() const {
    QString h = QStringLiteral("GreenRhythm %1\nOS: %2\n")
                    .arg(QString(NKR_VERSION), QSysInfo::prettyProductName());
    if (running != nullptr && running->bean != nullptr) {
        h += QStringLiteral("Server: %1:%2\n").arg(running->bean->serverAddress).arg(running->bean->serverPort);
    } else {
        h += QStringLiteral("Server: (не запущен)\n");
    }
    h += QStringLiteral("Mode: %1%2\n")
             .arg(NekoGui::dataStore->spmode_vpn ? "TUN " : "",
                  NekoGui::dataStore->spmode_system_proxy ? "SystemProxy" : (NekoGui::dataStore->spmode_vpn ? "" : "Proxy-only"));

    return h;
}

/**
 * Строка о чужих туннелях для отчёта. ЗВАТЬ ТОЛЬКО ИЗ РАБОЧЕГО ПОТОКА.
 *
 * Вынесено из diagnostics_header намеренно. Та считается в потоке интерфейса —
 * в том числе перед показом окна прогресса, — а поиск чужих туннелей запускает
 * powershell и ждёт его. На обычной машине это единицы секунд, и всё это время
 * окно не перерисовывается: Windows успевает повесить на него «Не отвечает».
 *
 * Смысл строки прежний: заметная доля обращений вида «включил ваш клиент, и
 * перестала открываться рабочая сеть» объясняется именно чужим туннелем, а без
 * неё поддержка узнаёт об этом на третьем письме.
 */
QString MainWindow::foreign_tunnels_line() {
    const auto found = NekoGui_sys::DetectForeignTunnels();
    if (found.isEmpty()) return {};
    return NekoGui_sys::DescribeForeignTunnels(found) + "\n";
}
