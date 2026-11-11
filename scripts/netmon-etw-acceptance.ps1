<#
.SYNOPSIS
    The first elevated run of NetworkMonitor's Windows ETW sources, as a test
    with a verdict.

.DESCRIPTION
    UltraNetMonitor has two sources that only an administrator can start: the
    kernel's network events (Microsoft-Windows-Kernel-Network - every TCP
    connect, accept and disconnect with the PID, and the bytes each way on the
    close) and the DNS client's events (Microsoft-Windows-DNS-Client - every
    query through the system resolver, with the process that asked). Both are
    compiled on CI and parsed from fixture bytes in the tests; this script is
    the run against the real providers.

    It records with the monitor for a few seconds (the snapshot differ and
    reverse DNS off, so everything recorded comes from the ETW sources), makes
    a TCP connection to itself over loopback, sends 100 000 bytes across it and
    closes it, resolves a name, then reads the recording back and checks:

      events   an "opened" and an "accepted" on the loopback port, attributed
               to this PowerShell process by name, and a "closed" that carries
               the bytes sent
      names    the resolved name in the DNS observations, from the DNS client
               events, asked by this PowerShell process

    Each check prints PASS or FAIL; the exit code is 0 when every check
    passed, 1 when one failed, 2 when the run could not be made (not elevated,
    no executable). The recording and the monitor's own output are left under
    the temporary folder it prints, for the bug report if a check fails.

    Nothing here changes the machine: no firewall rule, no resolver setting.
    Windows PowerShell 5.1 and PowerShell 7 both work.

.PARAMETER Path
    UltraNetMonitor.exe. Defaults to one beside this script, in bin\, or in
    build\ (where a local build puts it).

.PARAMETER Seconds
    How long to record. 20 by default; the traffic is made 4 seconds in.

.PARAMETER Name
    The name to resolve. example.com by default.

.EXAMPLE
    PS> .\scripts\netmon-etw-acceptance.ps1 -Path .\build\UltraNetMonitor.exe
#>
[CmdletBinding()]
param(
    [string]$Path,
    [int]$Seconds = 20,
    [string]$Name = 'example.com'
)

$ErrorActionPreference = 'Stop'

function Find-Monitor {
    param([string]$Given)
    if ($Given) { return (Resolve-Path $Given).Path }
    $here = Split-Path -Parent $MyInvocation.MyCommand.Path
    foreach ($candidate in @(
        (Join-Path $here 'UltraNetMonitor.exe'),
        (Join-Path $here 'bin\UltraNetMonitor.exe'),
        (Join-Path (Split-Path -Parent $here) 'build\UltraNetMonitor.exe'),
        (Join-Path (Split-Path -Parent $here) 'build\bin\UltraNetMonitor.exe'))) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }
    return $null
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'Not elevated: the ETW sources need an administrator. Run this from an elevated PowerShell.'
    exit 2
}

$monitor = Find-Monitor $Path
if (-not $monitor) {
    Write-Host 'UltraNetMonitor.exe not found: pass -Path.'
    exit 2
}
Write-Host "Monitor: $monitor"

