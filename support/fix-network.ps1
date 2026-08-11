<#
.SYNOPSIS
    Диагностика и очистка сетевого стека Windows для VerdantVibe / GreenRhythm.

.DESCRIPTION
    Утилиты обхода блокировок (Zapret, GoodbyeDPI, WARP, ByeDPI и т.п.) ставят
    драйверы-перехватчики и правят сетевые настройки. Удаление их папки не
    убирает ни драйвер, ни службу, ни изменённые прокси/DNS — и они продолжают
    перехватывать трафик раньше нашего клиента, из-за чего он не подключается.

    Скрипт сначала показывает, что нашёл. Ничего не меняет, пока не передан -Fix.

.PARAMETER Fix
    Выполнить очистку найденных проблем.

.PARAMETER Aggressive
    Дополнительно сбросить Winsock и стек TCP/IP (требует перезагрузки).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File fix-network.ps1
    powershell -ExecutionPolicy Bypass -File fix-network.ps1 -Fix
#>

[CmdletBinding()]
param(
    [switch]$Fix,
    [switch]$Aggressive
)

$ErrorActionPreference = 'SilentlyContinue'
$script:Found = @()

function Section($t) { Write-Host ""; Write-Host "=== $t ===" -ForegroundColor Cyan }
function Ok($m)      { Write-Host "  [OK]   $m" -ForegroundColor Green }
function Warn($m)    { Write-Host "  [!]    $m" -ForegroundColor Yellow; $script:Found += $m }
function Act($m)     { Write-Host "  [FIX]  $m" -ForegroundColor Magenta }

