@echo off
setlocal enabledelayedexpansion
rem ===========================================================================
rem uc-diagnose.bat - find out why a packaged UltraCanvas app does not start.
rem
rem Usage:  uc-diagnose.bat [path\to\App.exe] [app arguments...]
rem
rem With no argument it picks the only .exe it finds next to itself, or in a
rem bin\ subdirectory.
rem
rem The normal launchers hide every failure, and not by accident:
rem
rem   * the apps are linked as GUI-subsystem executables, so the process has no
rem     console and anything it writes to stderr is discarded;
rem   * launchers use `start "" App.exe`, which detaches the process - the shell
rem     prompt returns immediately whether the app came up or died on the spot,
rem     and the exit code is lost.
rem
rem A dead app therefore looks exactly like a healthy one from the command line:
rem the prompt comes back and nothing happens. This script removes both problems.
rem It runs the executable *attached* so the exit code survives, turns on the
rem framework log, and prints the OS build - the first thing anyone needs for a
rem "works on Windows 10, not on Windows 11" report.
rem ===========================================================================

set "HERE=%~dp0"

rem `shift` does not renumber %*, so the executable is taken off the front and
rem the app's own arguments are collected by hand into APPARGS.
set "TARGET="
set "APPARGS="
if not "%~1"=="" (
    set "TARGET=%~1"
    shift
)
:collect_args
if "%~1"=="" goto args_collected
set "APPARGS=%APPARGS% "%~1""
shift
goto collect_args
:args_collected

if "%TARGET%"=="" (
    for %%F in ("%HERE%*.exe") do (
        if "!TARGET!"=="" set "TARGET=%%~fF"
    )
)
if "%TARGET%"=="" (
    for %%F in ("%HERE%bin\*.exe") do (
        if "!TARGET!"=="" set "TARGET=%%~fF"
    )
)
if "%TARGET%"=="" (
    echo ERROR: no .exe found next to this script or in bin\.
    echo Usage: %~nx0 path\to\App.exe [arguments...]
    exit /b 1
)
if not exist "%TARGET%" (
    echo ERROR: "%TARGET%" does not exist.
    exit /b 1
)

set "LOG=%HERE%uc-diagnose.log"
if exist "%LOG%" del "%LOG%"

echo ===========================================================
echo UltraCanvas startup diagnosis
echo ===========================================================
echo Executable : %TARGET%
echo Log file   : %LOG%
for /f "tokens=2 delims=[]" %%V in ('ver') do echo Windows    : %%V
echo Processor  : %PROCESSOR_ARCHITECTURE%
echo.

rem --- Mark of the Web -----------------------------------------------------
rem Files extracted from a downloaded ZIP inherit a Zone.Identifier stream.
rem Windows 11 enforces it far more aggressively than Windows 10 does: with
rem Smart App Control on, an unsigned binary carrying this stream can be
rem terminated at launch with no dialog at all, which is exactly the reported
rem symptom. `dir /r` lists alternate data streams.
dir /r "%TARGET%" 2>nul | find /i "Zone.Identifier" >nul
if not errorlevel 1 (
    echo [!] This executable carries a Mark of the Web ^(downloaded-file marker^).
    echo     On Windows 11 that can make SmartScreen or Smart App Control stop it
    echo     silently. Clear it for the whole folder from PowerShell with:
    echo         Get-ChildItem -Recurse "%HERE%" ^| Unblock-File
    echo.
)

rem --- Run it --------------------------------------------------------------
rem ULTRACANVAS_DEBUG_LOG makes the framework's debugOutput write to a file in
rem any build configuration, including Release; the crash reporter appends the
rem exception code and faulting module to the same file.
set "ULTRACANVAS_DEBUG_LOG=%LOG%"

echo Starting (this window waits for the app to exit)...
echo.
"%TARGET%"%APPARGS%
set "CODE=%ERRORLEVEL%"

echo.
echo ===========================================================
echo Exit code: %CODE%
call :explain %CODE%
echo ===========================================================
echo.

if exist "%LOG%" (
    echo --- %LOG% ---
    type "%LOG%"
) else (
    echo No log was written. The process died before UltraCanvas ran - which
    echo points at the loader, not at the application: a missing or mismatched
    echo DLL, a blocked binary, or a CPU-instruction mismatch. Check
    echo Event Viewer ^> Windows Logs ^> Application for an entry naming this
    echo executable, and see Docs/UltraCanvas/UltraCanvasWindowsDiagnostics.md.
)

exit /b %CODE%

rem --- Exit-code decoder ---------------------------------------------------
rem An NTSTATUS such as 0xC0000135 reaches cmd as a negative 32-bit integer, so
rem the comparisons below are against the signed decimal form.
:explain
if "%~1"=="0" (
    echo Meaning  : clean exit. The app started and shut down normally.
    goto :eof
)
if "%~1"=="-1073741515" (
    echo Meaning  : 0xC0000135 STATUS_DLL_NOT_FOUND - a required DLL is missing.
    echo            The package is meant to be self-contained; a DLL was left out
    echo            or the app was started from a different folder.
    goto :eof
)
if "%~1"=="-1073741511" (
    echo Meaning  : 0xC0000139 STATUS_ENTRYPOINT_NOT_FOUND - a DLL was found but
    echo            is the wrong version. Usually an older copy of the same DLL
    echo            earlier on PATH is winning over the one in this folder.
    goto :eof
)
if "%~1"=="-1073741502" (
    echo Meaning  : 0xC0000142 STATUS_DLL_INIT_FAILED - a DLL loaded but its
    echo            initialiser failed.
    goto :eof
)
if "%~1"=="-1073741701" (
    echo Meaning  : 0xC000007B STATUS_INVALID_IMAGE_FORMAT - 32/64-bit mismatch
    echo            between the executable and one of its DLLs.
    goto :eof
)
if "%~1"=="-1073741819" (
    echo Meaning  : 0xC0000005 ACCESS_VIOLATION - the app crashed. The log above
    echo            should name the faulting module.
    goto :eof
)
if "%~1"=="-1073741795" (
    echo Meaning  : 0xC000001D ILLEGAL_INSTRUCTION - the binary uses CPU
    echo            instructions this machine does not have. Rebuild without
    echo            -march=native / -mavx512* on the build machine.
    goto :eof
)
if "%~1"=="-1073741571" (
    echo Meaning  : 0xC00000FD STATUS_STACK_OVERFLOW - unbounded recursion.
    goto :eof
)
if "%~1"=="-1073740791" (
    echo Meaning  : 0xC0000409 STATUS_STACK_BUFFER_OVERRUN - a security check
    echo            aborted the process.
    goto :eof
)
if "%~1"=="-1073741790" (
    echo Meaning  : 0xC0000022 STATUS_ACCESS_DENIED - something refused to let
    echo            the process run or read a file it needs. On Windows 11 check
    echo            Smart App Control ^(Windows Security ^> App ^& browser control^)
    echo            and any endpoint-protection product.
    goto :eof
)
echo Meaning  : not a code this script knows. If it is negative it is an
echo            NTSTATUS: convert it to hex and look it up.
goto :eof
