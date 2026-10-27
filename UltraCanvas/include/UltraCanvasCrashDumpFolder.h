// include/UltraCanvasCrashDumpFolder.h
// The folder the Windows crash reporter (InstallWindowsCrashReporter,
// OS/MSWindows/UltraCanvasWindowsDiagnostics.h) writes its minidumps to:
//
//   Windows  ULTRACANVAS_CRASH_DUMP_DIR when set,
//            else %LOCALAPPDATA%\UltraCanvas\CrashDumps
//   others   none - no dump is written there; a crash leaves the system's own
//            report (a core file, a DiagnosticReports entry)
//
// %LOCALAPPDATA% is the per-user, machine-local application data folder:
// writable without rights and never inside the installation, which may be
// read-only or a download about to be deleted.
//
// Until 2026-10 the rule lived inside the crash reporter, so nothing else
// could point the user at the dumps; UltraFiler's Extras > Open location >
// Crash dumps reads it here, and so would a "report a problem" page. The
// reporter itself reads it here too, so the two cannot disagree.
//
// Header-only, like UltraCanvasDataFolder.h; the pure CrashDumpFolderFor
// makes the rule testable on any platform. All strings are UTF-8.
//
// doc-check: example
//   // Where this machine's crash dumps go; empty off Windows.
//   const std::filesystem::path dumps = UltraCanvas::UltraCanvasCrashDumpFolder();
//   // The rule for another platform and environment, testable anywhere.
//   const std::filesystem::path onWindows = UltraCanvas::CrashDumpFolderFor(
//       UltraCanvas::DataFolderPlatform::Windows, "", "C:/Users/erika/AppData/Local");
//
// Version: 1.0.0 - the crash dump folder, moved out of the crash reporter
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasDataFolder.h"   // DataFolderPlatform / CurrentDataFolderPlatform
#include "UltraCanvasPathUtf8.h"     // PathFromUtf8 / GetEnvUtf8

#include <filesystem>
#include <string>

namespace UltraCanvas {

    // The crash dump folder for the given platform and environment values
    // (each may be empty): `customDir` (ULTRACANVAS_CRASH_DUMP_DIR) when set,
    // else <localAppData>\UltraCanvas\CrashDumps - on Windows only. Empty on
    // the other platforms, and on Windows when neither is set. Pure; the
    // folder need not exist.
    inline std::filesystem::path CrashDumpFolderFor(DataFolderPlatform platform,
                                                    const std::string& customDir,
                                                    const std::string& localAppData) {
        if (platform != DataFolderPlatform::Windows) return {};
        if (!customDir.empty()) return PathFromUtf8(customDir);
        if (localAppData.empty()) return {};
        return PathFromUtf8(localAppData) / "UltraCanvas" / "CrashDumps";
    }

    // CrashDumpFolderFor on this platform, both variables read as UTF-8
    // (GetEnvUtf8). The crash reporter creates the folder when it is
    // installed; until a process has run with it, the folder may be missing.
    inline std::filesystem::path UltraCanvasCrashDumpFolder() {
        return CrashDumpFolderFor(CurrentDataFolderPlatform(),
                                  GetEnvUtf8("ULTRACANVAS_CRASH_DUMP_DIR"),
                                  GetEnvUtf8("LOCALAPPDATA"));
    }

}  // namespace UltraCanvas
