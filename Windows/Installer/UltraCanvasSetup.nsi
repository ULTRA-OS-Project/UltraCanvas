; Windows/Installer/UltraCanvasSetup.nsi
; The UltraCanvas suite's Windows installer: what a user downloads,
; double-clicks and clicks Next through, instead of unpacking a zip.
;
; It installs exactly the folder package-win.sh assembled - every
; application with the DLLs it needs, the media, the docs - so the installer
; and the zip can never disagree about what ships. Built by
; scripts/build-windows-installer.sh (package-win.sh --installer), which
; generates the two lists included below from that folder.
;
;   - For this account only: %LOCALAPPDATA%\Programs\UltraCanvas, no
;     administrator rights, no UAC prompt.
;   - Start menu: a folder "UltraCanvas" with one entry per application.
;   - Apps & features: an entry with the version, the size and the
;     uninstaller.
;   - Finish page: "Use UltraDesktop as my Windows desktop" (off unless
;     ticked) runs UltraDesktop --use-as-desktop - the switch UOS-Settings
;     and the desktop's own menu turn off again.
;   - "UltraDesktop" works in Run boxes (App Paths): Task Manager's "Run new
;     task" is the way back for a desktop that stopped responding
;     (UltraDesktop --restore-desktop).
;   - Uninstall: the Windows desktop first (UltraDesktop --restore-desktop:
;     no start at sign-in, beside Explorer or instead of it, the taskbar as
;     it was - and the account's shell setting cleared here as well, should
;     UltraDesktop no longer run), then exactly the files the installer put
;     there - never anything else in the folder.
;
; Defines (all given by the build script):
;   UC_VERSION    the framework version, "0.9.258"
;   UC_ARCH       x86_64 or arm64
;   UC_DIST       the folder package-win.sh filled
;   UC_OUTFILE    the installer to write
;   UC_SHORTCUTS  the generated Start-menu list (CreateShortCut lines)
;   UC_REMOVE     the generated list of what to remove (Delete / RMDir lines)
;   UC_LICENSE    the licence text shown before installing
;   UC_ICON       optional: the installer's icon (.ico)
; Version: 0.2.0 - instead of Explorer: the shell setting cleared on uninstall,
;                  the guard waited for; App Paths for UltraDesktop
; Version: 0.1.0
; Last Modified: 2026-10-14
; Author: UltraCanvas Framework / ULTRA OS

Unicode true
ManifestDPIAware true

; !ifndef negates each name, so "|" reads "any one of them missing".
!ifndef UC_VERSION | UC_ARCH | UC_DIST | UC_OUTFILE | UC_SHORTCUTS | UC_REMOVE | UC_LICENSE
  !error "Build through scripts/build-windows-installer.sh, which defines UC_VERSION, UC_ARCH, UC_DIST, UC_OUTFILE, UC_SHORTCUTS, UC_REMOVE and UC_LICENSE"
!endif

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "StrFunc.nsh"
${UnStrStr}

!define PRODUCT "UltraCanvas"
!define PUBLISHER "ULTRA OS Development GmbH"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\UltraCanvas"
!define SETUP_KEY "Software\UltraCanvas\Setup"
!define APP_PATHS_KEY "Software\Microsoft\Windows\CurrentVersion\App Paths\UltraDesktop.exe"
!define WINLOGON_KEY "Software\Microsoft\Windows NT\CurrentVersion\Winlogon"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"

Name "${PRODUCT}"
OutFile "${UC_OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\UltraCanvas"
InstallDirRegKey HKCU "${SETUP_KEY}" "InstallDir"
SetCompressor /SOLID lzma
BrandingText "${PRODUCT} ${UC_VERSION} (${UC_ARCH})"
VIProductVersion "${UC_VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "ProductVersion" "${UC_VERSION}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "FileDescription" "${PRODUCT} Setup"
VIAddVersionKey "FileVersion" "${UC_VERSION}"
VIAddVersionKey "LegalCopyright" "${PUBLISHER}"