$work = Join-Path ([IO.Path]::GetTempPath()) ("netmon-etw-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $work | Out-Null
$db  = Join-Path $work 'acceptance.db'
$out = Join-Path $work 'record.out.txt'
$err = Join-Path $work 'record.err.txt'
Write-Host "Working folder: $work"

Write-Host ''
Write-Host '== Capabilities'
& $monitor --capabilities

Write-Host ''
Write-Host "== Recording for $Seconds s (ETW sources only: --no-diff --no-rdns)"
$recorder = Start-Process -FilePath $monitor `
    -ArgumentList @('--record', "`"$db`"", '--seconds', "$Seconds", '--no-diff', '--no-rdns') `
    -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
Start-Sleep -Seconds 4

# The traffic: a listener on loopback, a client of our own, 100 000 bytes
# one way, then both ends closed. Every event of it is this process's.
$bytesToSend = 100000
$listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
Write-Host "Loopback connection on port $port, $bytesToSend bytes"
$client = New-Object System.Net.Sockets.TcpClient
$client.Connect([System.Net.IPAddress]::Loopback, $port)
$server = $listener.AcceptTcpClient()
$payload = New-Object byte[] $bytesToSend
$clientStream = $client.GetStream()
$serverStream = $server.GetStream()
$clientStream.Write($payload, 0, $payload.Length)
$clientStream.Flush()
$received = 0
$buffer = New-Object byte[] 65536
while ($received -lt $bytesToSend) {
    $n = $serverStream.Read($buffer, 0, $buffer.Length)
    if ($n -le 0) { break }
    $received += $n
}
$clientStream.Close(); $client.Close()
$serverStream.Close(); $server.Close()
$listener.Stop()
Write-Host "Received $received bytes at the listener"

# The name: through the system resolver, so the DNS client logs it.
$resolved = $null
try {
    $resolved = Resolve-DnsName -Name $Name -Type A -ErrorAction Stop | Where-Object { $_.Type -eq 'A' } | Select-Object -First 1
    Write-Host "Resolved $Name to $($resolved.IPAddress)"
} catch {
    Write-Host "Resolve-DnsName ${Name}: $($_.Exception.Message)"
}

Write-Host 'Waiting for the recorder...'
$recorder.WaitForExit()
Write-Host "Recorder exit code $($recorder.ExitCode)"
Write-Host '-- recorder output'
Get-Content $out | ForEach-Object { "   $_" }
if ((Get-Item $err).Length -gt 0) { Write-Host '-- recorder errors'; Get-Content $err | ForEach-Object { "   $_" } }

Write-Host ''
Write-Host '== Recorded events'
$events = & $monitor --events-history "$db" 2>&1
$events | ForEach-Object { "   $_" }
Write-Host ''
Write-Host '== Recorded DNS observations'
$dns = & $monitor --dns "$db" 2>&1
$dns | ForEach-Object { "   $_" }

# The verdict. The process is this PowerShell: powershell or pwsh.
$self = (Get-Process -Id $PID).ProcessName
$onPort = $events | Where-Object { $_ -match "127\.0\.0\.1:$port\b" }
$opened   = $onPort | Where-Object { $_ -match '^\S+\s+opened\s'   -and $_ -match "\b$self\b" }
$accepted = $onPort | Where-Object { $_ -match '^\S+\s+accepted\s' -and $_ -match "\b$self\b" }
$closed   = $onPort | Where-Object { $_ -match '^\S+\s+closed\s' }
$closedWithBytes = $closed | Where-Object { $_ -match '\d+(\.\d+)?\s*[KMG]?i?B\s*/\s*\d' }
$named = $dns | Where-Object { $_ -match [regex]::Escape($Name) -and $_ -match 'DNS client events' }
$namedBySelf = $named | Where-Object { $_ -match "\b$self\b" }

$failures = 0
function Verdict([bool]$ok, [string]$what) {
    if ($ok) { Write-Host "PASS  $what" } else { Write-Host "FAIL  $what"; $script:failures++ }
}
Write-Host ''
Write-Host '== Verdict'
Verdict ($null -ne $opened)          "an opened event on port $port attributed to $self (kernel network ETW)"
Verdict ($null -ne $accepted)        "an accepted event on port $port attributed to $self"
Verdict ($null -ne $closed)          "a closed event on port $port"
Verdict ($null -ne $closedWithBytes) "the closed event carries the bytes moved (sends and receives summed per connection)"
Verdict ($null -ne $named)           "$Name observed from the DNS client events"
Verdict ($null -ne $namedBySelf)     "and attributed to $self"
Write-Host ''
if ($failures -eq 0) {
    Write-Host "All checks passed. The recording is in $work."
    exit 0
}
Write-Host "$failures check(s) failed. Attach $work (the recording and the recorder's output) to the report."
exit 1
