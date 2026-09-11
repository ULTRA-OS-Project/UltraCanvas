<#
.SYNOPSIS
    Find out why a packaged UltraCanvas application does not start, or starts
    without ever showing a window.

.DESCRIPTION
    The companion to uc-diagnose.bat, and the one to reach for when the app
    produces no window and no output. It answers the question the batch file
    cannot: is the process DEAD or is it ALIVE AND STUCK? Those look identical
    from the outside and have nothing in common as bugs.

      dead    -> a crash or an early exit. The exit code names it, and the
                 Windows event log usually names the faulting module.
      alive,
      no window
              -> the process is blocked or spinning before it creates its
                 window. Nothing crashed, so there is no exit code and no event
                 log entry; a host application waiting on IPC that is never
                 serviced looks exactly like this.

    Windows PowerShell 5.1 and PowerShell 7 both work. Nothing here modifies
    the machine, and the process is left running so it can be inspected.

.PARAMETER Path
    The executable. Defaults to the single .exe beside this script, or in bin\.

.PARAMETER Arguments
    Arguments passed on to the application.

.PARAMETER WaitSeconds
    How long to watch for a window before reporting (default 20).

.PARAMETER CheckOnly
    Inspect the executable's PE header and stop; do not launch it. This is the
    check that explains Windows' own "This app can't run on your PC" dialog:
    a binary built for another CPU architecture, a truncated or empty file, or
    a subsystem version this Windows is too old for. Exit code 0 when the file
    could be started here, 1 when Windows would refuse it.

.EXAMPLE
    .\uc-diagnose.ps1
    .\uc-diagnose.ps1 .\bin\Ladybird.exe https://example.com
    .\uc-diagnose.ps1 -CheckOnly .\UltraFiler.exe
#>
[CmdletBinding()]
param(
    [string]   $Path,
    [string[]] $Arguments = @(),
    [int]      $WaitSeconds = 20,
    [switch]   $CheckOnly
)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Definition

function Write-Section($text) {
    Write-Host ''
    Write-Host "=== $text ===" -ForegroundColor Cyan
}

# NTSTATUS values a failing launch actually produces. PowerShell surfaces them
# as signed 32-bit integers, so they are matched on the unsigned hex form.
$exitCodeMeanings = @{
    '0xC0000135' = 'STATUS_DLL_NOT_FOUND - a required DLL is missing from the package.'
    '0xC0000139' = 'STATUS_ENTRYPOINT_NOT_FOUND - a DLL was found but is the wrong version; usually an older copy earlier on PATH.'
    '0xC0000142' = 'STATUS_DLL_INIT_FAILED - a DLL loaded but its initialiser failed.'
    '0xC000007B' = 'STATUS_INVALID_IMAGE_FORMAT - 32/64-bit mismatch between the EXE and a DLL.'
    '0xC0000005' = 'ACCESS_VIOLATION - the process crashed. The event log below should name the faulting module.'
    '0xC000001D' = 'ILLEGAL_INSTRUCTION - the binary uses CPU instructions this machine does not have. Rebuild without -march=native.'
    '0xC00000FD' = 'STATUS_STACK_OVERFLOW - unbounded recursion.'
    '0xC0000409' = 'STATUS_STACK_BUFFER_OVERRUN - a security check aborted the process.'
    '0xC0000022' = 'STATUS_ACCESS_DENIED - something refused to let the process run. On Windows 11 suspect Smart App Control first.'
    '0xC0000415' = 'STATUS_DLL_INIT_FAILED_LOGOFF.'
}

