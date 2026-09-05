#include "main/Interference.hpp"

#include <QCoreApplication>
#include <QDir>

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
    } // namespace

    QString snapshotPath() {
        // Рабочий каталог клиента — каталог настроек (main.cpp). Снимок обязан
        // лежать там же, где профили: он должен пережить обновление программы,
        // а каталог с бинарями обновление как раз и заменяет.
        return QDir::current().absoluteFilePath(QStringLiteral("interference-paused.tsv"));
    }

    QString scanScript() {
        // РАЗВЕДКА БЕЗ ПРАВ АДМИНИСТРАТОРА. Get-Service, Get-NetAdapter и
        // Get-Process читаются обычным пользователем; повышение нужно только
        // чтобы что-то остановить. Значит, показать список можно сразу, а UAC
        // спросить один раз и только если человек нажал.
        //
        // Свой каталог исключается по пути — тем же приёмом, что в «Починить
        // сеть»: иначе наш же модуль обхода попал бы в список помех самому себе.
        static const char *const kPs = R"PS(
$ErrorActionPreference='SilentlyContinue'
$own=($env:GR_OWN_DIR).ToLower()
$out=@()
$vpn='warp|cloudflare|outline|amnezia|nordvpn|expressvpn|protonvpn|windscribe|surfshark|hideme|openvpn|wireguard|tailscale|zerotier|hamachi|checkpoint|forticlient|globalprotect|pulsesecure|ivanti|cisco ?anyconnect'
$dpi='winws|zapret|goodbyedpi|byedpi|ciadpi|proxifyre|spoofdpi|powertunnel'
foreach($s in Get-CimInstance Win32_Service | Where-Object {$_.Name -match $vpn -or $_.DisplayName -match $vpn -or $_.Name -match $dpi -or $_.PathName -match $dpi}){
  if($own -and $s.PathName -and $s.PathName.ToLower().Contains($own)){continue}
  $run = if($s.State -eq 'Running'){'1'}else{'0'}
  $corp='checkpoint|forticlient|globalprotect|pulsesecure|ivanti|cisco|anyconnect|zscaler|netskope|sophos|sonicwall|watchguard'
  $risk = if($s.Name -match $corp -or $s.DisplayName -match $corp){'корпоративный клиент: без него может пропасть доступ к рабочей сети'}else{''}
  $out += ('service' + "`t" + $s.Name + "`t" + $s.DisplayName + "`t" + $s.State + ', запуск ' + $s.StartMode + "`t" + $run + "`t1`t" + $risk)
}
foreach($a in Get-NetAdapter | Where-Object {$_.InterfaceDescription -match 'TAP|TUN|WireGuard|Wintun|WARP|Outline|Tailscale|ZeroTier' -and $_.InterfaceDescription -notmatch 'sing-tun' -and $_.Name -ne 'neko-tun' -and -not $_.HardwareInterface}){
  $run = if($a.Status -eq 'Up'){'1'}else{'0'}
  $m=''
  $r=@(Get-NetRoute -InterfaceIndex $a.ifIndex -EA SilentlyContinue | Where-Object {$_.DestinationPrefix -eq '0.0.0.0/0'})
  if($r){$m='забирает весь трафик: у него маршрут по умолчанию'}
  $out += ('adapter' + "`t" + $a.Name + "`t" + $a.InterfaceDescription + "`t" + $a.Status + "`t" + $run + "`t1`t" + $m)
}
foreach($d in Get-CimInstance Win32_SystemDriver | Where-Object {$_.Name -eq 'ndisrd' -or $_.PathName -match 'divert|zapret|winws'}){
  if($own -and $d.PathName -and $d.PathName.ToLower().Contains($own)){continue}
  $run = if($d.State -eq 'Running'){'1'}else{'0'}
  $out += ('driver' + "`t" + $d.Name + "`t" + $d.DisplayName + "`t" + $d.State + "`t" + $run + "`t1`t")
}
foreach($p in Get-Process | Where-Object {$_.ProcessName -match $dpi -or $_.ProcessName -match 'warp-svc|proxifyre'}){
  $pp=''; try{$pp=$p.Path}catch{}
  if($own -and $pp -and $pp.ToLower().StartsWith($own)){continue}
  $out += ('process' + "`t" + $p.ProcessName + "`t" + $p.ProcessName + "`tработает" + "`t1`t0`tзапущено вручную: вернуть сможете только вы сами")
}
$out -join [Environment]::NewLine
)PS";
        return QString::fromUtf8(kPs);
    }

    QList<Meddling> parseScan(const QString &raw) {
        QList<Meddling> out;
        for (const auto &line: raw.split(QChar('\n'))) {
            const auto f = line.trimmed().split(kSep);
            // Шесть полей — минимум; седьмое (риск) может отсутствовать вовсе,
            // если PowerShell склеил пустую строку с концом.
            if (f.size() < 6) continue;
            Meddling m;
            m.kind = kindFromWord(f[0].trimmed());
            m.key = f[1].trimmed();
            m.title = f[2].trimmed();
            m.detail = f[3].trimmed();
            m.running = f[4].trimmed() == QStringLiteral("1");
            m.reversible = f[5].trimmed() == QStringLiteral("1");
            if (f.size() > 6) m.risk = f[6].trimmed();
            if (m.key.isEmpty()) continue;
            if (m.title.isEmpty()) m.title = m.key;
            out << m;
        }
        return out;
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
