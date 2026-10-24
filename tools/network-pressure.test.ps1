$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../support/NetworkPressure.psm1') -Force
function Assert($Condition, $Message) { if (-not $Condition) { throw $Message } }
$rows = @(
    [pscustomobject]@{State='Established';RemoteAddress='1.1.1.1';RemotePort=443;OwningProcess=7},
    [pscustomobject]@{State='Bound';RemoteAddress='0.0.0.0';RemotePort=0;OwningProcess=7},
    [pscustomobject]@{State='TimeWait';RemoteAddress='172.19.0.2';RemotePort=853;OwningProcess=0},
    [pscustomobject]@{State='TimeWait';RemoteAddress='172.19.0.2';RemotePort=853;OwningProcess=0},
    [pscustomobject]@{State='FinWait2';RemoteAddress='172.19.0.2';RemotePort=853;OwningProcess=77},
    [pscustomobject]@{State='TimeWait';RemoteAddress='fdfe:dcba:9876::2';RemotePort=853;OwningProcess=0},
    [pscustomobject]@{State='TimeWait';RemoteAddress='1.1.1.1';RemotePort=853;OwningProcess=0}
)
$report = ConvertTo-GreenRhythmTcpPressure -Connections $rows -TunDnsAddresses '172.19.0.2','fdfe:dcba:9876::2'
Assert ($report.Total -eq 7) 'Lost TCP states outside Established'
Assert (($report.States | Where-Object State -eq TimeWait).Count -eq 4) 'Incorrect TIME_WAIT count'
Assert (($report.States | Where-Object State -eq Bound).Count -eq 1) 'Bound endpoints lost'
Assert ($report.TimeWaitDestinations[0].Count -eq 2) 'Highest-churn destination missing'
Assert ($report.TunDnsConnections.Count -eq 3) 'Mixed up IPv4, IPv6 or external DNS'
Assert (@($report.TunDnsConnections | Where-Object { $_.Pid -eq 0 -and $_.OwnerKnown }).Count -eq 0) 'PID 0 was attributed to a process'
Assert (@($report.TunDnsConnections | Where-Object { $_.Pid -eq 77 -and $_.OwnerKnown }).Count -eq 1) 'Live owning process lost'
$empty = ConvertTo-GreenRhythmTcpPressure -Connections @()
Assert ($empty.Total -eq 0 -and $empty.States.Count -eq 0) 'Empty table was not represented faithfully'
$roundTrip = $report | ConvertTo-Json -Depth 6 | ConvertFrom-Json
Assert ($roundTrip.TunDnsConnections.Count -eq 3) 'Machine-readable report lost entries'
Write-Host 'Network pressure fixtures passed (no network or registry changes)'