# --- Resolve the executable ------------------------------------------------
if (-not $Path) {
    $candidates = @(Get-ChildItem -Path $here -Filter *.exe -File -ErrorAction SilentlyContinue)
    if ($candidates.Count -eq 0) {
        $candidates = @(Get-ChildItem -Path (Join-Path $here 'bin') -Filter *.exe -File -ErrorAction SilentlyContinue)
    }
    if ($candidates.Count -eq 1) {
        $Path = $candidates[0].FullName
    } elseif ($candidates.Count -gt 1) {
        Write-Host "Several executables found; name the one to test:" -ForegroundColor Yellow
        $candidates | ForEach-Object { Write-Host "  $($_.Name)" }
        exit 1
    } else {
        Write-Host "No .exe found next to this script or in bin\. Pass one with -Path." -ForegroundColor Red
        exit 1
    }
}
$exe = (Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue)
if (-not $exe) { Write-Host "Not found: $Path" -ForegroundColor Red; exit 1 }
$exe = $exe.Path
$exeName = [IO.Path]::GetFileNameWithoutExtension($exe)

# --- Environment -----------------------------------------------------------
Write-Section 'Environment'
$os = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
$build = if ($os) { [int]($os.BuildNumber) } else { [Environment]::OSVersion.Version.Build }
# Windows 11 kept Windows 10's major.minor; build 22000 is the boundary.
$osName = if ($build -ge 22000) { 'Windows 11' } elseif ($build -ge 10240) { 'Windows 10' } else { 'Windows' }
# The machine's real architecture, not the shell's: a 32-bit or an emulated
# x64 PowerShell reports itself in PROCESSOR_ARCHITECTURE. Win32_Processor
# answers for the silicon (9 = x64, 12 = ARM64, 0 = x86); the environment is
# the fallback when WMI is unavailable.
$cpu = Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1
$hostArch = switch ([int]$(if ($cpu) { $cpu.Architecture } else { -1 })) {
    9  { 'x64' }
    12 { 'ARM64' }
    0  { 'x86' }
    default {
        $envArch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
        switch ($envArch) { 'AMD64' { 'x64' } 'ARM64' { 'ARM64' } 'x86' { 'x86' } default { $envArch } }
    }
}
Write-Host "OS          : $osName (build $build) - $($os.Caption)"
Write-Host "Architecture: $hostArch"
Write-Host "Executable  : $exe"

