<#
.SYNOPSIS
    Отбор посторонних TAP/TUN-адаптеров. Отдельным файлом — ради проверяемости.

.DESCRIPTION
    Этот предикат однажды выключил человеку домашний WireGuard, поэтому он живёт
    отдельно от скрипта и покрыт проверками (Test-ForeignAdapters.ps1). Дот-сорсить
    fix-network.ps1 ради одной функции нельзя: он при этом выполнится весь.

    Условия отбора и что каждое значит:
      Status = Up                     выключенный адаптер трогать незачем
      описание совпадает с токенами   TAP|TUN|WARP|WireGuard|Wintun
      описание НЕ sing-tun            наш собственный
      NOT HardwareInterface           никогда не физическая карта
      ifIndex не несёт 0.0.0.0/0      не тот, через который сейчас идёт весь трафик

    ВНИМАНИЕ: пройти этот отбор — НЕ значит "можно выключать". Домашний WireGuard
    со split-туннелем проходит его целиком, и это правильно: он действительно
    посторонний TUN. Решение принимает человек, посмотрев маршруты. Поэтому
    вызывающий обязан спросить, а не выключать сам.
#>

function Select-ForeignAdapters {
    param($Adapters, $DefaultIdx)
    @($Adapters | Where-Object {
        $_.Status -eq 'Up' -and
        $_.InterfaceDescription -match 'TAP|TUN|WARP|WireGuard|Wintun' -and
        $_.InterfaceDescription -notmatch 'sing-tun' -and
        -not $_.HardwareInterface -and
        $_.ifIndex -notin $DefaultIdx
    })
}
