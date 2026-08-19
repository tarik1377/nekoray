# GreenRhythm — диагностика режима TUN
#
# Отвечает на один вопрос: почему через «Режим системного прокси» всё работает,
# а через «Режим TUN» — нет. Это самый частый и самый непонятный отказ, потому что
# соединение с сервером в обоих случаях одно и то же: значит виноват не сервер и не
# подписка, а перехват трафика на уровне сетевого адаптера.
#
# Скрипт ничего не меняет — только читает и печатает отчёт.
#
# Запускать ПРИ ВКЛЮЧЁННОМ режиме TUN, иначе проверять нечего:
#   powershell -NoProfile -ExecutionPolicy Bypass -File diagnose-tun.ps1
#
# Результат целиком отправьте в поддержку.

$ErrorActionPreference = 'SilentlyContinue'
$out = @()
function Add($s) { $script:out += $s; Write-Host $s }
function Head($s) { Add ""; Add ("=" * 62); Add $s; Add ("=" * 62) }

Head "GreenRhythm — диагностика TUN"
Add ("Дата: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Add ("ОС  : " + (Get-CimInstance Win32_OperatingSystem).Caption + " build " + [Environment]::OSVersion.Version.Build)

# --- 1. Запущено ли вообще -------------------------------------------------
Head "1. Приложение и ядро"
$gui = Get-Process greenrhythm -EA SilentlyContinue
$core = Get-Process greenrhythm_core, nekobox_core -EA SilentlyContinue
Add ("GUI запущен  : " + [bool]$gui)
Add ("Ядро запущено: " + [bool]$core)
foreach ($c in $core) {
    if ($c.Path) { Add ("  " + $c.Path + "  -> " + (& $c.Path version 2>&1 | Select-Object -First 1)) }
}

# --- 2. Сам TUN-адаптер ----------------------------------------------------
# Без адреса 172.19.x или без маршрутов туннель «поднят», но трафик не забирает.
Head "2. TUN-адаптер"
$tun = Get-NetAdapter | Where-Object { $_.InterfaceDescription -match 'sing-tun' }
if (-not $tun) {
    Add "TUN-адаптер НЕ НАЙДЕН — режим TUN не запустился."
    Add "Причина обычно видна в журнале приложения (вкладка «Журнал»)."
} else {
    foreach ($t in $tun) {
        Add ("Имя      : " + $t.Name + "   статус: " + $t.Status)
        Add ("Описание : " + $t.InterfaceDescription)
        $ip = (Get-NetIPAddress -InterfaceIndex $t.ifIndex -AddressFamily IPv4).IPAddress -join ', '
        Add ("Адрес    : " + $(if ($ip) { $ip } else { 'НЕТ АДРЕСА — это и есть поломка' }))
        $mtu = (Get-NetIPInterface -InterfaceIndex $t.ifIndex -AddressFamily IPv4).NlMtu
        Add ("MTU      : " + $mtu)
        $rt = @(Get-NetRoute -InterfaceIndex $t.ifIndex -AddressFamily IPv4)
        Add ("Маршрутов: " + $rt.Count + $(if ($rt.Count -lt 3) { '  — СЛИШКОМ МАЛО, трафик мимо туннеля' } else { '' }))
    }
}

# --- 3. Кто выигрывает маршрутизацию --------------------------------------
# Если физический адаптер имеет более выгодную метрику, Windows отправит трафик
# мимо туннеля, и это будет выглядеть как «VPN включён, но не работает».
Head "3. Маршрут по умолчанию — кто побеждает"
Get-NetRoute -DestinationPrefix '0.0.0.0/0', '0.0.0.0/1', '128.0.0.0/1' -AddressFamily IPv4 |
    Sort-Object RouteMetric | ForEach-Object {
        $a = Get-NetAdapter -InterfaceIndex $_.InterfaceIndex
        Add ("  {0,-14} через {1,-24} метрика {2}" -f $_.DestinationPrefix, $a.Name, $_.RouteMetric)
    }

# --- 4. IPv6: главный подозреваемый ---------------------------------------
# Туннель поднимается только с IPv4. Если у машины есть рабочий IPv6 И приложение
# смогло получить AAAA-запись, соединение уйдёт по IPv6 мимо туннеля — напрямую к
# провайдеру, под блокировки. В режиме системного прокси этого не происходит,
# потому что там приложение отдаёт запрос прокси-серверу, а не открывает сокет само.
Head "4. IPv6 — может утекать мимо туннеля"
$v6 = Get-NetIPAddress -AddressFamily IPv6 | Where-Object {
    $_.IPAddress -notmatch '^fe80|^::1' -and $_.PrefixOrigin -ne 'WellKnown'
}
if ($v6) {
    foreach ($a in $v6) { Add ("Глобальный IPv6: " + $a.IPAddress + " на " + (Get-NetAdapter -InterfaceIndex $a.InterfaceIndex).Name) }
    $v6route = Get-NetRoute -DestinationPrefix '::/0' | Select-Object -First 1
    Add ("Маршрут ::/0   : " + $(if ($v6route) { 'есть через ' + (Get-NetAdapter -InterfaceIndex $v6route.InterfaceIndex).Name } else { 'нет' }))
    $aaaa = (Resolve-DnsName -Name 'www.google.com' -Type AAAA -QuickTimeout | Where-Object { $_.QueryType -eq 'AAAA' }).IPAddress
    if ($aaaa) {
        Add ("AAAA приходит  : " + ($aaaa -join ', '))
        Add "!! РИСК УТЕЧКИ: приложения увидят IPv6 и пойдут мимо туннеля."
        Add "   Обычно это значит, что DNS резолвит не наше ядро (браузерный DoH)."
    } else {
        Add "AAAA не приходит — утечки по IPv6 нет (наш DNS отдаёт только IPv4)."
    }
} else {
    Add "Глобального IPv6 нет — этот путь утечки исключён."
}

# --- 5. Браузерный DoH -----------------------------------------------------
# Браузер со своим «Безопасным DNS» резолвит в обход ядра: получает AAAA, ходит
# по IPv6, и мимо туннеля. Это самая частая причина «в браузере не грузится».
Head "5. Безопасный DNS в браузере (резолвит мимо нас)"
$browsers = @(
    @('Chrome', "$env:LOCALAPPDATA\Google\Chrome\User Data\Local State"),
    @('Edge', "$env:LOCALAPPDATA\Microsoft\Edge\User Data\Local State"),
    @('Yandex', "$env:LOCALAPPDATA\Yandex\YandexBrowser\User Data\Local State"),
    @('Opera', "$env:APPDATA\Opera Software\Opera Stable\Local State")
)
foreach ($b in $browsers) {
    if (Test-Path $b[1]) {
        $mode = (Get-Content $b[1] -Raw | ConvertFrom-Json).dns_over_https.mode
        if (-not $mode) { $mode = '(по умолчанию)' }
        Add ("  {0,-8}: {1}{2}" -f $b[0], $mode, $(if ($mode -eq 'secure') { '   <-- ВЫКЛЮЧИТЬ' } else { '' }))
    }
}
foreach ($pf in Get-ChildItem "$env:APPDATA\Mozilla\Firefox\Profiles" -Directory) {
    $pj = Join-Path $pf.FullName 'prefs.js'
    if ((Test-Path $pj) -and ((Get-Content $pj -Raw) -match 'network\.trr\.mode",\s*3')) {
        Add "  Firefox : строгий DoH   <-- ВЫКЛЮЧИТЬ"
    }
}

# --- 6. Чужие перехватчики -------------------------------------------------
# Эти драйверы забирают пакеты НИЖЕ нашего туннеля, поэтому TUN перестаёт работать,
# а системный прокси продолжает — он живёт выше, на уровне приложения.
Head "6. Чужие сетевые фильтры (забирают трафик ниже TUN)"
$drv = Get-CimInstance Win32_SystemDriver | Where-Object {
    $_.State -eq 'Running' -and $_.Name -match 'divert|ndisrd|zapret|winws|klwfp|kneps|adgnetwork|Prxer|epfwwfp|nfc_driver'
}
if ($drv) { foreach ($d in $drv) { Add ("  " + $d.Name + "  (" + $d.DisplayName + ")") } }
else { Add "  не найдено" }
$other = Get-NetAdapter | Where-Object {
    $_.Status -eq 'Up' -and $_.InterfaceDescription -match 'TAP|WireGuard|Wintun|WARP|Outline' -and $_.InterfaceDescription -notmatch 'sing-tun'
}
if ($other) { foreach ($a in $other) { Add ("  посторонний адаптер включён: " + $a.Name + " [" + $a.InterfaceDescription + "]") } }

# --- 7. Решающий тест ------------------------------------------------------
# Один и тот же адрес двумя путями. Если через прокси работает, а через систему
# нет — проблема ровно в перехвате TUN, а не в сервере, подписке или блокировках.
Head "7. Решающий тест: один адрес двумя путями"
$port = 2080
$listen = Get-NetTCPConnection -LocalPort $port -State Listen
Add ("Локальный вход 127.0.0.1:$port слушает: " + [bool]$listen)
foreach ($u in @('http://cp.cloudflare.com/generate_204', 'https://www.google.com/generate_204')) {
    foreach ($mode in @('через систему (TUN)', 'через прокси напрямую')) {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        try {
            if ($mode -like '*прокси*') { $r = Invoke-WebRequest $u -UseBasicParsing -TimeoutSec 12 -Proxy "http://127.0.0.1:$port" }
            else { $r = Invoke-WebRequest $u -UseBasicParsing -TimeoutSec 12 }
            Add ("  {0,-22} {1,-45} OK {2} ({3} мс)" -f $mode, $u, $r.StatusCode, $sw.ElapsedMilliseconds)
        } catch {
            Add ("  {0,-22} {1,-45} ОШИБКА ({2} мс)" -f $mode, $u, $sw.ElapsedMilliseconds)
        }
    }
}

# --- 7b. Скорость ------------------------------------------------------------
# «Медленно» — это не диагноз, а ощущение. Цифра отделяет три разных случая:
# узкий канал у провайдера, задушенный туннель и тормозящий сервер. Меряем тем
# же путём, которым ходит браузер, и рядом — напрямую через локальный вход, без
# перехвата: если через прокси быстро, а через систему медленно, виноват не
# сервер, а захват трафика на этой машине.
Head "7b. Скорость (25 МБ)"
# Меряем через System.Net.WebClient, а НЕ через Invoke-WebRequest: в PowerShell 5.1
# он рисует прогресс-бар на каждый блок и упирается в него сам. На машине, где curl
# показывал 124-152 Мбит/с, Invoke-WebRequest выдавал 14,8 — прибор мерил себя, а не
# канал, и с такими цифрами клиента отправили бы чинить исправный туннель.
$ProgressPreference = 'SilentlyContinue'
$url = "https://speed.cloudflare.com/__down?bytes=25000000"
$tmp = Join-Path $env:TEMP 'gr_speed.bin'
foreach ($mode in @("через систему (TUN)", "через прокси напрямую")) {
    $wc = New-Object System.Net.WebClient
    if ($mode -like "*прокси*") { $wc.Proxy = New-Object System.Net.WebProxy("http://127.0.0.1:$port") }
    else { $wc.Proxy = $null }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $wc.DownloadFile($url, $tmp)
        $sw.Stop()
        $mb = (Get-Item $tmp).Length / 1MB
        $mbps = [math]::Round($mb * 8 / $sw.Elapsed.TotalSeconds, 1)
        Add ("  {0,-22} {1,6} Мбит/с   ({2} с)" -f $mode, $mbps, [math]::Round($sw.Elapsed.TotalSeconds, 1))
    } catch {
        $sw.Stop()
        Add ("  {0,-22} НЕ ИЗМЕРЕНО ({1} с)" -f $mode, [math]::Round($sw.Elapsed.TotalSeconds, 1))
    } finally {
        $wc.Dispose()
        Remove-Item $tmp -ErrorAction SilentlyContinue
    }
}
Add "  Для сравнения: исправный туннель на нашем железе даёт 120-250 Мбит/с."

# --- 8. Внешний IP ---------------------------------------------------------
Head "8. Какой IP видит мир"
try {
    $tr = (Invoke-WebRequest 'https://cp.cloudflare.com/cdn-cgi/trace' -UseBasicParsing -TimeoutSec 12).Content
    ($tr -split "`n" | Where-Object { $_ -match '^(ip|loc)=' }) | ForEach-Object { Add ("  " + $_) }
    Add "  (loc= ваша страна означает, что трафик идёт МИМО VPN)"
} catch { Add "  не удалось определить" }

Head "Готово"
$file = Join-Path ([Environment]::GetFolderPath('Desktop')) 'greenrhythm-tun.txt'
$out | Out-File -FilePath $file -Encoding UTF8
Write-Host ""
Write-Host " Отчёт сохранён на рабочий стол: $file" -ForegroundColor Green
Write-Host " Отправьте этот файл в поддержку." -ForegroundColor Green
