#include "main/Interference.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QSet>

namespace GreenRhythm {

    namespace {
        // Разделитель полей — табуляция: в именах служб и описаниях адаптеров
        // она не встречается, а вертикальная черта и точка с запятой встречаются.
        constexpr QChar kSep = QChar('\t');

        Meddler kindFromWord(const QString &w) {
            if (w == QStringLiteral("adapter")) return Meddler::Adapter;
            if (w == QStringLiteral("driver")) return Meddler::Driver;
            if (w == QStringLiteral("process")) return Meddler::Process;
            if (w == QStringLiteral("filter")) return Meddler::Filter;
            return Meddler::Service;
        }

        QString wordFromKind(Meddler k) {
            switch (k) {
                case Meddler::Adapter: return QStringLiteral("adapter");
                case Meddler::Driver: return QStringLiteral("driver");
                case Meddler::Process: return QStringLiteral("process");
                case Meddler::Filter: return QStringLiteral("filter");
                case Meddler::Service: break;
            }
            return QStringLiteral("service");
        }

        /** Одинарные кавычки внутри строки для PowerShell — удвоением. */
        QString ps(const QString &s) {
            QString out = s;
            out.replace(QChar('\''), QStringLiteral("''"));
            return QStringLiteral("'") + out + QStringLiteral("'");
        }

        bool parseV4(const QString &prefix, quint32 *addr, int *bits) {
            const auto parts = prefix.split(QChar('/'));
            if (parts.size() != 2) return false;
            QHostAddress a;
            if (!a.setAddress(parts[0].trimmed())) return false;
            if (a.protocol() != QAbstractSocket::IPv4Protocol) return false;
            bool ok = false;
            const int b = parts[1].trimmed().toInt(&ok);
            if (!ok || b < 0 || b > 32) return false;
            *addr = a.toIPv4Address();
            *bits = b;
            return true;
        }

        quint32 maskOf(int bits) { return bits == 0 ? 0u : (0xFFFFFFFFu << (32 - bits)); }
    } // namespace

    QString snapshotPath() {
        // Рабочий каталог клиента — каталог настроек (main.cpp). Снимок обязан
        // лежать там же, где профили: он должен пережить обновление программы,
        // а каталог с бинарями обновление как раз и заменяет.
        return QDir::current().absoluteFilePath(QStringLiteral("interference-paused.tsv"));
    }

    QStringList tunnelExcludes() {
        // Копия списка из ConfigBuilder (route_exclude_address). Сверяется
        // набором проверок: он читает оба места и требует совпадения.
        return {QStringLiteral("10.0.0.0/8"),     QStringLiteral("172.16.0.0/12"),
                QStringLiteral("192.168.0.0/16"), QStringLiteral("169.254.0.0/16"),
                QStringLiteral("100.64.0.0/10")};
    }