!ifdef UC_ICON
  !define MUI_ICON "${UC_ICON}"
  !define MUI_UNICON "${UC_ICON}"
!endif
!define MUI_ABORTWARNING

; ===== PAGES =====
!define MUI_WELCOMEPAGE_TEXT "This installs the ${PRODUCT} applications ${UC_VERSION} for your account - UltraDesktop, UltraFiler, UltraMail, UOS-Settings and the rest.$\r$\n$\r$\nNo administrator rights are needed, and nothing changes for other accounts on this computer."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${UC_LICENSE}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
; The one choice that changes more than a folder, so it is asked last, off
; unless ticked, and says how to undo it.
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_RUN_TEXT "Use UltraDesktop as my Windows desktop (switch back any time in UOS-Settings)"
!define MUI_FINISHPAGE_RUN_FUNCTION UseUltraDesktop
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ===== INSTALL =====
Section "${PRODUCT}"
  SetOutPath "$INSTDIR"
  File /r "${UC_DIST}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; The Start menu: the folder, one entry per application.
  CreateDirectory "$SMPROGRAMS\${PRODUCT}"
  !include "${UC_SHORTCUTS}"

  ; Apps & features.
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${UC_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  IfFileExists "$INSTDIR\UltraDesktop.exe" 0 +2
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\UltraDesktop.exe"
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" "$0"
  WriteRegStr HKCU "${SETUP_KEY}" "InstallDir" "$INSTDIR"

  ; "UltraDesktop" in a Run box - Task Manager's "Run new task" among them.
  IfFileExists "$INSTDIR\UltraDesktop.exe" 0 +3
    WriteRegStr HKCU "${APP_PATHS_KEY}" "" "$INSTDIR\UltraDesktop.exe"
    WriteRegStr HKCU "${APP_PATHS_KEY}" "Path" "$INSTDIR"
SectionEnd

; The finish page's choice: switched over, and the desktop started now.
Function UseUltraDesktop
  IfFileExists "$INSTDIR\UltraDesktop.exe" 0 +2
    Exec '"$INSTDIR\UltraDesktop.exe" --use-as-desktop'
FunctionEnd

; ===== UNINSTALL =====
Section "Uninstall"
  ; The Windows desktop first, whatever else fails: no start at sign-in,
  ; the taskbar as it was before the switch. A desktop the sign-in started
  ; sees the switch gone and closes within a second, so its files are free
  ; to remove when the list below gets to them.
  IfFileExists "$INSTDIR\UltraDesktop.exe" 0 +3
    ExecWait '"$INSTDIR\UltraDesktop.exe" --restore-desktop'
    Sleep 1500

  ; The same, should UltraDesktop no longer run: a shell setting naming the
  ; guard about to be deleted would leave the next sign-in without any
  ; desktop, and a start beside Explorer would point at nothing.
  ReadRegStr $0 HKCU "${WINLOGON_KEY}" "Shell"
  ${UnStrStr} $1 $0 "UltraCanvasShellGuard.exe"
  StrCmp $1 "" +2
    DeleteRegValue HKCU "${WINLOGON_KEY}" "Shell"
  DeleteRegValue HKCU "${RUN_KEY}" "UltraCanvas Desktop"

  ; As the session's shell, the guard starts Explorer once UltraDesktop has
  ; closed, and holds its own file until then: up to 20 s for it to finish.
  StrCpy $2 0
  guard_wait:
    ClearErrors
    Delete "$INSTDIR\UltraCanvasShellGuard.exe"
    IfErrors 0 guard_gone
    IntOp $2 $2 + 1
    IntCmp $2 40 guard_gone
    Sleep 500
    Goto guard_wait
  guard_gone:

  RMDir /r "$SMPROGRAMS\${PRODUCT}"

  ; What the installer put there, file by file; a folder only goes when it
  ; is empty, so nothing the user keeps in it is lost.
  !include "${UC_REMOVE}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${SETUP_KEY}"
  DeleteRegKey HKCU "${APP_PATHS_KEY}"
SectionEnd