# --- PE header -------------------------------------------------------------
# Windows checks the header before the process exists, and when it does not
# like what it finds it shows "This app can't run on your PC" and nothing
# else: no process, no exit code, no event-log entry, no framework log. The
# three causes are all in the header, so read it here, before launching.
function Read-PeHeader([string] $file) {
    $info = [ordered]@{ Size = (Get-Item -LiteralPath $file).Length; Problems = @() }
    if ($info.Size -eq 0) { $info.Problems += 'the file is EMPTY (0 bytes) - a download or extraction that produced nothing'; return $info }
    if ($info.Size -lt 64) { $info.Problems += "the file is $($info.Size) bytes - too short to hold a PE header"; return $info }
    $stream = [IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(2)) -ne 'MZ') { $info.Problems += 'no MZ signature - this is not a Windows executable'; return $info }
        $stream.Position = 60
        $peOffset = $reader.ReadUInt32()
        if ($peOffset + 24 -gt $info.Size) { $info.Problems += "the PE header lies at byte $peOffset, beyond the end of the file - truncated"; return $info }
        $stream.Position = $peOffset
        if (($reader.ReadBytes(4) -join ',') -ne '80,69,0,0') { $info.Problems += "no PE signature at byte $peOffset"; return $info }
        $machine  = $reader.ReadUInt16()
        $sections = $reader.ReadUInt16()
        $stream.Position = $peOffset + 20
        $optSize  = $reader.ReadUInt16()
        $opt      = $peOffset + 24
        $stream.Position = $opt
        $magic    = $reader.ReadUInt16()
        $info.Machine = switch ($machine) { 0x8664 { 'x64' } 0xAA64 { 'ARM64' } 0x14C { 'x86' } 0x1C4 { 'ARM32' } default { ('0x{0:X4}' -f $machine) } }
        $info.Bits = switch ($magic) { 0x20B { 'PE32+' } 0x10B { 'PE32' } default { ('magic 0x{0:X4}' -f $magic) } }
        $info.Sections = $sections
        if ($magic -eq 0x20B -or $magic -eq 0x10B) {
            $stream.Position = $opt + 48
            $info.SubsystemVersion = '{0}.{1:00}' -f $reader.ReadUInt16(), $reader.ReadUInt16()
            $info.SubsystemMajor = [int]$info.SubsystemVersion.Split('.')[0]
            $stream.Position = $opt + 68
            $info.Subsystem = switch ($reader.ReadUInt16()) { 2 { 'GUI' } 3 { 'console' } default { "$_" } }
        }
        # A file cut short still has a header that parses. The section table
        # says how long the file has to be.
        $need = 0
        for ($i = 0; $i -lt $sections; $i++) {
            $stream.Position = $opt + $optSize + $i * 40 + 16
            $end = [long]$reader.ReadUInt32() + [long]$reader.ReadUInt32()
            if ($end -gt $need) { $need = $end }
        }
        $info.SectionsEnd = $need
    } finally {
        $reader.Close()
    }
    if ($info.SectionsEnd -gt $info.Size) {
        $info.Problems += "the file is TRUNCATED: its sections end at byte $($info.SectionsEnd) but it is only $($info.Size) bytes long. Re-download the package, or extract it again with a different tool."
    }
    if ($info.Sections -gt 96) { $info.Problems += "$($info.Sections) sections; Windows loads at most 96" }
    if ($info.SubsystemMajor -gt 10) { $info.Problems += "subsystem version $($info.SubsystemVersion) - newer than any Windows release" }
    switch ($info.Machine) {
        'ARM64' {
            if ($hostArch -ne 'ARM64') {
                $info.Problems += "this executable is built for ARM64 but this machine is $hostArch. It is the arm64 package; the one for this PC is the x86_64 package (UCDemo-Windows-<version>-x86_64.zip)."
            }
        }
        'x64' {
            if ($hostArch -eq 'x86') {
                $info.Problems += 'this executable is 64-bit but this Windows is 32-bit. Nothing 64-bit can run here.'
            } elseif ($hostArch -eq 'ARM64' -and $build -lt 22000) {
                $info.Problems += 'this executable is x64 and this is Windows 10 on ARM, which cannot emulate x64 programs (Windows 11 can). Use the arm64 package.'
            }
        }
        'x86' { }
        default { $info.Problems += "built for $($info.Machine), which this machine ($hostArch) does not run" }
    }
    return $info
}

Write-Section 'Executable header'
$pe = Read-PeHeader $exe
if ($pe.Contains('Machine')) {
    Write-Host "Built for   : $($pe.Machine) ($($pe.Bits)), $($pe.Subsystem) subsystem $($pe.SubsystemVersion), $($pe.Sections) sections"
}
Write-Host "File size   : $($pe.Size) bytes"
if ($pe.Problems.Count -gt 0) {
    Write-Host ''
    Write-Host "[X] Windows will refuse to start this file with ""This app can't run on your PC""" -ForegroundColor Red
    Write-Host "    (ERROR_BAD_EXE_FORMAT). It never becomes a process, so there is no exit" -ForegroundColor Red
    Write-Host "    code, no event-log entry and no log to read. The reason:" -ForegroundColor Red
    foreach ($p in $pe.Problems) { Write-Host "    - $p" -ForegroundColor Red }
    exit 1
}
Write-Host "The header is one this machine can load."
if ($CheckOnly) { exit 0 }

# --- Mark of the Web -------------------------------------------------------
# Files extracted from a downloaded ZIP inherit a Zone.Identifier stream.
# Windows 11 enforces it far more aggressively than Windows 10; with Smart App
# Control on, an unsigned binary carrying it can be stopped with no dialog.
$motw = Get-Item -LiteralPath $exe -Stream Zone.Identifier -ErrorAction SilentlyContinue
if ($motw) {
    Write-Host ''
    Write-Host "[!] Mark of the Web present on this executable (downloaded-file marker)." -ForegroundColor Yellow
    Write-Host "    Clear it for the whole folder and retry:" -ForegroundColor Yellow
    Write-Host "        Get-ChildItem -Recurse '$(Split-Path -Parent $exe)' | Unblock-File" -ForegroundColor Yellow
}