    QString scanScript() {
        // РАЗВЕДКА БЕЗ ПРАВ АДМИНИСТРАТОРА. Get-Service, Get-NetAdapter,
        // Get-NetRoute и Get-Process читаются обычным пользователем; повышение
        // нужно только чтобы что-то остановить. Значит, показать список можно
        // сразу, а UAC спросить один раз и только если человек нажал.
        //
        // Маршруты адаптера уезжают восьмым полем через запятую: именно по ним
        // решается, уживается чужой туннель с нашим или нет. Без них модуль
        // видел бы «туннель» и не видел бы главного — что он несёт.
        static const char *const kPs = R"PS(
$ErrorActionPreference='SilentlyContinue'
$own=($env:GR_OWN_DIR).ToLower()
$out=@()
$vpn='warp|cloudflare|outline|amnezia|nordvpn|expressvpn|protonvpn|windscribe|surfshark|hideme|openvpn|wireguard|tailscale|zerotier|hamachi|vipnet|infotecs|checkpoint|forticlient|globalprotect|pulsesecure|ivanti|anyconnect|zscaler|netskope'
$dpi='winws|zapret|goodbyedpi|byedpi|ciadpi|proxifyre|spoofdpi|powertunnel'
$corp='checkpoint|forticlient|globalprotect|pulsesecure|ivanti|cisco|anyconnect|zscaler|netskope|sophos|sonicwall|watchguard|vipnet|infotecs'
foreach($s in Get-CimInstance Win32_Service | Where-Object {$_.Name -match $vpn -or $_.DisplayName -match $vpn -or $_.Name -match $dpi -or $_.PathName -match $dpi}){
  if($own -and $s.PathName -and $s.PathName.ToLower().Contains($own)){continue}
  $run = if($s.State -eq 'Running'){'1'}else{'0'}
  $risk = if($s.Name -match $corp -or $s.DisplayName -match $corp){'корпоративный клиент: без него может пропасть доступ к рабочей сети'}else{''}
  $out += ('service' + "`t" + $s.Name + "`t" + $s.DisplayName + "`t" + $s.State + ', запуск ' + $s.StartMode + "`t" + $run + "`t1`t" + $risk + "`t")
}
foreach($a in Get-NetAdapter | Where-Object {$_.InterfaceDescription -match 'TAP|TUN|WireGuard|Wintun|WARP|Outline|Tailscale|ZeroTier|ViPNet' -and $_.InterfaceDescription -notmatch 'sing-tun' -and $_.Name -ne 'neko-tun' -and -not $_.HardwareInterface}){
  $run = if($a.Status -eq 'Up'){'1'}else{'0'}
  $pfx=@(Get-NetRoute -InterfaceIndex $a.ifIndex -AddressFamily IPv4 -EA SilentlyContinue | ForEach-Object {$_.DestinationPrefix}) -join ','
  $out += ('adapter' + "`t" + $a.Name + "`t" + $a.InterfaceDescription + "`t" + $a.Status + "`t" + $run + "`t1`t" + "`t" + $pfx)
}
foreach($d in Get-CimInstance Win32_SystemDriver | Where-Object {$_.Name -eq 'ndisrd' -or $_.PathName -match 'divert|zapret|winws'}){
  if($own -and $d.PathName -and $d.PathName.ToLower().Contains($own)){continue}
  $run = if($d.State -eq 'Running'){'1'}else{'0'}
  $out += ('driver' + "`t" + $d.Name + "`t" + $d.DisplayName + "`t" + $d.State + "`t" + $run + "`t1`t`t")
}
foreach($p in Get-Process | Where-Object {$_.ProcessName -match $dpi -or $_.ProcessName -match 'warp-svc|proxifyre'}){
  $pp=''; try{$pp=$p.Path}catch{}
  if($own -and $pp -and $pp.ToLower().StartsWith($own)){continue}
  $out += ('process' + "`t" + $p.ProcessName + "`t" + $p.ProcessName + "`tработает" + "`t1`t0`tзапущено вручную: вернуть сможете только вы сами" + "`t")
}
$out -join [Environment]::NewLine
)PS";
        return QString::fromUtf8(kPs);
    }

    QList<Meddling> parseScan(const QString &raw) {
        QList<Meddling> out;
        for (const auto &line: raw.split(QChar('\n'))) {
            const auto f = line.trimmed().split(kSep);
            if (f.size() < 6) continue;
            Meddling m;
            m.kind = kindFromWord(f[0].trimmed());
            m.key = f[1].trimmed();
            m.title = f[2].trimmed();
            m.detail = f[3].trimmed();
            m.running = f[4].trimmed() == QStringLiteral("1");
            m.reversible = f[5].trimmed() == QStringLiteral("1");
            if (f.size() > 6) m.risk = f[6].trimmed();
            if (f.size() > 7) {
                m.prefixes = f[7].split(QChar(','), Qt::SkipEmptyParts);
                for (auto &p: m.prefixes) p = p.trimmed();
            }
            if (m.key.isEmpty()) continue;
            if (m.title.isEmpty()) m.title = m.key;
            out << m;
        }
        return out;
    }

    bool prefixInside(const QString &inner, const QString &outer) {
        quint32 ia = 0, oa = 0;
        int ib = 0, ob = 0;
        if (!parseV4(inner, &ia, &ib) || !parseV4(outer, &oa, &ob)) return false;
        // Внутренний не может быть шире внешнего: /8 внутри /16 не помещается.
        if (ib < ob) return false;
        const quint32 m = maskOf(ob);
        return (ia & m) == (oa & m);
    }

    QStringList meaningfulPrefixes(const QStringList &prefixes) {
        QStringList out;
        for (const auto &raw: prefixes) {
            const auto p = raw.trimmed();
            if (p.isEmpty()) continue;
            quint32 a = 0;
            int b = 0;
            // Не IPv4 — мимо: у нашего туннеля своя политика по IPv6, и мешать
            // сюда fe80::/64 значит сравнивать несравнимое.
            if (!parseV4(p, &a, &b)) continue;
            // Шум, который есть у КАЖДОГО адаптера и ничего не говорит:
            // групповая рассылка и широковещательный адрес.
            if (prefixInside(p, QStringLiteral("224.0.0.0/4"))) continue;
            if (p == QStringLiteral("255.255.255.255/32")) continue;
            out << p;
        }
        out.removeDuplicates();
        return out;
    }

    Reach reachOf(const QStringList &prefixes) {
        bool lower = false, upper = false;
        bool any = false;
        for (const auto &raw: prefixes) {
            const auto p = raw.trimmed();
            if (p == QStringLiteral("0.0.0.0/0") || p == QStringLiteral("::/0")) return Reach::Everything;
            // ПАРА /1 — ЭТО И ЕСТЬ redirect-gateway. OpenVPN и WireGuard не
            // ставят 0.0.0.0/0: они ставят две половины, каждая длиннее чужого
            // умолчания и потому выигрывающая. Проверяй мы только 0.0.0.0/0 —
            // полнотуннельный клиент попал бы в «уживается».
            if (p == QStringLiteral("0.0.0.0/1")) lower = true;
            if (p == QStringLiteral("128.0.0.0/1")) upper = true;
            any = true;
        }
        if (lower && upper) return Reach::Everything;
        if (!any) return Reach::Unknown;
        return meaningfulPrefixes(prefixes).isEmpty() ? Reach::Unknown : Reach::OwnSubnets;
    }

    QStringList notCoveredByTunnel(const QStringList &prefixes, const QStringList &excludes) {
        QStringList out;
        for (const auto &p: meaningfulPrefixes(prefixes)) {
            bool safe = false;
            for (const auto &e: excludes) {
                if (prefixInside(p, e)) {
                    safe = true;
                    break;
                }
            }
            if (!safe) out << p;
        }
        out.removeDuplicates();
        return out;
    }

    QString familyOf(const QString &text) {
        const auto t = text.toLower();
        struct Pair {
            const char *needle;
            const char *family;
        };
        static const Pair table[]{
            {"vipnet", "vipnet"},        {"infotecs", "vipnet"},      {"итцс", "vipnet"},
            {"openvpn", "openvpn"},      {"tap-windows", "openvpn"},  {"tap0901", "openvpn"},
            {"wireguard", "wireguard"},  {"wintun", "wireguard"},     {"amnezia", "amnezia"},
            {"warp", "cloudflare"},      {"cloudflare", "cloudflare"},{"tailscale", "tailscale"},
            {"zerotier", "zerotier"},    {"outline", "outline"},      {"checkpoint", "corp"},
            {"check point", "corp"},     {"forticlient", "corp"},     {"globalprotect", "corp"},
            {"pulsesecure", "corp"},     {"anyconnect", "corp"},      {"zscaler", "corp"},
            {"netskope", "corp"},        {"ivanti", "corp"},
        };
        for (const auto &p: table) {
            if (t.contains(QString::fromLatin1(p.needle))) return QString::fromLatin1(p.family);
        }
        return {};
    }

    bool protectedFamily(const QString &family) {
        // ViPNet — не просто туннель: сверх него стоит фильтрующий драйвер и
        // своя политика, и остановка ломает не только сеть, но и соответствие
        // требованиям на рабочей машине. Корпоративные клиенты — по той же
        // причине. Показываем и разводим по адресам; останавливать не
        // предлагаем ни при каких условиях.
        return family == QStringLiteral("vipnet") || family == QStringLiteral("corp");
    }

    void classify(QList<Meddling> &items, const QStringList &excludes) {
        // Первый проход: исход каждого адаптера сам по себе.
        QSet<QString> familyTakesAll;
        QSet<QString> familyCoexists;
        for (auto &m: items) {
            m.family = familyOf(m.title + QChar(' ') + m.key);
            if (m.kind != Meddler::Adapter) continue;
            m.reach = reachOf(m.prefixes);
            if (m.reach == Reach::Everything) {
                if (!m.family.isEmpty()) familyTakesAll << m.family;
            } else if (m.reach == Reach::OwnSubnets) {
                m.overlap = notCoveredByTunnel(m.prefixes, excludes);
                if (!m.family.isEmpty()) familyCoexists << m.family;
            }
        }

        // Второй проход: совет. Служба судится по своему семейству — она нужна
        // тому туннелю, который его поднимает, и останавливать её при
        // уживающемся туннеле значит ломать работающее.
        for (auto &m: items) {
            const bool prot = protectedFamily(m.family);

            if (m.kind == Meddler::Adapter) {
                if (m.reach == Reach::Everything) {
                    m.cure = prot ? Cure::Manual : Cure::Pause;
                    m.advice = prot ? QStringLiteral("забирает весь трафик, но останавливать его мы не станем — "
                                                     "это корпоративный туннель; отключите его сами, если нужно")
                                    : QStringLiteral("забирает весь трафик: два маршрута по умолчанию не уживаются, "
                                                     "кто-то должен уступить");
                } else if (m.reach == Reach::OwnSubnets && !m.overlap.isEmpty()) {
                    m.cure = Cure::Separate;
                    m.advice = QStringLiteral("несёт свои подсети, и наш туннель забрал бы у него %1 — "
                                              "это разводится адресами, останавливать не нужно")
                                   .arg(m.overlap.join(QStringLiteral(", ")));
                } else if (m.reach == Reach::OwnSubnets) {
                    m.cure = Cure::Nothing;
                    m.advice = QStringLiteral("уживается: несёт только свои подсети (%1), наш туннель их не трогает")
                                   .arg(m.overlap.isEmpty() ? meaningfulPrefixes(m.prefixes).join(QStringLiteral(", "))
                                                            : QString());
                } else {
                    m.cure = Cure::Nothing;
                    m.advice = m.running ? QStringLiteral("поднят, но маршрутов не несёт — не мешает")
                                         : QStringLiteral("не поднят — не мешает");
                }
                continue;
            }

            if (m.kind == Meddler::Process || !m.reversible) {
                m.cure = Cure::Manual;
                if (m.advice.isEmpty()) m.advice = QStringLiteral("вернуть автоматически не сможем");
                continue;
            }

            // Служба, драйвер.
            if (prot) {
                m.cure = Cure::Manual;
                m.advice = QStringLiteral("корпоративный клиент — останавливать не предлагаем");
            } else if (!m.family.isEmpty() && familyCoexists.contains(m.family)
                       && !familyTakesAll.contains(m.family)) {
                m.cure = Cure::Nothing;
                m.advice = QStringLiteral("нужна своему туннелю, а тот уживается с нашим — трогать не надо");
            } else if (!m.running) {
                m.cure = Cure::Nothing;
                m.advice = QStringLiteral("не работает — не мешает");
            } else {
                m.cure = Cure::Pause;
                m.advice = QStringLiteral("работает и может перехватывать трафик");
            }
        }
    }

    QString pauseScript(const QList<Meddling> &chosen, const QString &snapshotPath) {
        QStringList body;
        body << QStringLiteral("$ErrorActionPreference='SilentlyContinue'");
        body << QStringLiteral("$snap=@()");
        body << QStringLiteral("$log=@()");
        body << QStringLiteral("$sp=%1").arg(ps(snapshotPath));

        // СНАЧАЛА ПЕРЕПИСЬ, ПОТОМ ОСТАНОВКА — правило 2. Прежнее состояние
        // собирается целиком и ложится на диск до первого Stop-Service. Упади мы
        // между двумя действиями при обратном порядке — человек остался бы без
        // рабочего VPN и без записи о том, каким он был.
        for (const auto &m: chosen) {
            if (!m.reversible) continue;
            const auto k = ps(m.key);
            const auto word = wordFromKind(m.kind);
            if (m.kind == Meddler::Service || m.kind == Meddler::Driver) {
                body << QStringLiteral("$o=Get-CimInstance Win32_Service -Filter (\"Name='\"+%1+\"'\") ; "
                                       "if(-not $o){$o=Get-CimInstance Win32_SystemDriver -Filter (\"Name='\"+%1+\"'\")} ; "
                                       "if($o){$snap+=('%2' + \"`t\" + %1 + \"`t\" + $o.StartMode + \"`t\" + $o.State)}")
                                .arg(k, word);
            } else if (m.kind == Meddler::Adapter) {
                body << QStringLiteral("$o=Get-NetAdapter -Name %1 ; if($o){$snap+=('adapter' + \"`t\" + %1 + "
                                       "\"`t\" + $o.AdminStatus + \"`t\" + $o.Status)}")
                                .arg(k);
            }
        }
        body << QStringLiteral("if($snap.Count){Set-Content -LiteralPath $sp -Value ($snap -join "
                               "[Environment]::NewLine) -Encoding UTF8 -Force}");

        for (const auto &m: chosen) {
            if (!m.reversible) continue;
            const auto k = ps(m.key);
            if (m.kind == Meddler::Service || m.kind == Meddler::Driver) {
                // Останавливаем и переводим в «вручную». НЕ в «отключено»:
                // отключённую службу человек не вернёт из окна свойств одним
                // движением, а «вручную» стартует и по требованию, и нашим
                // возвратом.
                body << QStringLiteral("Stop-Service -Name %1 -Force -NoWait ; "
                                       "sc.exe config %1 start= demand | Out-Null ; "
                                       "$log+=('приостановлено: ' + %1)")
                                .arg(k);
            } else if (m.kind == Meddler::Adapter) {
                body << QStringLiteral("Disable-NetAdapter -Name %1 -Confirm:$false ; "
                                       "$log+=('адаптер отключён: ' + %1)")
                                .arg(k);
            }
        }
        body << QStringLiteral("if($log.Count){$log -join [Environment]::NewLine}else{'NOTHING'}");
        return body.join(QChar('\n')) + QChar('\n');
    }

    QString resumeScript(const QString &snapshotPath) {
        // Снимок удаляется ПОСЛЕДНИМ действием. Удали мы его в начале — и
        // прерывание на середине оставило бы половину служб выключенными без
        // единой записи о том, что их надо вернуть.
        static const char *const kPs = R"PS(
$ErrorActionPreference='SilentlyContinue'
$log=@()
if(Test-Path -LiteralPath $sp){
  foreach($line in (Get-Content -LiteralPath $sp)){
    $f=$line -split "`t"
    if($f.Count -lt 4){continue}
    $kind=$f[0]; $key=$f[1]; $start=$f[2]; $state=$f[3]
    if($kind -eq 'adapter'){
      if($start -ne 'Down'){Enable-NetAdapter -Name $key -Confirm:$false; $log+=('адаптер включён обратно: '+$key)}
      continue
    }
    switch($start){
      'Auto' {sc.exe config $key start= auto | Out-Null}
      'Boot' {sc.exe config $key start= boot | Out-Null}
      'System' {sc.exe config $key start= system | Out-Null}
      'Manual' {sc.exe config $key start= demand | Out-Null}
      'Disabled' {sc.exe config $key start= disabled | Out-Null}
    }
    if($state -eq 'Running'){Start-Service -Name $key}
    $log+=('возвращено как было: '+$key+' ['+$start+', '+$state+']')
  }
  Remove-Item -LiteralPath $sp -Force
}
if($log.Count){$log -join [Environment]::NewLine}else{'NOTHING'}
)PS";
        return QStringLiteral("$sp=%1\n").arg(ps(snapshotPath)) + QString::fromUtf8(kPs);
    }

    QStringList snapshotSummary(const QString &snapshot) {
        QStringList out;
        for (const auto &line: snapshot.split(QChar('\n'))) {
            const auto f = line.trimmed().split(kSep);
            if (f.size() < 4) continue;
            const auto what = f[0] == QStringLiteral("adapter") ? QStringLiteral("адаптер") : QStringLiteral("служба");
            out << QStringLiteral("%1 %2 — вернём в состояние «%3, %4»").arg(what, f[1], f[2], f[3]);
        }
        return out;
    }

} // namespace GreenRhythm
