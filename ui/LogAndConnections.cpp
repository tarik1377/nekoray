#include "ui/mainwindow_common.hpp"

#include "main/ConnectionRow.hpp"

/**
 * Журнал и список соединений — две поверхности, которыми чинят подключение.
 *
 * ВЫНЕСЕНО ИЗ mainwindow.cpp. У них общая забота, которую видно только рядом:
 * не сорить. Журнал растёт бесконечно, список соединений перерисовывается
 * несколько раз в секунду, и оба легко превращаются в то, из-за чего окно
 * начинает подтормаживать, а поддержка получает бесполезную простыню.
 */

inline QJsonArray last_arr; // format is nekoray_connections_json
// Осталось здесь же, где единственный читатель: прежде лежало в mainwindow.cpp
// за полторы тысячи строк от места, которое его сравнивает.

void MainWindow::show_log_impl(const QString &log) {
    auto lines = SplitLines(log.trimmed());
    if (lines.isEmpty()) return;

    // Strip ANSI colour/escape sequences the sing-box / xray cores emit on stderr —
    // otherwise raw «\x1b[36mINFO\x1b[0m» garbage leaks into the log view.
    static const QRegularExpression ansiRe(QStringLiteral("\x1B\\[[0-9;]*[A-Za-z]"));
    QStringList newLines;
    auto log_ignore = NekoGui::dataStore->log_ignore;
    for (const auto &rawLine: lines) {
        QString line = QString(rawLine).remove(ansiRe);
        bool showThisLine = true;
        for (const auto &str: log_ignore) {
            if (line.contains(str)) {
                showThisLine = false;
                break;
            }
        }
        if (showThisLine) newLines << line;
    }
    if (newLines.isEmpty()) return;

    // Persist to disk before rendering: the on-screen buffer is capped at max_log_line and
    // gone on close, which is why support only ever got screenshots. The file keeps the
    // history a customer can attach in one click.
    append_log_to_file(newLines);

    // Colour-code so routing reads at a glance: [proxy] (foreign) blue,
    // [bypass] (domestic/direct) green, errors red, everything else default.
    {
        QTextCursor cursor(qvLogDocument);
        cursor.movePosition(QTextCursor::End);
        cursor.beginEditBlock();
        for (const auto &line: newLines) {
            cursor.insertBlock();
            QTextCharFormat fmt;
            if (line.contains("ERROR")) {
                fmt.setForeground(QColor(0xE5, 0x48, 0x4D));
            } else if (line.contains("[proxy]")) {
                fmt.setForeground(QColor(0x4C, 0x9A, 0xFF));
            } else if (line.contains("[bypass]")) {
                fmt.setForeground(QColor(0x3F, 0xB9, 0x50));
            }
            cursor.insertText(line, fmt);
        }
        cursor.endEditBlock();
    }
    // From https://gist.github.com/jemyzhang/7130092
    auto block = qvLogDocument->begin();

    while (block.isValid()) {
        if (qvLogDocument->blockCount() > NekoGui::dataStore->max_log_line) {
            QTextCursor cursor(block);
            block = block.next();
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            continue;
        }
        break;
    }
}

