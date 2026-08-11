<#
.SYNOPSIS
    Снимок сетевого состояния для поддержки VerdantVibe / GreenRhythm.

.DESCRIPTION
    Запускать ПРИ ВКЛЮЧЁННОМ VPN, когда интернет не работает. Скрипт ничего не
    меняет - только собирает картину и складывает в отчёт, который можно
    отправить в поддержку одним файлом.

    Главное, что он ловит: маршруты при поднятом туннеле. Симптом "включаю VPN -
    интернет пропадает" почти всегда означает, что трафик уходит в туннель, а
    сам туннель до сервера достучаться не может, потому что маршрут до сервера
    тоже завернулся внутрь. Из лога клиента этого не видно, из таблицы - видно.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File collect-report.ps1
#>

$ErrorActionPreference = 'SilentlyContinue'
$out = "$env:USERPROFILE\Desktop\vv-report.txt"
$lines = New-Object System.Collections.ArrayList

function Add($t) { [void]$lines.Add($t); Write-Host $t }
function Sec($t) { Add ""; Add "=== $t ===" }

Add " VerdantVibe - отчёт для поддержки"
Add " дата: $(Get-Date -Format 'dd.MM.yyyy HH:mm:ss')"

Sec "Маршруты по умолчанию (порядок решает всё)"
Get-NetRoute -DestinationPrefix '0.0.0.0/0' |
    Sort-Object RouteMetric |
    Select-Object ifIndex, InterfaceAlias, NextHop, RouteMetric |
    Format-Table -AutoSize | Out-String -Width 120 | ForEach-Object { Add $_ }

Sec "Маршрут до VPN-сервера"
# Если сюда попадает neko-tun, туннель заворачивает сам себя - это и есть обрыв.
$srv = Read-Host " Введите IP сервера из приложения (например 185.237.218.9)"
Add " сервер: $srv"
if ($srv) {
    Get-NetRoute -ErrorAction SilentlyContinue |
        Where-Object { $_.DestinationPrefix -like "$srv*" } |
        Select-Object DestinationPrefix, InterfaceAlias, NextHop, RouteMetric |
        Format-Table -AutoSize | Out-String -Width 120 | ForEach-Object { Add $_ }
    Add " трассировка первых узлов:"
    (Test-NetConnection -ComputerName $srv -TraceRoute -WarningAction SilentlyContinue).TraceRoute |
        Select-Object -First 5 | ForEach-Object { Add "   $_" }
    $t = Test-NetConnection -ComputerName $srv -Port 443 -WarningAction SilentlyContinue
    Add " TCP 443 до сервера: $(if($t.TcpTestSucceeded){'ДОСТУПЕН'}else{'НЕДОСТУПЕН'})"
}

Sec "Адаптеры (ВСЕ, не только активные)"
# Не фильтруем по Status: отключённый или полуживой TAP от другого VPN всё равно
# участвует в выборе маршрута и DNS. Один такой (outline-tap0 с APIPA-адресом)
# на реальной машине не попал в отчёт именно из-за фильтра по Up.
Get-NetAdapter |
    Select-Object Name, InterfaceDescription, ifIndex, Status, LinkSpeed |
    Format-Table -AutoSize | Out-String -Width 150 | ForEach-Object { Add $_ }

Sec "Чужие VPN-адаптеры"
$foreign = Get-NetAdapter | Where-Object {
    $_.InterfaceDescription -match 'TAP|TUN|WireGuard|Wintun|WARP|Outline' -and
    $_.InterfaceDescription -notmatch 'sing-tun'
}
if ($foreign) {
    foreach ($f in $foreign) { Add " ЕСТЬ: $($f.Name) - $($f.InterfaceDescription) [$($f.Status)]" }
    Add " Эти адаптеры конфликтуют с нашим туннелем - их нужно отключить или удалить."
} else { Add " не найдено" }

Sec "IP и шлюзы"
Get-NetIPConfiguration | ForEach-Object {
    Add " $($_.InterfaceAlias): IP=$($_.IPv4Address.IPAddress) GW=$($_.IPv4DefaultGateway.NextHop) DNS=$($_.DNSServer.ServerAddresses -join ',')"
}

Sec "Проверки связи"
foreach ($h in '8.8.8.8', '1.1.1.1', 'ya.ru', 'google.com') {
    $r = Test-NetConnection -ComputerName $h -Port 443 -WarningAction SilentlyContinue -InformationLevel Quiet
    Add " $($h.PadRight(12)) : $(if($r){'OK'}else{'НЕТ'})"
}

Sec "DNS"
foreach ($d in 'ya.ru', 'google.com') {
    $a = (Resolve-DnsName $d -Type A -ErrorAction SilentlyContinue | Select-Object -First 1).IPAddress
    Add " $($d.PadRight(12)) -> $(if($a){$a}else{'НЕ РЕЗОЛВИТСЯ'})"
}

Sec "Локальный прокси приложения"
$listening = $false
foreach ($p in 2080, 2081) {
    $c = Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue
    if ($c) { $listening = $true }
    Add " порт $p : $(if($c){'слушает'}else{'не слушает'})"
}
# Отчёт снятый при остановленном профиле бесполезен, но выглядит как рабочий:
# интернет есть, проверки зелёные - и по нему делают неверный вывод.
$tun = Get-NetAdapter | Where-Object { $_.InterfaceDescription -match 'sing-tun' -and $_.Status -eq 'Up' }
if (-not $listening -and -not $tun) {
    Add ""
    Add " ВНИМАНИЕ: туннель НЕ ЗАПУЩЕН (прокси не слушает, адаптера sing-tun нет)."
    Add " Отчёт снят без VPN и не показывает проблему. Нажмите ПОДКЛЮЧИТЬСЯ в приложении,"
    Add " дождитесь пропадания интернета и запустите скрипт заново."
}
# Прямая проверка через локальный прокси: отделяет "туннель не работает" от
# "туннель работает, но система в него не заходит".
try {
    $r = Invoke-WebRequest -Uri 'http://cp.cloudflare.com' -Proxy 'http://127.0.0.1:2080' -UseBasicParsing -TimeoutSec 10
    Add " запрос через прокси приложения: HTTP $($r.StatusCode) - туннель РАБОТАЕТ"
} catch {
    Add " запрос через прокси приложения: ОШИБКА - $($_.Exception.Message)"
}

Sec "Процессы VPN"
Get-Process | Where-Object { $_.ProcessName -match 'greenrhythm|sing-box|xray|winws|warp' } |
    Select-Object Id, ProcessName | Format-Table -AutoSize | Out-String | ForEach-Object { Add $_ }

$lines | Out-File -FilePath $out -Encoding UTF8
Write-Host ""
Write-Host " Отчёт сохранён: $out" -ForegroundColor Green
Write-Host " Отправьте этот файл в поддержку." -ForegroundColor Green
