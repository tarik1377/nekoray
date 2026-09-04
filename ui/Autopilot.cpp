#include "ui/mainwindow_common.hpp"

/**
 * Автопилот: сторож работающего подключения и возвращение домой.
 *
 * ВЫНЕСЕНО ИЗ mainwindow.cpp. Это машина состояний с выдержками, удвоением
 * пауз и условием возврата «две удачные проверки подряд». Такое читается
 * только целиком: разбираться в том, почему клиент не вернулся с резерва,
 * листая между обработчиком меню и отрисовкой таблицы, невозможно.
 */

// «Автопилот» watchdog. Probes the RUNNING tunnel end-to-end (HTTP 204 through the
// local mixed inbound — not a bare server ping) and self-heals on repeated failure,
// encoding the real-world failure modes: panel key rotation → refresh the subscription
// and reconnect; DPI/server death → switch to the best other server; then back off
// with a tray hint at Диагностика. No telemetry; everything stays local.
void MainWindow::autopilot_tick() {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!NekoGui::dataStore->connection_autopilot || NekoGui::dataStore->prepare_exit ||
        running == nullptr || autopilot_probing || now < autopilot_cooldown_until) {
        autopilot_fails = 0;
        if (now >= autopilot_cooldown_until) autopilot_stage = 0;
        autopilot_timer->start(60 * 1000);
        return;
    }
    autopilot_probing = true;
    const auto proxyAddr = NekoGui::dataStore->inbound_address;
    const auto proxyPort = quint16(NekoGui::dataStore->inbound_socks_port);
    runOnNewThread([=] {
        QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, proxyAddr, proxyPort);
        QNetworkAccessManager nam;
        nam.setProxy(proxy);
        auto *reply = nam.get(QNetworkRequest(QUrl(QStringLiteral("http://cp.cloudflare.com/generate_204"))));
        QEventLoop loop;
        QTimer::singleShot(6000, reply, &QNetworkReply::abort);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        runOnUiThread([=] {
            autopilot_probing = false;
            if (running == nullptr) { // stopped while probing
                autopilot_fails = 0;
                conn_health = Health_Unknown;
                autopilot_timer->start(60 * 1000);
                return;
            }
            // Surface the probe result in the status pill regardless of whether we act on it.
            conn_health = ok ? Health_Ok : Health_NoTraffic;
            refresh_status();
            if (ok) {
                autopilot_fails = 0;
                autopilot_stage = 0;
                // Резерв работает — самое время посмотреть, не вернулся ли
                // основной. Именно здесь, а не при отказе: проверять основной,
                // когда и резерв не отвечает, значит искать причину не там.
                if (running != nullptr && running->type == "relay") autopilot_probe_home();
                autopilot_timer->start(60 * 1000);
                return;
            }
            autopilot_fails++;
            MW_show_log(tr("Автопилот: проверка соединения не прошла (%1/2).").arg(autopilot_fails));
            if (autopilot_fails >= 2) {
                autopilot_fails = 0;
                autopilot_recover();
                autopilot_timer->start(30 * 1000); // re-check the fix soon
            } else {
                autopilot_timer->start(15 * 1000); // quick confirm before acting
            }
        });
    });
}

/**
 * Проверка прежнего профиля БЕЗ переключения на него.
 *
 * Обычная проба TCP до адреса сервера, в отдельном потоке. Живой туннель не
 * трогается вовсе — это главное требование: «проверить, вернулся ли основной»
 * не должно обрывать человеку связь ради вопроса, на который может прийти
 * отрицательный ответ.
 *
 * ЧТО ЭТА ПРОБА ДОКАЗЫВАЕТ И ЧЕГО НЕ ДОКАЗЫВАЕТ. Она говорит только «порт
 * снова принимает соединение». Туннель после этого может и не подняться:
 * фильтрация умеет пропускать установку соединения и резать рукопожатие.
 * Поэтому проба — не приговор, а ВОРОТА: не пускает обратно, пока сервер явно
 * недоступен, а окончательный ответ даёт обычная проверка автопилота уже после
 * переключения. Чем сложнее проба, тем больше в ней своих отказов.
 *
 * Возврат — только после ДВУХ удачных подряд: одна удача на дрожащем канале не
 * повод уходить с работающего резерва.
 */
void MainWindow::autopilot_probe_home() {
    if (autopilot_fallback_from < 0) return;

    const auto now = QDateTime::currentMSecsSinceEpoch();

    // Минимальная выдержка. Удваивается с каждым уходом в пределах часа, потолок
    // два часа: канал, который то есть, то нет, иначе гонял бы человека весь день.
    qint64 hold = 10 * 60 * 1000;
    for (int i = 1; i < autopilot_fallback_cycles && hold < 2 * 60 * 60 * 1000; i++) hold *= 2;
    if (hold > 2 * 60 * 60 * 1000) hold = 2 * 60 * 60 * 1000;
    if (now - autopilot_fallback_since < hold) return;

    if (now < autopilot_fallback_next_probe) return;
    autopilot_fallback_next_probe = now + 15 * 60 * 1000;

    auto home = NekoGui::profileManager->GetProfile(autopilot_fallback_from);
    if (home == nullptr) {
        // Профиль удалили, пока мы сидели на резерве. Возвращаться некуда, и
        // держать состояние «мы в отходе» бессмысленно.
        autopilot_fallback_from = -1;
        return;
    }

    const auto id = home->id;
    const auto host = home->bean->serverAddress;
    const auto port = quint16(home->bean->serverPort);
    if (host.isEmpty() || port == 0) return;

    runOnNewThread([this, id, host, port] {
        QTcpSocket probe;
        probe.connectToHost(host, port);
        const bool ok = probe.waitForConnected(6000);
        probe.abort();

        runOnUiThread([this, id, ok] {
            // Пока проба летела, человек мог переключиться руками или уйти на
            // другой профиль. Тогда наше состояние уже не про то.
            if (autopilot_fallback_from != id) return;

            if (ok) {
                autopilot_fallback_ok++;
                MW_show_log(tr("Автопилот: основной сервер отвечает (%1/2).").arg(autopilot_fallback_ok));
            } else {
                autopilot_fallback_ok = 0;
            }
            if (autopilot_fallback_ok < 2) return;

            MW_show_log(tr("Автопилот: основной сервер вернулся — возвращаюсь на него."));
            if (tray != nullptr)
                tray->showMessage(GreenRhythm::kServiceName, tr("Возвращаюсь на основной сервер"));
            autopilot_fallback_from = -1;
            autopilot_fallback_ok = 0;
            autopilot_stage = 0;
            neko_start(id);
        }, this);
    });
}

