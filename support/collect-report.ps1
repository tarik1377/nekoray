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

Sec "Адаптеры"
Get-NetAdapter | Where-Object Status -eq 'Up' |
    Select-Object Name, InterfaceDescription, ifIndex, LinkSpeed |
    Format-Table -AutoSize | Out-String -Width 140 | ForEach-Object { Add $_ }

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
foreach ($p in 2080, 2081) {
    $c = Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue
    Add " порт $p : $(if($c){'слушает'}else{'не слушает'})"
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