void MainWindow::on_masterLogBrowser_customContextMenuRequested(const QPoint &pos) {
    QMenu *menu = ui->masterLogBrowser->createStandardContextMenu();

    auto sep = new QAction(this);
    sep->setSeparator(true);
    menu->addAction(sep);

    auto action_add_ignore = new QAction(this);
    action_add_ignore->setText(tr("Set ignore keyword"));
    connect(action_add_ignore, &QAction::triggered, this, [=] {
        auto list = NekoGui::dataStore->log_ignore;
        auto newStr = ui->masterLogBrowser->textCursor().selectedText().trimmed();
        if (!newStr.isEmpty()) list << newStr;
        bool ok;
        newStr = QInputDialog::getMultiLineText(GetMessageBoxParent(), tr("Set ignore keyword"), tr("Set the following keywords to ignore?\nSplit by line."), list.join("\n"), &ok);
        if (ok) {
            NekoGui::dataStore->log_ignore = SplitLines(newStr);
            NekoGui::dataStore->Save();
        }
    });
    menu->addAction(action_add_ignore);

    auto action_add_route = new QAction(this);
    action_add_route->setText(tr("Save as route"));
    connect(action_add_route, &QAction::triggered, this, [=] {
        auto newStr = ui->masterLogBrowser->textCursor().selectedText().trimmed();
        if (newStr.isEmpty()) return;
        //
        bool ok;
        newStr = QInputDialog::getText(GetMessageBoxParent(), tr("Save as route"), tr("Edit"), {}, newStr, &ok).trimmed();
        if (!ok) return;
        if (newStr.isEmpty()) return;
        //
        auto select = IsIpAddress(newStr) ? 0 : 3;
        QStringList items = {"proxyIP", "bypassIP", "blockIP", "proxyDomain", "bypassDomain", "blockDomain"};
        auto item = QInputDialog::getItem(GetMessageBoxParent(), tr("Save as route"),
                                          tr("Save \"%1\" as a routing rule?").arg(newStr),
                                          items, select, false, &ok);
        if (ok) {
            auto index = items.indexOf(item);
            switch (index) {
                case 0:
                    ADD_TO_CURRENT_ROUTE(proxy_ip, newStr);
                    break;
                case 1:
                    ADD_TO_CURRENT_ROUTE(direct_ip, newStr);
                    break;
                case 2:
                    ADD_TO_CURRENT_ROUTE(block_ip, newStr);
                    break;
                case 3:
                    ADD_TO_CURRENT_ROUTE(proxy_domain, newStr);
                    break;
                case 4:
                    ADD_TO_CURRENT_ROUTE(direct_domain, newStr);
                    break;
                case 5:
                    ADD_TO_CURRENT_ROUTE(block_domain, newStr);
                    break;
                default:
                    break;
            }
            MW_dialog_message("", "UpdateDataStore,RouteChanged");
        }
    });
    menu->addAction(action_add_route);

    // «Сохранить лог…» — one attachment for support instead of a wall of screenshots.
    // Writes the secret-free header plus the full on-disk history (both rotated files).
    auto action_save_log = new QAction(this);
    action_save_log->setText(tr("Сохранить лог в файл…"));
    connect(action_save_log, &QAction::triggered, this, [=] {
        const QString suggested = QDir::homePath() + "/greenrhythm-log-" +
                                  QDateTime::currentDateTime().toString("yyyyMMdd-HHmm") + ".txt";
        const QString dst = QFileDialog::getSaveFileName(GetMessageBoxParent(), tr("Сохранить лог"), suggested,
                                                         tr("Текст (*.txt)"));
        if (dst.isEmpty()) return;
        QFile out(dst);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(GetMessageBoxParent(), tr("Сохранить лог"), tr("Не удалось записать файл."));
            return;
        }
        out.write(diagnostics_header().toUtf8());
        out.write(tun_diagnostics_block({}).toUtf8());
        out.write("\n");
        // Prefer the on-disk history (full); fall back to the on-screen buffer if no file yet.
        bool wroteFile = false;
        for (const QString &src: {log_file_path + ".1", log_file_path}) {
            if (src.isEmpty()) continue;
            QFile in(src);
            if (in.open(QIODevice::ReadOnly)) {
                out.write(in.readAll());
                in.close();
                wroteFile = true;
            }
        }
        if (!wroteFile) out.write(qvLogDocument->toPlainText().toUtf8());
        out.close();
        QMessageBox box(QMessageBox::Information, tr("Сохранить лог"),
                        tr("Лог сохранён:\n%1\n\nПрикрепите этот файл в поддержку.").arg(dst),
                        QMessageBox::Ok, GetMessageBoxParent());
        auto *openBtn = box.addButton(tr("Показать папку"), QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == openBtn)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(dst).absolutePath()));
    });
    menu->addAction(action_save_log);

    auto action_clear = new QAction(this);
    action_clear->setText(tr("Clear"));
    connect(action_clear, &QAction::triggered, this, [=] {
        qvLogDocument->clear();
        ui->masterLogBrowser->clear();
    });
    menu->addAction(action_clear);

    menu->exec(ui->masterLogBrowser->viewport()->mapToGlobal(pos)); // 弹出菜单
}