$isAdmin = ([Security.Principal.WindowsPrincipal] `
            [Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

Write-Host ""
Write-Host " VerdantVibe - проверка сети" -ForegroundColor White
Write-Host " режим: $(if($Fix){'ОЧИСТКА'}else{'только диагностика'})  |  админ: $isAdmin"

if ($Fix -and -not $isAdmin) {
    Write-Host ""
    Write-Host " Для очистки нужен запуск от администратора." -ForegroundColor Red
    Write-Host " Закройте окно, нажмите ПКМ по PowerShell -> 'Запуск от имени администратора'." -ForegroundColor Red
    return
}

# --- 1. Драйверы-перехватчики -------------------------------------------------
# WinDivert — общий движок GoodbyeDPI/Zapret/ByeDPI. Пока драйвер загружен,
# он фильтрует пакеты, даже если сама программа удалена.
Section "Драйверы-перехватчики трафика"
$badDrivers = Get-CimInstance Win32_SystemDriver |
              Where-Object { $_.Name -match 'divert|zapret|byedpi|goodbyedpi' }
if ($badDrivers) {
    foreach ($d in $badDrivers) {
        Warn "драйвер '$($d.Name)' ($($d.State)) - $($d.PathName)"
        if ($Fix) {
            Act "останавливаю и удаляю $($d.Name)"
            & sc.exe stop   $d.Name  | Out-Null
            & sc.exe delete $d.Name  | Out-Null
        }
    }
} else { Ok "перехватчиков не найдено" }

# --- 2. Службы обхода ---------------------------------------------------------
Section "Службы обхода блокировок"
$svcPattern = 'warp|cloudflare|zapret|goodbyedpi|byedpi|amnezia|outline|tun2socks'
$svcs = Get-Service | Where-Object { $_.Name -match $svcPattern -or $_.DisplayName -match $svcPattern }
if ($svcs) {
    foreach ($s in $svcs) {
        Warn "служба '$($s.Name)' - $($s.DisplayName) [$($s.Status)]"
        if ($Fix) {
            Act "останавливаю и перевожу в ручной запуск: $($s.Name)"
            Stop-Service $s.Name -Force
            Set-Service  $s.Name -StartupType Disabled
        }
    }
} else { Ok "посторонних служб нет" }

# --- 3. Процессы --------------------------------------------------------------
Section "Запущенные перехватчики"
$procs = Get-Process | Where-Object { $_.ProcessName -match 'winws|goodbyedpi|zapret|byedpi|warp-svc|tun2socks' }
if ($procs) {
    foreach ($p in $procs) {
        Warn "процесс $($p.ProcessName) (pid $($p.Id))"
        if ($Fix) { Act "завершаю $($p.ProcessName)"; Stop-Process -Id $p.Id -Force }
    }
} else { Ok "лишних процессов нет" }

# --- 4. Системный прокси ------------------------------------------------------
# Утилиты часто прописывают себя прокси и не убирают запись при удалении.
Section "Системный прокси"
$ie = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
if ($ie.ProxyEnable -eq 1) {
    Warn "включён прокси: $($ie.ProxyServer)"
    if ($Fix) {
        Act "выключаю системный прокси"
        Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' ProxyEnable 0
        Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' ProxyServer
    }
} else { Ok "системный прокси выключен" }

if ($ie.AutoConfigURL) {
    Warn "задан скрипт автонастройки: $($ie.AutoConfigURL)"
    if ($Fix) {
        Act "убираю AutoConfigURL"
        Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' AutoConfigURL
    }
}

# netsh отдаёт массив строк - склеиваем, иначе -notmatch вернёт непустой
# список несовпавших строк и условие сработает всегда.
$winhttp = (& netsh winhttp show proxy) -join ' '
if ($winhttp -match 'irect access|рямой доступ') {
    Ok "WinHTTP чист"
} else {
    Warn "WinHTTP-прокси задан: $($winhttp.Trim())"
    if ($Fix) { Act "сбрасываю WinHTTP"; & netsh winhttp reset proxy | Out-Null }
}

# --- 5. DNS -------------------------------------------------------------------
# Ломает связь именно DNS на 127.x - его прописывают локальные резолверы утилит
# обхода, и после их удаления резолвить становится некому. DNS роутера или
# публичный (1.1.1.1, 8.8.8.8) - норма, его не трогаем.
Section "Настройки DNS"
$dnsIssues = $false
Get-NetAdapter | Where-Object Status -eq 'Up' | ForEach-Object {
    $a = $_
    $srv = (Get-DnsClientServerAddress -InterfaceIndex $a.ifIndex -AddressFamily IPv4).ServerAddresses
    if ($srv) {
        $isVirtual = $a.InterfaceDescription -match 'TAP|TUN|sing-box|WireGuard|Wintun'
        $isDead    = $srv | Where-Object { $_ -like '127.*' }
        if ($isDead -and -not $isVirtual) {
            $dnsIssues = $true
            Warn "$($a.Name): DNS указывает в локальный резолвер ($($srv -join ', ')) - вероятно, остаток утилиты"
            if ($Fix) {
                Act "возвращаю DNS на автоматический: $($a.Name)"
                Set-DnsClientServerAddress -InterfaceIndex $a.ifIndex -ResetServerAddresses
            }
        } else {
            Write-Host "  $($a.Name): $($srv -join ', ')"
        }
    }
}
if (-not $dnsIssues) { Ok "проблемных DNS не найдено" }

# --- 6. hosts -----------------------------------------------------------------
# ВАЖНО: hosts НИКОГДА не чистится автоматически. Там почти всегда лежат рабочие
# записи (корпоративные хосты, docker.internal), и их удаление ломает человеку
# работу. Показываем только те строки, что похожи на следы утилит обхода.
Section "Файл hosts"
$hosts = "$env:SystemRoot\System32\drivers\etc\hosts"
$lines = Get-Content $hosts | Where-Object { $_ -match '^\s*[0-9]' -and $_ -notmatch '^\s*#' }
if ($lines) {
    $suspect = $lines | Where-Object {
        $_ -match 'telegram|discord|youtube|google|instagram|facebook|twitter|cloudflare|warp|dns'
    }
    if ($suspect) {
        Warn "подозрительные записи в hosts (могли добавить утилиты обхода):"
        $suspect | ForEach-Object { Write-Host "      $_" -ForegroundColor Yellow }
        Write-Host "      Проверьте вручную: notepad $hosts" -ForegroundColor Yellow
        Write-Host "      Скрипт их НЕ трогает - рядом могут быть ваши рабочие записи." -ForegroundColor Yellow
    } else {
        Ok "записей от утилит обхода не видно ($($lines.Count) собственных записей - не трогаем)"
    }
} else { Ok "hosts чист" }

# --- 7. Виртуальные адаптеры --------------------------------------------------
# Лишние TAP/TUN путают выбор маршрута и ломают игры и мессенджеры.
Section "Виртуальные сетевые адаптеры"
$virt = Get-NetAdapter | Where-Object {
    $_.Status -eq 'Up' -and $_.InterfaceDescription -match 'TAP|TUN|WARP|WireGuard|Wintun'
}
if ($virt) {
    foreach ($v in $virt) { Warn "поднят: $($v.Name) - $($v.InterfaceDescription)" }
    Write-Host "      (наш клиент использует sing-tun; остальные лучше отключить)"
    if ($Fix) {
        foreach ($v in ($virt | Where-Object { $_.InterfaceDescription -notmatch 'sing-tun' })) {
            Act "отключаю адаптер $($v.Name)"
            Disable-NetAdapter -Name $v.Name -Confirm:$false
        }
    }
} else { Ok "лишних адаптеров нет" }

# --- 8. Статические маршруты --------------------------------------------------
Section "Статические маршруты"
$routes = Get-NetRoute -AddressFamily IPv4 |
          Where-Object { $_.DestinationPrefix -eq '0.0.0.0/0' } |
          Sort-Object RouteMetric
$routes | Select-Object InterfaceAlias, NextHop, RouteMetric | Format-Table -AutoSize
if ($routes.Count -gt 1) { Warn "несколько маршрутов по умолчанию - возможен конфликт" }
else { Ok "маршрут по умолчанию один" }

# --- 9. Глубокий сброс --------------------------------------------------------
if ($Fix -and $Aggressive) {
    Section "Глубокий сброс сетевого стека"
    Act "netsh winsock reset";        & netsh winsock reset        | Out-Null
    Act "netsh int ip reset";         & netsh int ip reset         | Out-Null
    Act "ipconfig /flushdns";         & ipconfig /flushdns         | Out-Null
    Write-Host "  ТРЕБУЕТСЯ ПЕРЕЗАГРУЗКА" -ForegroundColor Red
} elseif ($Fix) {
    & ipconfig /flushdns | Out-Null
    Ok "кэш DNS очищен"
}

# --- Итог ---------------------------------------------------------------------
Section "Итог"
if ($script:Found.Count -eq 0) {
    Write-Host "  Проблем не найдено. Если клиент всё равно не подключается -" -ForegroundColor Green
    Write-Host "  пришлите журнал из приложения." -ForegroundColor Green
} elseif ($Fix) {
    Write-Host "  Исправлено пунктов: $($script:Found.Count)" -ForegroundColor Green
    Write-Host "  ПЕРЕЗАГРУЗИТЕ компьютер и запустите приложение заново." -ForegroundColor Yellow
} else {
    Write-Host "  Найдено проблем: $($script:Found.Count)" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Для исправления запустите PowerShell от администратора и выполните:" -ForegroundColor White
    Write-Host "    powershell -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Fix" -ForegroundColor White
}
Write-Host ""
