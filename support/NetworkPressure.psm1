# Read-only counters. No packet capture, profile reads, network probes or repair.
function ConvertTo-GreenRhythmTcpPressure {
    param([object[]]$Connections = @(), [string[]]$TunDnsAddresses = @())
    $rows = @($Connections | Where-Object { $null -ne $_ })
    $timeWait = @($rows | Where-Object { [string]$_.State -eq 'TimeWait' })
    $destinations = @($timeWait | Group-Object RemoteAddress, RemotePort |
        Sort-Object Count -Descending | Select-Object -First 10 | ForEach-Object {
            [pscustomobject]@{ Address = $_.Group[0].RemoteAddress; Port = $_.Group[0].RemotePort; Count = $_.Count }
        })
    $tun = @($rows | Where-Object { $TunDnsAddresses -contains $_.RemoteAddress } |
        Group-Object RemoteAddress, RemotePort, State, OwningProcess | ForEach-Object {
            $row = $_.Group[0]
            [pscustomobject]@{ Address = $row.RemoteAddress; Port = $row.RemotePort; State = [string]$row.State;
                Pid = $row.OwningProcess; Count = $_.Count; OwnerKnown = ($row.OwningProcess -gt 0) }
        })
    [pscustomobject]@{
        Total = $rows.Count
        States = @($rows | Group-Object State | Sort-Object Name | ForEach-Object {
            [pscustomobject]@{ State = $_.Name; Count = $_.Count }
        })
        TimeWaitDestinations = $destinations
        TunDnsConnections = $tun
        Interpretation = 'TIME_WAIT PID 0 does not identify the initiating process. The synthetic DNS address is also a System TCP NAT peer: connections to its ephemeral ports alone do not prove a DNS loop. Active counts alone do not establish absence of a leak or a cause of a watchdog.'
    }
}

function Get-GreenRhythmNetworkPressure {
    $failures = New-Object 'System.Collections.Generic.List[string]'
    $dns = @()
    try {
        foreach ($adapter in @(Get-NetAdapter -IncludeHidden -ErrorAction Stop |
                Where-Object { $_.InterfaceDescription -match 'sing-tun' -or $_.Name -like 'neko-tun*' })) {
            $dns += @(Get-DnsClientServerAddress -InterfaceIndex $adapter.ifIndex -ErrorAction Stop |
                ForEach-Object { $_.ServerAddresses })
        }
    } catch { $failures.Add('TUN DNS inventory unavailable: ' + $_.Exception.Message) }
    $tcp = $null
    try {
        $tcp = ConvertTo-GreenRhythmTcpPressure -Connections @(Get-NetTCPConnection -ErrorAction Stop) -TunDnsAddresses $dns
    } catch { $failures.Add('TCP table unavailable: ' + $_.Exception.Message) }
    $events = @()
    try {
        # No matching event is a normal result. Access errors remain visible.
        $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Tcpip';
                Id = 4227,4231,4266; StartTime = (Get-Date).AddDays(-7) } -MaxEvents 20 -ErrorAction Stop |
            ForEach-Object { [pscustomobject]@{ Id = $_.Id; TimeUtc = $_.TimeCreated.ToUniversalTime().ToString('o') } })
    } catch { if ($_.FullyQualifiedErrorId -notlike 'NoMatchingEventsFound*') { $failures.Add('Tcpip history unavailable: ' + $_.Exception.Message) } }
    $dumpMode = $null
    try { $dumpMode = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl' -Name CrashDumpEnabled -ErrorAction Stop).CrashDumpEnabled }
    catch { $failures.Add('Crash dump setting unavailable: ' + $_.Exception.Message) }
    $processes = @(Get-Process greenrhythm,greenrhythm_core -ErrorAction SilentlyContinue | ForEach-Object {
        [pscustomobject]@{ Pid = $_.Id; Name = $_.ProcessName; Handles = $_.HandleCount;
            Threads = $_.Threads.Count; PrivateBytes = $_.PrivateMemorySize64; WorkingSetBytes = $_.WorkingSet64 }
    })
    [pscustomobject]@{
        CapturedUtc = (Get-Date).ToUniversalTime().ToString('o')
        Tcp = $tcp
        RecentTcpipEvents = $events
        CrashDumpEnabled = $dumpMode
        Processes = $processes
        Errors = @($failures)
        Scope = 'Read-only snapshot; a past connection burst or kernel lock owner cannot be recovered from this snapshot.'
    }
}

Export-ModuleMember -Function ConvertTo-GreenRhythmTcpPressure, Get-GreenRhythmNetworkPressure