void MainWindow::autopilot_recover() {
    auto ent = running;
    if (ent == nullptr) return;
    const int gid = ent->gid;
    const int curId = ent->id;
    auto group = NekoGui::profileManager->GetGroup(gid);
    autopilot_stage++;

    const auto giveUp = [this] {
        MW_show_log(tr("Автопилот: восстановить не удалось — запустите «Диагностику соединения» в меню «Зелёный Ритм»."));
        if (tray != nullptr)
            tray->showMessage(GreenRhythm::kServiceName,
                              tr("Не удалось восстановить соединение — откройте «Диагностику соединения»."),
                              QSystemTrayIcon::Warning);
        autopilot_cooldown_until = QDateTime::currentMSecsSinceEpoch() + 5 * 60 * 1000;
        autopilot_stage = 0;
    };

    if (autopilot_stage == 3) {
        // Ступень 3 — резерв. Последний рубеж, а не ещё одна попытка: он
        // платный по трафику и медленнее обычного сервера, поэтому идём сюда,
        // только перебрав всё остальное.
        auto relay = autopilot_relay_profile();
        const bool already = ent->type == "relay";
        if (relay != nullptr && !already && DeviceCredentials::IsProvisioned() &&
            DeviceCredentials::CurrentState() == DeviceCredentials::Active) {
            autopilot_fallback_from = curId;
            autopilot_fallback_since = QDateTime::currentMSecsSinceEpoch();
            autopilot_fallback_ok = 0;

            // Затухание: каждый следующий уход в пределах часа удваивает
            // выдержку. Иначе канал, который «то есть, то нет», гонял бы
            // человека туда-сюда весь день.
            const auto now = autopilot_fallback_since;
            if (now - autopilot_fallback_first > 60 * 60 * 1000) {
                autopilot_fallback_first = now;
                autopilot_fallback_cycles = 0;
            }
            autopilot_fallback_cycles++;

            MW_show_log(tr("Автопилот: обычные серверы не отвечают — перехожу на резервное подключение."));
            if (tray != nullptr)
                tray->showMessage(GreenRhythm::kServiceName,
                                  tr("Перехожу на резервное подключение"));
            neko_start(relay->id);
            return;
        }
        giveUp();
        return;
    }
    if (autopilot_stage > 3) {
        giveUp();
        return;
    }

    if (autopilot_stage == 1 && group != nullptr && !group->url.isEmpty()) {
        // Stage 1 — keys may have rotated on the panel: refresh the subscription,
        // then reconnect to the best profile in the group (ids may change on update).
        MW_show_log(tr("Автопилот: обновляю подписку и переподключаюсь…"));
        if (tray != nullptr)
            tray->showMessage(GreenRhythm::kServiceName, tr("Соединение потеряно — восстанавливаю…"));
        NekoGui_sub::groupUpdater->AsyncUpdate(group->url, gid, [this, gid] {
            runOnUiThread([this, gid] {
                auto g = NekoGui::profileManager->GetGroup(gid);
                if (g == nullptr) return;
                auto ps = g->Profiles();
                if (ps.isEmpty()) return;
                std::shared_ptr<NekoGui::ProxyEntity> best;
                for (const auto &p: ps)
                    if (p->latency > 0 && (best == nullptr || p->latency < best->latency)) best = p;
                if (best == nullptr) best = ps.first();
                neko_start(best->id);
            });
        });
        return;
    }

    // Stage 2 (or stage 1 without a subscription) — switch to the best OTHER server.
    auto ps = group != nullptr ? group->Profiles() : QList<std::shared_ptr<NekoGui::ProxyEntity>>{};
    std::shared_ptr<NekoGui::ProxyEntity> next;
    for (const auto &p: ps) {
        if (p->id == curId) continue;
        // Резерв из этой ступени ИСКЛЮЧЁН намеренно. Он лежит в той же группе и
        // попал бы сюда наравне с обычными серверами — тогда лестница
        // схлопнулась бы: резерв стал бы обычной второй попыткой вместо
        // последнего рубежа, и человек уходил бы на платный по трафику канал
        // раньше, чем перебраны бесплатные.
        if (p->type == "relay") continue;
        if (next == nullptr) next = p;
        else if (p->latency > 0 && (next->latency <= 0 || p->latency < next->latency)) next = p;
    }
    if (next != nullptr) {
        MW_show_log(tr("Автопилот: сервер недоступен, переключаюсь на «%1»…").arg(next->bean->DisplayName()));
        if (tray != nullptr)
            tray->showMessage(GreenRhythm::kServiceName, tr("Переключаюсь на %1").arg(next->bean->DisplayName()));
        neko_start(next->id);
        return;
    }
    giveUp();
}
