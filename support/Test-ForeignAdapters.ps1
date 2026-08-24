<#
.SYNOPSIS
    Проверка отбора посторонних адаптеров. Без сети и без прав администратора.

.DESCRIPTION
    Предикат из ForeignAdapters.ps1 однажды выключил человеку домашний WireGuard.
    Здесь закреплено, кто под него попадает, а кто нет. Адаптеры подставные —
    обычные объекты с теми же полями, что отдаёт Get-NetAdapter.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File support\Test-ForeignAdapters.ps1
#>

. (Join-Path $PSScriptRoot 'ForeignAdapters.ps1')

$script:checks = 0
$script:fails  = 0

function Adapter($name, $desc, $status, $hw, $idx) {
    [PSCustomObject]@{
        Name                 = $name
        InterfaceDescription = $desc
        Status               = $status
        HardwareInterface    = $hw
        ifIndex              = $idx
    }
}

function Expect($what, $ok) {
    $script:checks++
    if ($ok) { Write-Host "  ок   $what" -ForegroundColor Green }
    else     { $script:fails++; Write-Host "  ПЛОХО $what" -ForegroundColor Red }
}

Write-Host ""
Write-Host "Select-ForeignAdapters"

# Тот, у кого 0.0.0.0/0 — сейчас через него идёт весь трафик.
$defIdx = @(7)

$all = @(
    (Adapter 'Ethernet'   'Intel(R) Ethernet Connection I219-V'    'Up'       $true  7),
    (Adapter 'Ethernet 2' 'Realtek PCIe GbE Family Controller TUN' 'Up'       $true  8),
    (Adapter 'GreenRhythm' 'sing-tun Tunnel'                       'Up'       $false 21),
    (Adapter 'home'       'WireGuard Tunnel'                       'Up'       $false 22),
    (Adapter 'office'     'Wintun Userspace Tunnel'                'Up'       $false 23),
    (Adapter 'wg-full'    'WireGuard Tunnel'                       'Up'       $false 7),
    (Adapter 'outline-tap0' 'TAP-Windows Adapter V9'               'Disabled' $false 24)
)

$picked = Select-ForeignAdapters $all $defIdx
$names  = @($picked | ForEach-Object { $_.Name })

# ГЛАВНОЕ. Домашний WireGuard со split-туннелем проходит отбор — он
# действительно посторонний TUN, и прятать это неправильно. Но вызывающий
# обязан СПРОСИТЬ, а не выключить: ровно на этом сгорел прошлый вариант.
Expect "домашний WireGuard в отборе виден"        ($names -contains 'home')
Expect "офисный Wintun в отборе виден"            ($names -contains 'office')

# Чего в отборе быть не должно ни при каких условиях.
Expect "физическая карта не попадает"             (-not ($names -contains 'Ethernet'))
Expect "физическая карта с TUN в описании не попадает" (-not ($names -contains 'Ethernet 2'))
Expect "наш собственный туннель не попадает"      (-not ($names -contains 'GreenRhythm'))
Expect "несущий весь трафик не попадает"          (-not ($names -contains 'wg-full'))
Expect "уже выключенный не попадает"              (-not ($names -contains 'outline-tap0'))
Expect "и больше никого"                          ($picked.Count -eq 2)

# Пустой ввод не должен ронять.
Expect "пустой список отдаёт пустой"              ((Select-ForeignAdapters @() @()).Count -eq 0)

Write-Host ""
Write-Host "проверок: $script:checks, провалов: $script:fails"
if ($script:fails -gt 0) { exit 1 }
