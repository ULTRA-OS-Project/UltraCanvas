@echo off
rem uc-remove-registrations.cmd - shipped in the ZIP package beside the programs.
rem
rem What the installer's uninstaller does for its notification registrations,
rem for a folder unpacked from the ZIP, which has no uninstaller: Windows keeps,
rem for each application whose notifications were shown, its name, its icon and
rem the program a click on one of them starts (HKCU\Software\Classes\
rem AppUserModelId and CLSID, listed under HKCU\Software\UltraCanvas\
rem Notifications). Run it before deleting the folder. Nothing else is touched;
rem an application registers again the next time it starts.
rem See Docs/UltraCanvas/UltraCanvasNotificationActivation.md.
rem Version: 1.0.0

setlocal
set "HERE=%~dp0"
if not exist "%HERE%UOS-Settings.exe" (
    echo UOS-Settings.exe is not in this folder: run this file from the UltraCanvas folder.
    pause
    exit /b 1
)
"%HERE%UOS-Settings.exe" --remove-notification-registrations
if errorlevel 1 (
    echo The notification registrations could not be removed.
    pause
    exit /b 1
)
echo The notification registrations of the UltraCanvas applications were removed.
echo You can now delete this folder.
pause
