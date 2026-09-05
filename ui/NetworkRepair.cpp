#include "ui/mainwindow_common.hpp"

/**
 * Починка сети: снятие чужих перехватчиков и работа с адаптерами.
 *
 * ВЫНЕСЕНО ИЗ mainwindow.cpp, где лежало вперемешку с обработчиками меню,
 * автопилотом и онбордингом. Это самая разрушительная часть программы — она
 * останавливает службы, удаляет драйверы и просит права администратора, — и
 * читать её вместе с кодом отрисовки строки таблицы было нельзя.
 *
 * Методы остались членами MainWindow: объявления в mainwindow.h не тронуты,
 * переехали только тела. Ни одна привязка сигналов об этом не знает.
 */

// Repairs a Windows network stack left broken by OTHER tools. Users who tried
// Zapret/GoodbyeDPI/WARP and "uninstalled" them keep the parts that actually block
// us: the WinDivert driver still filters packets, services still run, and the system
// proxy/DNS still point at a resolver that no longer exists — so our tunnel cannot
// come up and the user only sees "не подключается". Deleting the app folder removes
// none of that, which is why support kept hitting a wall.
//
// Everything here is destructive and needs elevation, so it is strictly opt-in: we
// spell out what will change, require confirmation, and never touch the hosts file —
// on a real machine it held the user's own work entries (corporate hosts, docker),
// and wiping those would break something we were never asked to touch.
void MainWindow::disable_extra_adapters() {
#if defined(Q_OS_MACOS)
    /*
     * НА МАКЕ ТОТ ЖЕ ПУНКТ ПОКАЗЫВАЕТ, А НЕ ВЫКЛЮЧАЕТ.
     *
     * Сначала пункт здесь просто прятался, и это было неверно: вопрос «что за
     * туннели у меня подняты и не мешают ли они» одинаков на обеих платформах,
     * а меню без него выглядит урезанным.
     *
     * Выключения нет намеренно, а не по недоделке. Интерфейсы utun заводит и
     * держит та программа, которая их подняла; «выключить» его снаружи означает
     * оставить владельца в состоянии, которого он не ожидает, — а через минуту
     * тот поднимет его заново. Правильное действие — выйти из той программы, и
     * ровно это здесь и написано.
     */
    const auto found = NekoGui_sys::DetectForeignTunnels();
    if (found.isEmpty()) {
        QMessageBox::information(this, tr("Сторонние туннели"),
                                 tr("Сторонних туннелей не найдено."));
        return;
    }

    QStringList lines;
    for (const auto &t: found) {
        QString one = t.name;
        if (t.ownsHalfTheInternet) {
            one += tr("  —  через него идёт ВЕСЬ трафик; с нашим туннелем он ужиться не сможет");
        } else if (!t.prefixes.isEmpty()) {
            auto shown = t.prefixes.mid(0, 4);
            if (t.prefixes.size() > 4) shown << QStringLiteral("…");
            one += tr("  —  маршруты: ") + shown.join(", ");
        }
        lines << one;
    }

    QMessageBox::information(
        this, tr("Сторонние туннели"),
        tr("Найдено:\n\n%1\n\n"
           "Мы их не трогаем. Выключить туннель можно только в той программе, "
           "которая его подняла.\n\n"
           "Если он вам нужен, перечислите его сети в «Не заводить в туннель» — "
           "тогда они останутся доступны в обход канала.")
            .arg(lines.join("\n")));
#elif !defined(Q_OS_WIN)
    QMessageBox::information(this, tr("Сетевые адаптеры"),
                             tr("Эта функция пока доступна только в Windows и macOS."));
#else
    /*
     * ЭТОТ ПУНКТ ЗАМЕНЯЕТ ТО, ЧТО РАНЬШЕ ДЕЛАЛОСЬ БЕЗ СПРОСА.
     *
     * «Починить сеть Windows» выключала все сторонние TAP/TUN-адаптеры молча —
     * и выключала людям домашний WireGuard, потому что он проходит отбор
     * целиком и по делу: он действительно посторонний туннель. Отличить «остаток
     * от удалённой программы» от «мой туннель до дома» может только человек,
     * поэтому список показывается с маршрутами, все галки сняты, и без явного
     * согласия не выключается ничего.
     */
    const auto found = NekoGui_sys::DetectForeignTunnels();
    if (found.isEmpty()) {
        QMessageBox::information(this, tr("Сетевые адаптеры"),
                                 tr("Сторонних туннелей не найдено."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Отключить лишние сетевые адаптеры"));
    auto *lay = new QVBoxLayout(&dlg);

    auto *note = new QLabel(
        tr("Ниже — сетевые адаптеры, созданные не нами. Среди них может быть ваш "
           "туннель до дома или до работы: если через адаптер идут маршруты, он, "
           "скорее всего, нужен вам.\n\nОтметьте только те, которые точно лишние."),
        &dlg);
    note->setWordWrap(true);
    lay->addWidget(note);

    QList<QCheckBox *> boxes;
    for (const auto &t: found) {
        QString label = t.name;
        if (t.ownsHalfTheInternet) {
            label += tr("  —  через него идёт ВЕСЬ трафик, это почти наверняка ваш VPN");
        } else if (!t.prefixes.isEmpty()) {
            auto shown = t.prefixes.mid(0, 4);
            if (t.prefixes.size() > 4) shown << QStringLiteral("…");
            label += tr("  —  маршруты: ") + shown.join(", ");
        } else {
            label += tr("  —  маршрутов нет, похоже на остаток удалённой программы");
        }
        auto *cb = new QCheckBox(label, &dlg);
        cb->setChecked(false); // ВСЕ СНЯТЫ. Отмеченное по умолчанию однажды нажмут не глядя.
        cb->setProperty("adapter", t.name);
        cb->setProperty("ifIndex", t.ifIndex);
        boxes << cb;
        lay->addWidget(cb);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Отключить отмеченные"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    QStringList chosen;   // для показа человеку
    QStringList indexes;  // для команды
    for (auto *cb: boxes) {
        if (!cb->isChecked()) continue;
        chosen << cb->property("adapter").toString();
        indexes << QString::number(cb->property("ifIndex").toInt());
    }
    if (chosen.isEmpty()) return;

    /*
     * ВЫКЛЮЧАЕМ ПО НОМЕРУ ИНТЕРФЕЙСА, А НЕ ПО ИМЕНИ.
     *
     * Сначала здесь стояло `Disable-NetAdapter -Name $n`, и от подстановки
     * строки защита была — кавычки удваивались. Но у -Name есть свойство, о
     * котором легко забыть: он принимает ПОДСТАНОВОЧНЫЕ ЗНАКИ. Windows заводит
     * адаптеры с именами вида «Local Area Connection* 2» и «Подключение по
     * локальной сети* 2» (Wi-Fi Direct, WAN Miniport) — звёздочка стоит в самом
     * имени. Отметив один такой, человек погасил бы ВСЕ подходящие под шаблон,
     * включая физическую сетевую карту.
     *
     * Номер интерфейса шаблоном быть не может: это число.
     */
    const auto script = QStringLiteral("$idx = @(%1); foreach ($i in $idx) { "
                                       "Get-NetAdapter -InterfaceIndex $i | "
                                       "Disable-NetAdapter -Confirm:$false }")
                            .arg(indexes.join(","));

    const auto rc = WinCommander::runProcessElevated(
        PowerShellPath(), {"-NoProfile", "-NonInteractive", "-Command", script});

    /*
     * ОТЧИТЫВАЕМСЯ ПО ФАКТУ, А НЕ ПО НАМЕРЕНИЮ.
     *
     * Здесь безусловно говорилось «Отключено: …». Человек, отказавшийся от
     * запроса прав администратора, видел то же самое сообщение — и уходил
     * уверенный, что чужой туннель погашен. Дальше он искал причину «не
     * подключается» где угодно, только не там.
     *
     * Поэтому состояние перечитывается заново: называем только те адаптеры,
     * которых в списке чужих туннелей больше НЕТ. Это дороже на один запрос,
     * зато сказанное человеку соответствует тому, что на самом деле произошло.
     */
    QStringList still;
    for (const auto &t: NekoGui_sys::DetectForeignTunnels()) {
        if (chosen.contains(t.name)) still << t.name;
    }

    QStringList gone;
    for (const auto &name: chosen) {
        if (!still.contains(name)) gone << name;
    }

    if (gone.isEmpty()) {
        MW_show_log(tr("Ни один адаптер не отключён."));
        QMessageBox::warning(
            this, tr("Сетевые адаптеры"),
            rc != 0 ? tr("Ничего не отключено — запрос прав администратора не подтверждён.")
                    : tr("Ничего не отключено. Возможно, адаптером управляет другая программа."));
        return;
    }

    MW_show_log(tr("Отключены сетевые адаптеры: %1").arg(gone.join(", ")));

    QString text = tr("Отключено: %1.\n\n"
                      "Включить обратно можно в «Сетевых подключениях» Windows "
                      "или командой Enable-NetAdapter.")
                       .arg(gone.join(", "));
    if (!still.isEmpty()) {
        // Частичный исход — тоже исход, и молчать о нём нельзя.
        text += tr("\n\nНе удалось отключить: %1.").arg(still.join(", "));
    }
    QMessageBox::information(this, tr("Сетевые адаптеры"), text);
#endif
}

/**
 * «Починить сеть» на macOS — свой набор действий, а не отсутствующий пункт.
 *
 * ПОЧЕМУ ПУНКТ ОСТАЁТСЯ. Windows-версия чистит следы Zapret, GoodbyeDPI и WARP;
 * на маке таких программ нет, и первым решением было просто спрятать пункт. Это
 * оказалось неверно: НУЖДА-то остаётся — «включил, а браузер не работает», — и
 * человек, не найдя в меню того, что видел на скриншотах, решает, что у него
 * не та версия. Меню должно совпадать; отличаться может начинка.
 *
 * ЧТО ДЕЛАЕТСЯ ЗДЕСЬ, и все три пункта — про реальные обращения:
 *   1. Возвращаются системные настройки прокси, если мы их оставили. Прошлый
 *      запуск мог кончиться падением, и тогда в системе до сих пор стоит наш
 *      адрес автонастройки, ведущий на порт, которого больше нет: браузер молча
 *      перестаёт открывать что-либо.
 *   2. Сбрасывается кэш имён — после смены маршрутов система нередко держит
 *      старые ответы, и «сайт не открывается» переживает выключение туннеля.
 *   3. Показываются чужие туннели и чужой прокси. ПОКАЗЫВАЮТСЯ, а не трогаются:
 *      выключить их можно только в той программе, которая их подняла.
 *
 * Чужой прокси не трогается ни при каких условиях. Ровно на этом обжёгся
 * вендорный QvProxyConfigurator: он гасит прокси на всех службах, не помня, что
 * там было, — и стирает человеку его корпоративную настройку без возврата.
 */
void MainWindow::repair_macos_network() {
#ifdef Q_OS_MACOS
    const bool ours = NekoGui_sys::MacProxy::OursIsLeftBehind();

    QMessageBox ask(QMessageBox::Question, tr("Починить сеть"),
                    tr("Будет сделано:\n\n"
                       "• системные настройки прокси вернутся к тому, что было до нас;\n"
                       "• сбросится кэш имён (DNS);\n"
                       "• найденные сторонние туннели будут показаны — и НЕ тронуты.\n\n"
                       "Чужой прокси, если он настроен не нами, не трогается.\n\n"
                       "Для сброса кэша имён система спросит пароль. Продолжить?"),
                    QMessageBox::NoButton, this);
    auto *go = ask.addButton(tr("Починить"), QMessageBox::AcceptRole);
    ask.addButton(tr("Отмена"), QMessageBox::RejectRole);
    ask.exec();
    if (ask.clickedButton() != go) return;

    QStringList report;

    if (ours) {
        report << (NekoGui_sys::MacProxy::Disable()
                       ? tr("Настройки прокси возвращены к прежним.")
                       : tr("Вернуть настройки прокси не удалось — возможно, ими управляет организация."));
    } else {
        report << tr("Наших настроек прокси в системе не осталось — возвращать нечего.");
    }

    // Один вызов под правами: два отдельных означали бы два запроса пароля.
    QProcess flush;
    flush.start("osascript",
                {"-e", QStringLiteral("do shell script \"dscacheutil -flushcache; "
                                      "killall -HUP mDNSResponder\" with administrator privileges")});
    // Результат ожидания проверяется, а не выбрасывается: у незавершённого
    // процесса exitCode() равен нулю, и отчёт бодро сообщал об успехе там, где
    // система просто не ответила. Отчёту верят больше всего остального — после
    // такой строки причину «сайты не открываются» ищут где угодно, только не в
    // кэше имён.
    const bool flushDone = flush.waitForFinished(30000);
    if (!flushDone) flush.kill();
    if (!flushDone) {
        report << tr("Кэш имён — не дождались ответа системы.");
    } else if (flush.exitStatus() == QProcess::NormalExit && flush.exitCode() == 0) {
        report << tr("Кэш имён сброшен.");
    } else {
        report << tr("Кэш имён не сброшен — запрос прав не подтверждён.");
    }

    report << QString();
    report << tr("Состояние системных настроек:");
    report << NekoGui_sys::MacProxy::Report();

    const auto foreign = NekoGui_sys::DetectForeignTunnels();
    report << QString();
    report << (foreign.isEmpty()
                   ? tr("Сторонних туннелей не найдено.")
                   : NekoGui_sys::DescribeForeignTunnels(foreign));
    if (!foreign.isEmpty()) {
        report << tr("Выключить их можно только в той программе, которая их подняла.");
    }

    QMessageBox::information(this, tr("Починить сеть"), report.join("\n"));
#endif
}

void MainWindow::repair_windows_network() {
#ifndef Q_OS_WIN
    QMessageBox::information(this, tr("Починить сеть Windows"),
                             tr("Эта функция доступна только в Windows."));
#else
    QMessageBox ask(QMessageBox::Warning, tr("Починить сеть Windows"),
                    tr("Другие программы обхода блокировок (Zapret, GoodbyeDPI, WARP) "
                       "и посторонние VPN перехватывают трафик раньше нашего клиента — "
                       "из-за этого подключение есть, а сайты не открываются.\n\n"
                       "Будут убраны:\n"
                       "• драйверы-перехватчики (WinDivert и подобные);\n"
                       "• их службы, задания и записи автозапуска;\n"
                       "• зависший прокси, мёртвый DNS и кэш DNS.\n\n"
                       "Другие VPN НЕ отключаются: если рядом поднят туннель до дома "
                       "или до работы, он продолжит работать. Найденные туннели и их службы "
                       "будут просто перечислены в отчёте.\n\n"
                       "Ваши файлы, пароли и файл hosts НЕ затрагиваются.\n\n"
                       "Нужны права администратора и перезагрузка. Продолжить?"),
                    QMessageBox::NoButton, this);
    auto *go = ask.addButton(tr("Починить"), QMessageBox::AcceptRole);
    ask.addButton(tr("Отмена"), QMessageBox::RejectRole);
    ask.exec();
    if (ask.clickedButton() != go) return;

    // Written from what actually broke on customer machines, not from a generic list:
    //  - Zapret registers its service under an arbitrary name (winws1, zapret1, ...),
    //    so services and drivers are matched on their ImagePath. Matching by name
    //    reported a clean machine while Zapret was plainly installed on it.
    //  - Its WinDivert driver keeps filtering TCP until a reboot even after the files
    //    are gone, which is why the reboot below is not optional.
    //  - Foreign TUN/TAP adapters (a stale outline-tap0 among them) keep their own DNS
    //    and compete for routing. Раньше их отключали; теперь только называют в
    //    отчёте — почему, подробно у самого прохода ниже. Мёртвый резолвер такого
    //    адаптера при этом всё равно чинится: его ловит отдельная проверка
    //    loopback-DNS, и для неё адаптер выключать не нужно.
    //  - hosts is never touched: a real machine had legitimate work entries in it.
    // Two passes. Most of the work needs admin rights, but two things must run as the
    // logged-in customer, not the elevating admin: the per-user system proxy and the
    // browser DoH check both live in the customer's own profile, and on a standard-user PC
    // the UAC prompt elevates a different account whose HKCU/AppData is the wrong one. So a
    // non-elevated pass runs first as the customer, then the elevated pass does the rest.
    //
    // The scripts are C++11 raw literals (no backslash/quote escaping) written to temp .ps1
    // files with a UTF-8 BOM so PowerShell 5.1 reads the Cyrillic. Written from what actually
    // breaks RU machines:
    //  - bypass tools register services under arbitrary names and via nssm, so services are
    //    matched on ImagePath, Name AND DisplayName; ByeDPI's real binary is ciadpi.exe
    //    (+proxifyre.exe / the bdmanager supervisor), which the old 'byedpi' token never hit.
    //  - the forced reboot used to re-arm the tool from its Run key / Startup shortcut; those
    //    are now removed first, ours excluded by the 'greenrhythm' marker, and the HKCU Run
    //    key is swept per real-user SID under HKEY_USERS so the elevating admin's hive is not
    //    the one we look at.
    //  - stopping WARP's service leaves the adapter on WARP's dead loopback DNS (127.0.2.x),
    //    so the machine could resolve nothing; loopback resolvers are probed and only the
    //    dead ones reset — never a live AdGuard/Acrylic the user installed on purpose.
    //  - kill-switch WFP filters, the ndisrd driver and browser Secure-DNS are only reported,
    //    never touched: they explain "clean machine still broken" without us deleting an
    //    antivirus or a setting the user needs.
    //  - ЧУЖИЕ АДАПТЕРЫ БОЛЬШЕ НЕ ОТКЛЮЧАЮТСЯ, ТОЛЬКО НАЗЫВАЮТСЯ. Здесь стоял
    //    Disable-NetAdapter по всему, чьё описание совпадало с 'TAP|TUN|WireGuard|
    //    Wintun|WARP|Outline'. Защита была одна — не трогать несущего маршрут по
    //    умолчанию, — и она прикрывала только ПОЛНЫЙ чужой туннель. Домашний
    //    WireGuard с AllowedIPs = 192.168.1.0/24 проходил все условия и выключался:
    //    описание совпадает, не sing-tun, не железо, 0.0.0.0/0 у него нет. Владелец
    //    сообщил об этом словами «чтобы он не убивал второй интерфейс, который
    //    поднимается туннелем до дома».
    //
    //    Убрано целиком, а не подправлен список слов: 'Wintun' совпадает с
    //    WireGuard и без токена 'WireGuard', а 'TUN' совпадает подстрокой почти со
    //    всем. Правка одного слова закрыла бы один случай и оставила мину.
    //
    //    Отключение и не было нужно для задачи этой функции. Она снимает
    //    Zapret/GoodbyeDPI/WARP, и они вычищаются выше — по службам, драйверам,
    //    процессам, заданиям и автозапуску. Адаптер, за которым не осталось живой
    //    службы, трафик не перехватывает: он просто висит. То есть платили риском
    //    убить чужую рабочую сеть за нулевой выигрыш, причём необратимо для
    //    человека: включить адаптер обратно приложение не умеет.
    //
    //    Теперь такой адаптер попадает в отчёт вместе со своими маршрутами — тем
    //    же приёмом, каким уже сообщается про WFP-фильтры и ndisrd. Оператор видит
    //    достаточно, чтобы принять решение сам. Осознанное отключение живёт
    //    отдельным пунктом меню, где список показывается с галочками.
    //  - 'netsh winsock reset' ВЫПОЛНЯЕТСЯ НЕ ВСЕГДА. Он шёл безусловно, даже когда
    //    весь проход не нашёл ничего: обнулял каталог LSP (а там бывают записи
    //    корпоративного антивируса) и требовал перезагрузки в ответ на «у меня всё
    //    чисто». Теперь только если сняли фильтрующий драйвер или нашли ndisrd —
    //    то есть когда есть что чинить. Тот же довод, что и у адаптеров:
    //    разрушительное действие без доказательства, что оно нужно.
    //  - 'netsh int ip reset' is skipped when a PHYSICAL adapter has a static IP (offices),
    //    so we do not wipe a fixed LAN address; Hyper-V/WSL virtual adapters do not count.
    //  - hosts is never touched: real customers had legitimate work entries in it.
    static const char *const kUserPs = R"PS(
$u=@()
$k='HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
if((Get-ItemProperty $k -Name ProxyEnable -EA SilentlyContinue).ProxyEnable){$u+='системный прокси отключён'}
Set-ItemProperty $k ProxyEnable 0 -EA SilentlyContinue
if((Get-ItemProperty $k -Name AutoConfigURL -EA SilentlyContinue).AutoConfigURL){$u+='PAC-скрипт удалён'; Remove-ItemProperty $k AutoConfigURL -EA SilentlyContinue}
$t='winws|zapret|goodbyedpi|byedpi|ciadpi|proxifyre|spoofdpi|powertunnel'
$rk='HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if(Test-Path $rk){$ip=Get-ItemProperty $rk; foreach($n in (Get-Item $rk).Property){$v=[string]$ip.$n; if($v -match $t -and $v -notmatch 'greenrhythm'){$u+=('автозапуск удалён: '+$n); Remove-ItemProperty $rk $n -EA SilentlyContinue}}}
$sf=[Environment]::GetFolderPath('Startup')
if($sf -and (Test-Path $sf)){foreach($f in Get-ChildItem $sf -File -EA SilentlyContinue){$c=''; if($f.Extension -match '\.(bat|cmd|ps1)$'){$c=Get-Content $f.FullName -Raw -EA SilentlyContinue} elseif($f.Extension -eq '.lnk'){try{$sh=New-Object -ComObject WScript.Shell; $l=$sh.CreateShortcut($f.FullName); $c=$l.TargetPath+' '+$l.Arguments}catch{}}; if($c -match $t){$u+=('автозапуск удалён: '+$f.Name); Remove-Item $f.FullName -Force -EA SilentlyContinue}}}
foreach($b in @(@('Chrome',"$env:LOCALAPPDATA\Google\Chrome\User Data\Local State"),@('Edge',"$env:LOCALAPPDATA\Microsoft\Edge\User Data\Local State"),@('Yandex',"$env:LOCALAPPDATA\Yandex\YandexBrowser\User Data\Local State"),@('Opera',"$env:APPDATA\Opera Software\Opera Stable\Local State"))){if(Test-Path $b[1]){try{$j=Get-Content $b[1] -Raw|ConvertFrom-Json; if($j.dns_over_https.mode -eq 'secure'){$u+=('браузер '+$b[0]+': включён Безопасный DNS')}}catch{}}}
foreach($pf in Get-ChildItem "$env:APPDATA\Mozilla\Firefox\Profiles" -Directory -EA SilentlyContinue){$pj=Join-Path $pf.FullName 'prefs.js'; if((Test-Path $pj) -and ((Get-Content $pj -Raw -EA SilentlyContinue) -match 'network\.trr\.mode",\s*3')){$u+='браузер Firefox: включён строгий DoH'}}
if($u.Count){$u -join [Environment]::NewLine}
)PS";

    static const char *const kAdminPs = R"PS(
$ErrorActionPreference='SilentlyContinue'
$log=@()
$lsp=$false
$t='winws|windivert|zapret|goodbyedpi|byedpi|ciadpi|proxifyre|spoofdpi|powertunnel'
foreach($s in Get-CimInstance Win32_Service | Where-Object {$_.PathName -match $t -or $_.Name -match $t -or $_.DisplayName -match $t}){if($Own -and $s.PathName -and $s.PathName.ToLower().Contains($Own)){$log+=('наш модуль обхода, служба (НЕ тронута): '+$s.Name); continue}; $log+=('служба: '+$s.Name); sc.exe stop $s.Name|Out-Null; sc.exe config $s.Name start= disabled|Out-Null; sc.exe delete $s.Name|Out-Null}
foreach($n in 'WinDivert','WinDivert1.4','WinDivert14'){if(Get-Service $n -EA SilentlyContinue){$ip=(Get-CimInstance Win32_SystemDriver -Filter "Name='$n'" -EA SilentlyContinue).PathName; if($Own -and $ip -and $ip.ToLower().Contains($Own)){$log+=('наш модуль обхода, драйвер (НЕ тронут): '+$n); continue}; $log+=('драйвер: '+$n); $lsp=$true; sc.exe stop $n|Out-Null; sc.exe delete $n|Out-Null}}
foreach($d in Get-CimInstance Win32_SystemDriver | Where-Object {$_.PathName -match 'divert|zapret|winws'}){if($Own -and $d.PathName -and $d.PathName.ToLower().Contains($Own)){$log+=('наш модуль обхода, драйвер (НЕ тронут): '+$d.Name); continue}; $log+=('драйвер: '+$d.Name); $lsp=$true; sc.exe stop $d.Name|Out-Null; sc.exe delete $d.Name|Out-Null}
foreach($s in Get-Service | Where-Object {$_.Name -match 'warp|cloudflare|outline|amnezia' -or $_.DisplayName -match 'warp|cloudflare|outline|amnezia'}){$log+=('чужой VPN (НЕ тронут): '+$s.Name+' ['+$s.Status+']')}
foreach($p in Get-Process | Where-Object {$_.ProcessName -match 'winws|goodbyedpi|zapret|byedpi|ciadpi|proxifyre|bdmanager|spoofdpi|powertunnel|warp-svc'}){$pp=''; try{$pp=$p.Path}catch{}; if($Own -and $pp -and $pp.ToLower().StartsWith($Own)){$log+=('наш модуль обхода, процесс (НЕ тронут): '+$p.ProcessName); continue}; $log+=('процесс: '+$p.ProcessName); Stop-Process -Id $p.Id -Force}
foreach($tk in Get-ScheduledTask | Where-Object {($_.Actions.Execute -match $t) -or ($_.Actions.Arguments -match $t)}){$log+=('задание: '+$tk.TaskName); $tk | Unregister-ScheduledTask -Confirm:$false}
$runkeys=@('HKLM:\Software\Microsoft\Windows\CurrentVersion\Run','HKLM:\Software\Wow6432Node\Microsoft\Windows\CurrentVersion\Run')
foreach($sid in (Get-ChildItem 'Registry::HKEY_USERS' | Where-Object {$_.PSChildName -match '^S-1-5-21-' -and $_.PSChildName -notmatch '_Classes$'})){$runkeys+="Registry::HKEY_USERS\$($sid.PSChildName)\Software\Microsoft\Windows\CurrentVersion\Run"}
foreach($rk in $runkeys){if(Test-Path $rk){$ip=Get-ItemProperty $rk; foreach($n in (Get-Item $rk).Property){$v=[string]$ip.$n; if($v -match $t -and $v -notmatch 'greenrhythm'){$log+=('автозапуск: '+$n); Remove-ItemProperty $rk $n}}}}
$cs=[Environment]::GetFolderPath('CommonStartup')
if($cs -and (Test-Path $cs)){foreach($f in Get-ChildItem $cs -File){$c=''; if($f.Extension -match '\.(bat|cmd|ps1)$'){$c=Get-Content $f.FullName -Raw} elseif($f.Extension -eq '.lnk'){try{$sh=New-Object -ComObject WScript.Shell; $l=$sh.CreateShortcut($f.FullName); $c=$l.TargetPath+' '+$l.Arguments}catch{}}; if($c -match $t){$log+=('автозапуск: '+$f.Name); Remove-Item $f.FullName -Force}}}
$defIdx=@(Get-NetRoute -DestinationPrefix '0.0.0.0/0' -EA SilentlyContinue | Sort-Object RouteMetric | Select-Object -ExpandProperty InterfaceIndex)
foreach($a in Get-NetAdapter | Where-Object {$_.InterfaceDescription -match 'TAP|TUN|WireGuard|Wintun|WARP|Outline' -and $_.InterfaceDescription -notmatch 'sing-tun' -and $_.Status -ne 'Disabled' -and -not $_.HardwareInterface}){
$pfx=@(Get-NetRoute -InterfaceIndex $a.ifIndex -EA SilentlyContinue | Select-Object -ExpandProperty DestinationPrefix | Where-Object {$_ -notmatch '/32$' -and $_ -notmatch '/128$' -and $_ -ne '224.0.0.0/4' -and $_ -ne 'ff00::/8'} | Sort-Object -Unique)
if($a.ifIndex -in $defIdx){$m=' — через него идёт весь трафик'}elseif($pfx){$m=' — маршруты: '+($pfx -join ', ')}else{$m=''}
$log+=('сторонний туннель (НЕ тронут): '+$a.Name+' ['+$a.InterfaceDescription+']'+$m)}
foreach($i in Get-DnsClientServerAddress | Where-Object {$_.ServerAddresses -match '^127\.|^::1$|^fd01:db8:1111'}){$dead=@($i.ServerAddresses | Where-Object {$_ -match '^127\.|^::1$|^fd01:db8:1111'} | Where-Object { -not (Resolve-DnsName -Name 'dns.msftncsi.com' -Server $_ -DnsOnly -QuickTimeout -EA SilentlyContinue) }); if($dead){$log+=('мёртвый DNS '+($dead -join ',')+' сброшен: '+$i.InterfaceAlias); Set-DnsClientServerAddress -InterfaceIndex $i.InterfaceIndex -ResetServerAddresses}}
$wf=Join-Path (Split-Path -Parent $Report) 'gr_wfp.xml'; netsh wfp show state file="$wf"|Out-Null
if(Test-Path $wf){try{$w=[xml](Get-Content $wf -Raw); $nm=@($w.wfpstate.providers.item|ForEach-Object{$_.displayData.name})+@($w.wfpstate.subLayers.item|ForEach-Object{$_.displayData.name}); foreach($x in ($nm|Where-Object{$_}|Sort-Object -Unique)){if($x -notmatch 'Microsoft|Windows|MPSSVC|NetIo|FWPM|Teredo|IPsec|WSH|sing-?(box|tun)|Hyper-V|WNV|WSL|Built-in'){$log+=('сетевой фильтр (НЕ удалён): '+$x)}}}catch{}; Remove-Item $wf -Force}
if(Get-CimInstance Win32_SystemDriver | Where-Object {$_.Name -eq 'ndisrd' -and $_.State -eq 'Running'}){$log+='драйвер ndisrd (ProxiFyre/WireSock, НЕ удалён)'; $lsp=$true}
Set-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' ProxyEnable 0 -EA SilentlyContinue
Remove-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' AutoConfigURL -EA SilentlyContinue
netsh winhttp reset proxy|Out-Null
if($lsp){netsh winsock reset|Out-Null; $log+='стек Winsock сброшен — нужна перезагрузка'}else{$log+='стек Winsock не сбрасывался — фильтрующих драйверов не найдено'}
$phys=Get-NetAdapter -Physical | Where-Object {$_.Status -eq 'Up'} | Select-Object -ExpandProperty ifIndex
$sp=Get-NetIPInterface -AddressFamily IPv4 | Where-Object {$_.Dhcp -eq 'Disabled' -and $_.ConnectionState -eq 'Connected' -and $phys -contains $_.InterfaceIndex}
if($sp){$log+='статический IP — глубокий сброс IP пропущен'}else{netsh int ip reset|Out-Null; netsh int ipv6 reset|Out-Null}
ipconfig /flushdns|Out-Null
$out = if($log.Count){$log -join [Environment]::NewLine}else{'NOTHING'}
Set-Content -LiteralPath $Report -Value $out -Encoding UTF8
)PS";

    // НИ ОДНОГО ИСПОЛНЯЕМОГО ФАЙЛА НА ДИСКЕ.
    //
    // Прежде тела обоих скриптов писались в общий %TEMP% под постоянными
    // именами (gr_fixnet_user.ps1, gr_fixnet_admin.ps1), и привилегированный
    // запускался оттуда. Между записью и запуском проходило до МИНУТЫ — столько
    // ждал непривилегированный проход выше. Всё это время файл, который вот-вот
    // выполнится с правами администратора, лежал в каталоге, доступном на
    // запись любой программе обычного пользователя. Ловить гонку не требовалось.
    //
    // -EncodedCommand принимает тело скрипта аргументом, поэтому подменять
    // нечего: файла не существует. Отчёт остаётся файлом (перенаправить вывод
    // из-под Start-Process -Verb RunAs нельзя), но лежит в каталоге со
    // случайным именем, который создаётся одной операцией.
    QTemporaryDir work;
    if (!work.isValid()) {
        QMessageBox::warning(this, tr("Починить сеть Windows"),
                             tr("Не удалось подготовить очистку (нет доступа к временной папке)."));
        return;
    }
    const QString report = work.filePath("report.txt");

    // $Report перестал быть параметром: у -EncodedCommand нет привязки
    // параметров, а param() обязан быть первой строкой скрипта. Путь
    // подставляется в текст; одинарные кавычки удваиваются на случай пути с
    // апострофом — в имени пользователя он встречается.
    QString reportLiteral = QDir::toNativeSeparators(report);
    reportLiteral.replace(QStringLiteral("'"), QStringLiteral("''"));
    /*
     * $Own — НАШ СОБСТВЕННЫЙ КАТАЛОГ ОБХОДА, И ОН ОБЯЗАН БЫТЬ ИСКЛЮЧЁН.
     *
     * Скрипт ниже ищет перехватчики по маске winws|windivert|zapret|… в пути,
     * имени и описании службы. Маска верная — она и должна ловить чужое, — но
     * своего от чужого она не отличает вовсе: драйвер WinDivert на машине один
     * на всех, и служба у него одна. Появись у нас собственный модуль обхода,
     * эта же кнопка снесла бы его при первом нажатии, а человек увидел бы, что
     * «починка сети» ломает нашу же защиту.
     *
     * Отличаем ПО ПУТИ, а не по имени: имя процесса и имя службы у нашего и
     * чужого winws одинаковы, переименование не спасает и выглядит как попытка
     * спрятаться. Путь — единственный признак, который нельзя подделать
     * случайно.
     *
     * Сравнение в нижнем регистре: PathName у служб приходит как записан в
     * реестре, регистр там произвольный.
     */
    QString ownDir = QDir::toNativeSeparators(QApplication::applicationDirPath() + "/dpi").toLower();
    ownDir.replace(QStringLiteral("'"), QStringLiteral("''"));

    QString adminScript = QString::fromUtf8(kAdminPs);
    adminScript.prepend(QStringLiteral("$Report='%1'\n$Own='%2'\n").arg(reportLiteral, ownDir));

    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Customer-context pass: runs as the logged-in user, so its HKCU proxy and AppData
    // browser paths are the right ones. Its stdout is the report, no file needed.
    QProcess up;
    up.start(PowerShellPath(),
             {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-EncodedCommand",
              PowerShellEncode(QString::fromUtf8(kUserPs))});
    // Результат ожидания проверяется, а не выбрасывается: при истечении срока
    // вывод пуст, и «проход отработал и ничего не нашёл» стало бы неотличимо от
    // «проход не дошёл до конца».
    const bool userDone = up.waitForFinished(60000);
    if (!userDone) up.kill();
    const QString userOut =
        userDone ? QString::fromUtf8(up.readAllStandardOutput()).trimmed() : QString();

    // Elevated pass: the destructive work. Аргументы передаются массивом, а не
    // одной строкой: так их нечем разделить, каким бы ни было содержимое.
    const QString launcher =
        QString("Start-Process -FilePath '%1' -Verb RunAs -WindowStyle Hidden -Wait "
                "-ArgumentList '-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass',"
                "'-EncodedCommand','%2'")
            .arg(QDir::toNativeSeparators(PowerShellPath()), PowerShellEncode(adminScript));

    QProcess proc;
    proc.start(PowerShellPath(),
               {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", launcher});
    if (!proc.waitForFinished(240000)) {
        proc.kill();
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, tr("Починить сеть Windows"),
                             tr("Очистка не завершилась вовремя. Попробуйте ещё раз."));
        return;
    }

    QString adminOut;
    QFile rf(report);
    if (rf.open(QIODevice::ReadOnly)) {
        adminOut = QString::fromUtf8(rf.readAll());
        rf.close();
        QFile::remove(report);
    }
    // Скриптов-файлов больше нет, убирать нечего; каталог отчёта уносит за собой
    // QTemporaryDir при выходе из функции.
    QApplication::restoreOverrideCursor();

    // Set-Content -Encoding UTF8 prepends a BOM; strip it so 'NOTHING' and isEmpty() work.
    if (!adminOut.isEmpty() && adminOut.front() == QChar(0xFEFF)) adminOut.remove(0, 1);
    adminOut = adminOut.trimmed();

    if (adminOut.isEmpty()) {
        // The elevated pass always writes at least 'NOTHING', so an empty report means it
        // never ran — almost always a declined UAC prompt, a choice, not an error.
        QMessageBox::warning(this, tr("Починить сеть Windows"),
                             tr("Очистка не выполнена — не были выданы права администратора.\n\n"
                                "Попробуйте ещё раз и подтвердите запрос Windows."));
        return;
    }

    // Combine both passes. "Nothing at all" only when neither pass did anything.
    const bool adminNothing = adminOut.startsWith("NOTHING");
    QStringList parts;
    if (!adminNothing) parts << adminOut;
    if (!userOut.isEmpty()) parts << userOut;
    const QString found = parts.join("\n");
    const bool nothingAll = found.isEmpty();

    // Advisories for the report-only findings the cleaner deliberately does not touch.
    QString extra;
    if (found.contains(QString::fromUtf8("Безопасный DNS")) || found.contains("DoH")) {
        extra += QString::fromUtf8(
            "\n\nВ браузере включён Безопасный DNS. Если сайты не открываются только в браузере — "
            "отключите его: Настройки → Конфиденциальность и безопасность → «Использовать безопасный "
            "DNS-сервер» выключить.");
    }
    if (found.contains(QString::fromUtf8("сетевой фильтр")) || found.contains("ndisrd")) {
        extra += QString::fromUtf8(
            "\n\nНайдены сетевые фильтры сторонних программ — мы их не удаляем. Если после "
            "перезагрузки интернета нет совсем, откройте эту программу (WARP, AmneziaVPN и т.п.), "
            "отключите в ней Kill Switch / «постоянную защиту» и удалите её штатным деинсталлятором.");
    }

    // «Ничего не найдено» нельзя утверждать, если половина проверок не
    // дошла до конца: это разные новости, и вторая требует повторить.
    if (!userDone) {
        extra += tr("\n\nПроверка от вашей учётной записи не завершилась вовремя — "
                    "часть сведений в отчёт не попала. Сделанное выше это не отменяет, "
                    "но список найденного может быть неполным.");
    }

    QString body =
        (nothingAll && userDone)
            ? tr("Посторонних программ не найдено.\n\n"
                 "Сетевые настройки сброшены. Если подключение всё равно "
                 "не работает, перезагрузите компьютер и напишите в поддержку.")
            // Adapters are only disabled, never removed, and we say so:
            // someone who needs a work VPN should not think we deleted it.
            : tr("Сделано:\n\n%1\n\nСетевые настройки сброшены.\n\n"
                 "Чтобы изменения вступили в силу, нужна перезагрузка: "
                 "драйверы-перехватчики остаются в памяти до неё.\n\n"
                 "Адаптеры только отключены, не удалены. Если какой-то из них "
                 "нужен для работы, включите его обратно в «Сетевые подключения» "
                 "(Win+R → ncpa.cpl → правой кнопкой → Включить).")
                  .arg(found);
    body += extra;
    QMessageBox done(QMessageBox::Information, tr("Починить сеть Windows"), body,
                     QMessageBox::NoButton, this);
    auto *reboot = done.addButton(tr("Перезагрузить сейчас"), QMessageBox::AcceptRole);
    done.addButton(tr("Позже"), QMessageBox::RejectRole);
    done.exec();
    if (done.clickedButton() == reboot) {
        // /t 8, not /t 0: we just asked them to trust us with their network stack;
        // killing whatever they have open without warning would be a poor thanks.
        QProcess::startDetached("shutdown", {"/r", "/t", "8"});
    }
#endif
}
