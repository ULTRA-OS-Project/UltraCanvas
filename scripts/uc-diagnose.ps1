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

.EXAMPLE
    .\uc-diagnose.ps1
    .\uc-diagnose.ps1 .\bin\Ladybird.exe https://example.com
#>
[CmdletBinding()]
param(
    [string]   $Path,
    [string[]] $Arguments = @(),
    [int]      $WaitSeconds = 20
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
Write-Host "OS          : $osName (build $build) - $($os.Caption)"
Write-Host "Architecture: $env:PROCESSOR_ARCHITECTURE"
Write-Host "Executable  : $exe"

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
    Write-Host "A failure here, before the process exists, points at the loader or at a"
    Write-Host "policy blocking the binary rather than at the application."
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