void MainWindow::refresh_connection_list(const QJsonArray &arr) {

    // Разбор поломок кормится ДО отбора для таблицы: ему нужны и закрытые
    // соединения, а таблица их не показывает. Пока окно закрыто — ни строчки
    // лишней работы.
    if (what_broke != nullptr && !what_broke->watching().isEmpty()) {
        QList<GreenRhythm::Seen> batch;
        batch.reserve(arr.size());
        for (const auto &_item: arr) {
            const auto item = _item.toObject();
            GreenRhythm::Seen seen;
            seen.process = item["Process"].toString();
            seen.tag = item["Tag"].toString();
            seen.network = item["Network"].toString();
            seen.dest = item["Dest"].toString();
            seen.rule = item["Rule"].toString();
            seen.start = item["Start"].toInt();
            seen.end = item["End"].toInt();
            batch += seen;
        }
        what_broke->feed(batch);
    }

    // СРАВНИВАЕМ ТОЛЬКО ЖИВЫЕ, и это не мелочь.
    //
    // Ранний выход «ничего не изменилось» держит таблицу на месте, пока на месте
    // соединения: иначе она перестраивается под руками и теряет и выделение, и
    // место прокрутки. Закрытые записи устаревают каждую секунду, поэтому
    // сравнение по всему ответу не совпадало бы почти никогда, и покой таблицы
    // пропал бы вместе с возможностью что-то в ней прочитать.
    QJsonArray live;
    for (const auto &_item: arr) {
        if (_item.toObject()["End"].toInt() == 0) live += _item;
    }
    if (last_arr == live) return;
    last_arr = live;

    if (NekoGui::dataStore->flag_debug) qDebug() << live;

    // ПРОКРУТКУ И ВЫДЕЛЕНИЕ ВОЗВРАЩАЕМ НА МЕСТО.
    //
    // Таблица собирается заново раз в секунду. Без этого она каждый раз прыгает
    // в начало, и человек, разглядывающий строку в середине списка, теряет её —
    // а смотрят сюда именно тогда, когда что-то не работает. Постоянный порядок
    // строк задаёт ядро; здесь остаётся вернуть взгляд туда, где он был.
    const int keepScroll = ui->tableWidget_conn->verticalScrollBar()->value();
    const int keepRow = ui->tableWidget_conn->currentRow();

    ui->tableWidget_conn->setRowCount(0);

    int nProxy = 0, nDirect = 0, nBlock = 0; // route-map tallies (active connections only)
    int row = -1;
    for (const auto &_item: live) {
        auto item = _item.toObject();
        if (NekoGui::dataStore->ignoreConnTag.contains(item["Tag"].toString())) continue;


        // Count active (not-yet-ended) connections per outbound for the route map.
        if (item["End"].toInt() == 0) {
            const auto t = item["Tag"].toString();
            if (t == "proxy") nProxy++;
            else if (t == "direct" || t == "bypass") nDirect++;
            else if (t == "block") nBlock++;
        }

        row++;
        ui->tableWidget_conn->insertRow(row);

        auto f0 = std::make_unique<QTableWidgetItem>();
        f0->setData(114514, item["ID"].toInt());

        // C0: Status
        auto c0 = new QLabel;
        auto start_t = item["Start"].toInt();
        auto end_t = item["End"].toInt();
        // icon
        auto outboundTag = item["Tag"].toString();
        if (outboundTag == "block") {
            c0->setPixmap(Icon::GetMaterialIcon("cancel"));
        } else {
            if (end_t > 0) {
                c0->setPixmap(Icon::GetMaterialIcon("history"));
            } else {
                c0->setPixmap(Icon::GetMaterialIcon("swap-vertical"));
            }
        }
        c0->setAlignment(Qt::AlignCenter);
        c0->setToolTip(tr("Start: %1\nEnd: %2").arg(DisplayTime(start_t), end_t > 0 ? DisplayTime(end_t) : ""));
        ui->tableWidget_conn->setCellWidget(row, 0, c0);

        // C1: Outbound — humanised + colour-coded so RU/direct vs foreign/proxy is
        // obvious at a glance (helps curate routing rules). Raw tag kept in the tooltip.
        auto f = f0->clone();
        f->setToolTip(outboundTag);
        QString obLabel = outboundTag;
        QColor obColor;
        if (outboundTag == "proxy") {
            obLabel = QString::fromUtf8("\xF0\x9F\x8C\x8D ") + tr("Прокси"); // 🌍 foreign
            obColor = QColor(0x4C, 0x9A, 0xFF);
        } else if (outboundTag == "direct" || outboundTag == "bypass") {
            obLabel = QString::fromUtf8("\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA ") + tr("Напрямую"); // 🇷🇺 domestic
            obColor = QColor(0x3F, 0xB9, 0x50);
        } else if (outboundTag == "block") {
            obLabel = QString::fromUtf8("\xE2\x9B\x94 ") + tr("Блокировка"); // ⛔
            obColor = QColor(0xE5, 0x48, 0x4D);
        }
        f->setText(obLabel);
        if (obColor.isValid()) f->setForeground(QBrush(obColor));
        ui->tableWidget_conn->setItem(row, 1, f);

        // C3: Куда. Имя впереди, адрес хвостом — см. ConnectionRow.hpp: раньше
        // адрес стоял первым в скобках, и имя, которое глаз ищет, было вторым.
        f = f0->clone();
        const QString target1 = item["Dest"].toString();
        const QString target2 = item["RDest"].toString();
        f->setText(GreenRhythm::destinationLabel(target1, target2));
        f->setToolTip(target2.isEmpty() ? target1 : target2 + QStringLiteral("\n") + target1);
        // Голый хост (без порта) — контекстному меню, чтобы собрать правило.
        // Имя предпочтительнее адреса, когда есть оба.
        f->setData(Qt::UserRole,
                   GreenRhythm::hostWithoutPort(!target2.isEmpty() && target2 != target1 ? target2 : target1));

        // C2: Программа. Без процесса и к серверу профиля — это наш собственный
        // туннель, а не неизвестная программа мимо VPN; так и подписываем.
        {
            const QString serverHost = running != nullptr ? running->bean->serverAddress : QString();
            const auto proc = GreenRhythm::programLabel(item["Process"].toString(), target1, serverHost);
            auto fp = new QTableWidgetItem(proc);
            fp->setToolTip(proc == GreenRhythm::tunnelLabel()
                               ? tr("Соединение самого клиента с сервером профиля. Это не программа мимо VPN — "
                                    "это и есть туннель.")
                               : proc);
            if (proc == GreenRhythm::tunnelLabel()) fp->setForeground(QBrush(QColor(0x8B, 0x94, 0x9E)));
            ui->tableWidget_conn->setItem(row, 2, fp);
        }

        ui->tableWidget_conn->setItem(row, 3, f);
    }

    ui->tableWidget_conn->verticalScrollBar()->setValue(keepScroll);
    if (keepRow >= 0 && keepRow < ui->tableWidget_conn->rowCount()) {
        ui->tableWidget_conn->setCurrentCell(keepRow, 0, QItemSelectionModel::NoUpdate);
    }

    // Те же числа — в колонку. Считать их второй раз незачем: карта маршрутов
    // ниже уже прошла по всем строкам и посчитала.
    if (shell != nullptr) {
        // Трафик берём тот, что уже посчитал счётчик защищённого пути: колонка
        // отвечает на вопрос «сколько ушло под защитой», а не на «сколько всего».
        QString down, up;
        if (auto *p = NekoGui_traffic::trafficLooper->proxy; p != nullptr) {
            if (p->downlink + p->uplink > 0) {
                down = ReadableSize(p->downlink);
                up = ReadableSize(p->uplink);
            }
        }
        shell->setLive(nProxy, nDirect, down, up);
        shell->setBypassCount(
            NekoGui::dataStore->vpn_rule_process.split(QChar(0x0A), Qt::SkipEmptyParts).size());
    }

    // Update the route-map strip.
    if (conn_route_summary != nullptr) {
        const int total = nProxy + nDirect + nBlock;
        if (total == 0) {
            conn_route_summary->setText(tr("Нет активных соединений"));
        } else {
            const int W = 44; // bar cells
            int wp = nProxy * W / total, wd = nDirect * W / total;
            int wb = W - wp - wd; // give the remainder to block so the bar is always full
            if (nBlock == 0) { wb = 0; if (nDirect >= nProxy) wd = W - wp; else wp = W - wd; }
            auto seg = [](int n, const QString &color) {
                return n > 0 ? QStringLiteral("<span style='color:%1;'>%2</span>").arg(color, QString(n, QChar(0x2588))) : QString(); // █
            };
            const QString bar = QStringLiteral("<span style='font-family:monospace;font-size:11px;'>%1%2%3</span>")
                                    .arg(seg(wp, "#4C9AFF"), seg(wd, "#3FB950"), seg(wb, "#E5484D"));
            const QString counts =
                QString::fromUtf8("\xF0\x9F\x8C\x8D ") + QStringLiteral("<span style='color:#4C9AFF;'>") + tr("Прокси: %1").arg(nProxy) + "</span>&nbsp;&nbsp;" +
                QString::fromUtf8("\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA ") + QStringLiteral("<span style='color:#3FB950;'>") + tr("Напрямую: %1").arg(nDirect) + "</span>&nbsp;&nbsp;" +
                QString::fromUtf8("\xE2\x9B\x94 ") + QStringLiteral("<span style='color:#E5484D;'>") + tr("Блок: %1").arg(nBlock) + "</span>";
            conn_route_summary->setText(bar + "<br>" + counts);
        }
    }
}

