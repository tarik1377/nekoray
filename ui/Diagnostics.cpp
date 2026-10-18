#include "ui/mainwindow_common.hpp"
#include "ui/dialog_service_health.h"
#include <QCryptographicHash>

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
    if (auto *existing = findChild<DialogServiceHealth *>("grServiceHealth")) {
        ActivateWindow(existing); return;
    }
    auto *dialog = new DialogServiceHealth(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    struct Snapshot { int profile = -1; quint64 core = 0, connection = 0; bool tun = false; };
    auto snapshot = std::make_shared<Snapshot>();
    dialog->captureContext = [this, snapshot] {
        GreenRhythm::HealthContext context;
        context.version = QString(NKR_VERSION);
        context.profileRunning = running != nullptr;
        context.tunEnabled = NekoGui::dataStore->spmode_vpn;
        context.tunStack = Preset::SingBox::VpnImplementation.value(NekoGui::dataStore->vpn_implementation);
        context.mtu = NekoGui::dataStore->vpn_mtu;
        QString address = NekoGui::dataStore->inbound_address;
        if (address == "0.0.0.0" || address.isEmpty()) address = "127.0.0.1";
        if (address == "::") address = "::1";
        // Match the subscription updater/browser probe's HTTP mixed inbound.
        context.profileProxy = QNetworkProxy(QNetworkProxy::HttpProxy, address,
            quint16(NekoGui::dataStore->inbound_socks_port), NekoGui::dataStore->inbound_auth->username,
            NekoGui::dataStore->inbound_auth->password);
        auto group = running ? NekoGui::profileManager->GetGroup(running->gid) : NekoGui::profileManager->CurrentGroup();
        if (group) {
            context.subscription = QUrl(group->url);
            context.cachedProfiles = group->Profiles().size();
            context.lastUpdateOutcome = group->last_update_outcome.load();
            context.lastUpdateUtc = group->sub_last_update;
        }
        context.subscriptionViaProfile = NekoGui::dataStore->sub_use_proxy;
        context.subscriptionUserAgent = NekoGui::dataStore->GetUserAgent();
        if (running && running->bean) {
            context.protocol = running->type;
            const auto identity = running->type + ':' + running->bean->serverAddress.toLower()
                + ':' + QString::number(running->bean->serverPort);
            context.nodeId = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(20));
        }
        const auto routes = QJsonDocument(NekoGui::dataStore->routing->ToJson()).toJson(QJsonDocument::Compact)
            + NekoGui::dataStore->custom_route_global.toUtf8() + NekoGui::dataStore->vpn_rule_process.toUtf8();
        context.routeFingerprint = QString::fromLatin1(QCryptographicHash::hash(routes, QCryptographicHash::Sha256).toHex().left(20));
        snapshot->profile = NekoGui::dataStore->started_id;
#ifndef NKR_NO_GRPC
        snapshot->core = core_process ? core_process->generation() : 0;
#endif
        snapshot->connection = connection_lifecycle.token();
        snapshot->tun = context.tunEnabled;
        return context;
    };
    dialog->contextStillCurrent = [this, snapshot] {
        return snapshot->profile == NekoGui::dataStore->started_id
#ifndef NKR_NO_GRPC
            && snapshot->core == (core_process ? core_process->generation() : 0)
#endif
            && snapshot->connection == connection_lifecycle.token() && snapshot->tun == NekoGui::dataStore->spmode_vpn;
    };
    dialog->health->validateSubscription = [](const QByteArray &body) {
        NekoGui_sub::RawUpdater parser; parser.collect_only = true;
        parser.update(QString::fromUtf8(body));
        return parser.updated_order.size();
    };
    connect(dialog, &DialogServiceHealth::observeDiscordRequested, this, [this, dialog] {
        if (!dialog->contextStillCurrent()) { dialog->health->invalidate(); return; }
        open_what_broke();
        if (what_broke) {
            QObject::disconnect(what_broke, nullptr, dialog->health, nullptr);
            connect(what_broke, &DialogWhatBroke::discordObserved, dialog->health,
                [dialog](int via, int direct, int udp, bool limited, const QDateTime &from, const QDateTime &to) {
                    if (!dialog->contextStillCurrent()) { dialog->health->invalidate(); return; }
                    dialog->health->recordDiscordObservation(via, direct, udp, limited, from, to);
                });
            what_broke->inspect(QStringLiteral("Discord.exe"));
        }
    });
    dialog->show();
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

    // Match normalizeTunStack: only an absent selection defaults to System.
    // Explicit System, Mixed and gVisor selections are preserved by the core.
    const auto chosen = Preset::SingBox::VpnImplementation.value(NekoGui::dataStore->vpn_implementation);
    const auto effective = chosen.isEmpty()
                               ? QStringLiteral("system")
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