# --- Launch ----------------------------------------------------------------
# ULTRACANVAS_DEBUG_LOG makes the framework's debugOutput write to a file in any
# build configuration, Release included; the crash reporter appends the
# exception code and faulting module to the same file.
$log = Join-Path $here 'uc-diagnose.log'
if (Test-Path $log) { Remove-Item $log -Force }
$env:ULTRACANVAS_DEBUG_LOG = $log

Write-Section 'Launching'
Write-Host "Log file    : $log"

# Deliberately NOT Start-Process. Its -PassThru object does not reliably carry
# the child's exit code - it can report 0 for a process that exited non-zero,
# which would turn a crash into "clean exit" in the verdict below. Starting the
# process through the .NET API keeps the OS handle open, so ExitCode is the real
# one. UseShellExecute = false also lets the child inherit this console's
# handles, so anything it writes to stderr still lands here.
function ConvertTo-CommandLineArgument([string] $value) {
    # PowerShell 5.1 runs on .NET Framework, which has no ProcessStartInfo
    # .ArgumentList, so the command line is built by hand.
    if ($value -notmatch '[\s"]') { return $value }
    return '"' + ($value -replace '(\\*)"', '$1$1\"') + '"'
}

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = (($Arguments | ForEach-Object { ConvertTo-CommandLineArgument $_ }) -join ' ')
$psi.UseShellExecute = $false
# Match a double-click from Explorer, which is the case most people have
# already tried, rather than inheriting whatever directory the shell is in.
$psi.WorkingDirectory = Split-Path -Parent $exe
Write-Host "Working dir : $($psi.WorkingDirectory)"

$startedAt = Get-Date
try {
    $proc = [System.Diagnostics.Process]::Start($psi)
} catch {
    Write-Host "Could not start the process: $($_.Exception.Message)" -ForegroundColor Red
    $win32 = $_.Exception
    while ($win32 -and -not ($win32 -is [ComponentModel.Win32Exception])) { $win32 = $win32.InnerException }
    $code = if ($win32) { $win32.NativeErrorCode } else { -1 }
    switch ($code) {
        193 { Write-Host "Win32 error 193 ERROR_BAD_EXE_FORMAT - the dialog for this is ""This app can't run on your PC"". The header check above passed, so the file changed between the check and the launch, or an endpoint-protection product is interposing." }
        216 { Write-Host "Win32 error 216 ERROR_EXE_MACHINE_TYPE_MISMATCH - built for another CPU architecture; see the header check above." }
        5   { Write-Host "Win32 error 5 ACCESS_DENIED - a policy (AppLocker, Smart App Control, endpoint protection) refused to let the binary run." }
        1260 { Write-Host "Win32 error 1260 - blocked by software restriction policy / AppLocker." }
        default {
            Write-Host "A failure here, before the process exists, points at the loader or at a"
            Write-Host "policy blocking the binary rather than at the application."
        }
    }
    exit 1
}
Write-Host "PID         : $($proc.Id)"
Write-Host "Watching for up to $WaitSeconds seconds..."

# Poll rather than wait: a window appearing is as interesting as an exit, and
# PowerShell does not block on a GUI-subsystem process the way cmd does.
$sawWindow = $false
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 400
    $proc.Refresh()
    if ($proc.HasExited) { break }
    if ($proc.MainWindowHandle -ne 0) { $sawWindow = $true; break }
}