// Right-click a live connection → make a persistent routing rule from its destination.
// Turns "I see youtube.com going direct" into a one-click «Всегда через прокси».
void MainWindow::show_conn_context_menu(const QPoint &pos) {
    auto *cell = ui->tableWidget_conn->itemAt(pos);
    if (cell == nullptr) return;
    auto *destItem = ui->tableWidget_conn->item(cell->row(), 3); // destination moved right when the process column landed
    const QString host = destItem != nullptr ? destItem->data(Qt::UserRole).toString() : QString();
    if (host.isEmpty()) return;

    // ПРОГРАММА ЦЕЛИКОМ, А НЕ ТОЛЬКО АДРЕС.
    //
    // Правило по адресу лечит один сайт, а человек жалуется обычно на ПРОГРАММУ:
    // «игра не работает», «лаунчер не пускает». На телефоне это давно делается
    // галочкой в списке приложений, а здесь надо было знать имя исполняемого
    // файла и куда его вписать. Теперь достаточно найти строку в этой же
    // таблице: имя программы в ней уже есть, и угадывать его не нужно — а
    // угаданное имя не совпадает ни с чем и молча не делает ничего.
    auto *procItem = ui->tableWidget_conn->item(cell->row(), 2);
    const QString program = procItem != nullptr ? procItem->text().trimmed() : QString();
    const bool knownProgram = !program.isEmpty() && program != QStringLiteral("—")
                              && program != GreenRhythm::tunnelLabel();
    const auto bypassList =
        NekoGui::dataStore->vpn_rule_process.split(QChar(0x0A), Qt::SkipEmptyParts);
    bool alreadyDirect = false;
    for (const auto &line: bypassList) {
        if (line.trimmed().compare(program, Qt::CaseInsensitive) == 0) alreadyDirect = true;
    }

    // ОБРАТНОЕ НАПРАВЛЕНИЕ. Список умел только выводить из-под защиты; загнать
    // одну игру В туннель — например ту, чьи серверы фильтруются, — через
    // интерфейс было нельзя, и схему правили руками.
    const auto proxyList =
        NekoGui::dataStore->vpn_rule_process_proxy.split(QChar(0x0A), Qt::SkipEmptyParts);
    bool alreadyProxy = false;
    for (const auto &line: proxyList) {
        if (line.trimmed().compare(program, Qt::CaseInsensitive) == 0) alreadyProxy = true;
    }

    QMenu menu(this);
    QAction *aProgramDirect = nullptr;
    QAction *aProgramBack = nullptr;
    QAction *aProgramProxy = nullptr;
    QAction *aProgramProxyBack = nullptr;
    if (knownProgram) {
        if (alreadyDirect) {
            aProgramBack = menu.addAction(tr("Вернуть «%1» под защиту").arg(program));
        } else if (alreadyProxy) {
            aProgramProxyBack = menu.addAction(tr("Вернуть «%1» под общие правила").arg(program));
        } else {
            aProgramDirect = menu.addAction(tr("Пустить «%1» напрямую").arg(program));
            aProgramProxy = menu.addAction(tr("Пустить «%1» через VPN").arg(program));
        }
        menu.addSeparator();
    }
    auto *aDirect = menu.addAction(QString::fromUtf8("\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA ") + tr("Всегда напрямую: %1").arg(host));   // 🇷🇺
    auto *aProxy = menu.addAction(QString::fromUtf8("\xF0\x9F\x8C\x8D ") + tr("Всегда через прокси: %1").arg(host));               // 🌍
    auto *aBlock = menu.addAction(QString::fromUtf8("\xE2\x9B\x94 ") + tr("Блокировать: %1").arg(host));                          // ⛔
    menu.addSeparator();
    auto *aCopy = menu.addAction(tr("Копировать адрес"));
    auto *chosen = menu.exec(ui->tableWidget_conn->viewport()->mapToGlobal(pos));
    if (chosen == nullptr) return;
    if (chosen == aProgramDirect && aProgramDirect != nullptr) {
        auto list = bypassList;
        list << program;
        NekoGui::dataStore->vpn_rule_process = list.join(QChar(0x0A));
        NekoGui::dataStore->Save();
        MW_show_log(tr("«%1» пойдёт напрямую. Начнёт действовать при следующем подключении.")
                        .arg(program));
        return;
    }
    if (chosen == aProgramBack && aProgramBack != nullptr) {
        QStringList list;
        for (const auto &line: bypassList) {
            if (line.trimmed().compare(program, Qt::CaseInsensitive) != 0) list << line;
        }
        NekoGui::dataStore->vpn_rule_process = list.join(QChar(0x0A));
        NekoGui::dataStore->Save();
        MW_show_log(tr("«%1» снова под защитой. Начнёт действовать при следующем подключении.")
                        .arg(program));
        return;
    }
    if (chosen == aProgramProxy && aProgramProxy != nullptr) {
        auto list = proxyList;
        list << program;
        NekoGui::dataStore->vpn_rule_process_proxy = list.join(QChar(0x0A));
        NekoGui::dataStore->Save();
        MW_show_log(tr("«%1» пойдёт через VPN, даже если общие правила уводят её мимо. "
                       "Начнёт действовать при следующем подключении.")
                        .arg(program));
        return;
    }
    if (chosen == aProgramProxyBack && aProgramProxyBack != nullptr) {
        QStringList list;
        for (const auto &line: proxyList) {
            if (line.trimmed().compare(program, Qt::CaseInsensitive) != 0) list << line;
        }
        NekoGui::dataStore->vpn_rule_process_proxy = list.join(QChar(0x0A));
        NekoGui::dataStore->Save();
        MW_show_log(tr("«%1» снова под общими правилами. Начнёт действовать при следующем подключении.")
                        .arg(program));
        return;
    }
    if (chosen == aCopy) {
        QApplication::clipboard()->setText(host);
    } else if (chosen == aDirect) {
        add_routing_rule(host, 0);
    } else if (chosen == aProxy) {
        add_routing_rule(host, 1);
    } else if (chosen == aBlock) {
        add_routing_rule(host, 2);
    }
}