# --- Verdict ---------------------------------------------------------------
Write-Section 'Verdict'
$proc.Refresh()
if ($proc.HasExited) {
    $code = $proc.ExitCode
    $hex = '0x{0:X8}' -f $code
    Write-Host "The process EXITED after $([int]((Get-Date) - $startedAt).TotalSeconds)s." -ForegroundColor Yellow
    Write-Host "Exit code   : $code  ($hex)"
    if ($exitCodeMeanings.ContainsKey($hex)) {
        Write-Host "Meaning     : $($exitCodeMeanings[$hex])"
    } elseif ($code -eq 0) {
        Write-Host "Meaning     : clean exit - it shut down on purpose rather than crashing."
    } else {
        Write-Host "Meaning     : not a code this script knows. A negative value is an NTSTATUS; look up the hex."
    }
} elseif ($sawWindow) {
    Write-Host "A window appeared. The application started normally." -ForegroundColor Green
    Write-Host "PID $($proc.Id) is still running."
} else {
    Write-Host "The process is ALIVE after ${WaitSeconds}s but has NO window." -ForegroundColor Yellow
    Write-Host ''
    Write-Host "Nothing crashed, so there is no exit code and there will be no event log"
    Write-Host "entry. The process is blocked or spinning before it creates its window."
    Write-Host "For a host application built on UltraCanvas, the usual cause is an event"
    Write-Host "loop that never services what the application is waiting on - IPC sockets"
    Write-Host "registered through AddFdWatch() are the common case. See the"
    Write-Host "'Alive but no window' section of"
    Write-Host "Docs/UltraCanvas/UltraCanvasWindowsDiagnostics.md."
    Write-Host ''
    Write-Host "CPU time used so far: $($proc.TotalProcessorTime)"
    Write-Host "  near zero and not growing -> blocked on a wait that never completes"
    Write-Host "  climbing steadily         -> spinning in the event loop"
    Write-Host ''
    Write-Host "PID $($proc.Id) has been left running. Stop it with: Stop-Process -Id $($proc.Id)"
}

# --- Child processes -------------------------------------------------------
# A multi-process application (a browser and its content processes) may be
# waiting on a helper that failed to start, or that started and was never
# talked to.
$children = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($proc.Id)" -ErrorAction SilentlyContinue
if ($children) {
    Write-Section 'Child processes'
    $children | ForEach-Object { Write-Host "  $($_.ProcessId)  $($_.Name)" }
}

# --- Event log -------------------------------------------------------------
Write-Section 'Windows event log'
$since = $startedAt.AddMinutes(-1)
$events = @()
foreach ($logName in @('Application', 'System')) {
    $events += Get-WinEvent -FilterHashtable @{LogName = $logName; StartTime = $since} -ErrorAction SilentlyContinue |
               Where-Object { $_.Message -and $_.Message -match [regex]::Escape($exeName) }
}
if ($events) {
    $events | Sort-Object TimeCreated | ForEach-Object {
        Write-Host ''
        Write-Host "[$($_.TimeCreated)] $($_.LogName)/$($_.ProviderName) id=$($_.Id) $($_.LevelDisplayName)" -ForegroundColor Yellow
        Write-Host ($_.Message -split "`n" | Select-Object -First 12 | Out-String).TrimEnd()
    }
} else {
    Write-Host "No entry mentioning '$exeName' since the launch."
    Write-Host "For a crash there would normally be one; its absence is itself evidence"
    Write-Host "that the process did not fault."
}

# --- Framework log ---------------------------------------------------------
Write-Section 'Framework log'
if (Test-Path $log) {
    $size = (Get-Item $log).Length
    if ($size -eq 0) {
        Write-Host "$log exists but is EMPTY (0 bytes)." -ForegroundColor Yellow
        Write-Host ''
        Write-Host "Either the application is not built against a UltraCanvas new enough to"
        Write-Host "honour ULTRACANVAS_DEBUG_LOG (0.3.80 and later), or it was created by the"
        Write-Host "application's own logging and the process never reached a flush. An empty"
        Write-Host "log file whose creation succeeded still rules out a loader failure: the"
        Write-Host "process ran far enough to open a file."
    } else {
        Get-Content $log
    }
} else {
    Write-Host "No log was written at all. If the process also exited immediately, it died"
    Write-Host "before any application code ran - a loader problem, not an application one."
}

Write-Host ''
