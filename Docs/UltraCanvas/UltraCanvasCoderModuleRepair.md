# UltraCanvasCoderModuleRepair

**Header:** `UltraCanvas/include/UltraCanvasCoderModuleRepair.h`
**Sources:** `core/UltraCanvasCoderModuleRepair.cpp` (platform-free half and
the no-op for every platform but Windows),
`OS/MSWindows/UltraCanvasWindowsCoderModuleRepair.cpp` (System32 lookup)
**Test:** `Tests/CoderModuleRepairTest.cpp`

Repairs an installed Windows package in which an ImageMagick coder module
carries the file name of a Windows system DLL. The image subsystem calls it
on Windows before anything can load a coder; no application code needs to.

## Why it exists

Windows keys the modules of a process by base name. ImageMagick loads every
coder into the process the first time libvips asks it whether it recognises
a file, and with `coders\mpr.dll` (the `MPR:` in-memory image registry) among
them, every later import of `MPR.dll` by a Windows component the shell loads
in here - `pcacli.dll` for the `runas` verb behind "Delete as administrator",
`daxexec.dll` for a Store app - was bound to the coder and failed with
*The procedure entry point `WNetGetConnectionW` could not be located*.
`package-win.sh` ships no such file any more, but a package extracted over
an older folder keeps the old coders, and every extraction of 0.9.92 or older
has them. See [*"Entry point not found" in a Windows
DLL*](UltraCanvasWindowsDiagnostics.md#entry-point-not-found-in-a-windows-dll).

## What it does

For every `*.dll` in `<exe>\lib\ImageMagick-*\modules-Q16HDRI\coders` whose
name is one Windows itself uses (`mpr`, `url`, `dpx`, `vid` always, plus
whatever the machine's `System32` holds):

- a pseudo-format of no use here (`mpr`, `url`) is deleted, with its `.la`;
- a real format (`dpx`: SMPTE DPX, which the export dialog offers) is renamed
  to `<name>-coder.dll` and its `.la` rewritten so `dlname=` and
  `library_names=` name the new file. ImageMagick opens a coder through its
  `.la`, so the rename is invisible to it, and the process then holds a
  module no system DLL is called;
- a renamable coder with no `.la` beside it is deleted, since nothing could
  point ImageMagick at the new name.

The repair is idempotent, a missing folder is nothing to do, and a package
re-extracted over a repaired one (the original back beside the renamed file)
is repaired again. Every change and every failure is written to the
framework log (`ULTRACANVAS_DEBUG_LOG`); a folder the process cannot write
leaves the files in place, where `uc-diagnose.ps1 -CheckOnly` lists them.

## API

```cpp
namespace UltraCanvas::CoderModuleRepair {
    struct CoderModuleRepairResult {
        std::vector<std::string> removed;   // "mpr.dll", "mpr.la"
        std::vector<std::string> renamed;   // "dpx.dll -> dpx-coder.dll"
        std::vector<std::string> failed;    // "mpr.dll: <why>"
        bool Changed() const;
    };
    std::string LowerCaseName(const std::string& name);
    bool IsKnownSystemDllName(const std::string& lowerName);   // mpr, url, dpx, vid
    bool IsDroppableCoder(const std::string& lowerName);        // mpr, url
    std::string RenamedCoderFileName(const std::string& dllName);
    std::string RewriteLibtoolArchive(const std::string& laText, const std::string& dlName);
    CoderModuleRepairResult RepairCoderModules(const std::string& codersDirUtf8,
            const std::function<bool(const std::string& lowerName)>& isSystemDllName);
    CoderModuleRepairResult RepairPackagedCoderModules(const std::string& exeDirUtf8);
    bool NativeIsSystemDllName(const std::string& lowerName);   // Windows backend
}
```

`RepairPackagedCoderModules` is the entry point `UCImageRaster::
InitializeImageSubsysterm` uses on Windows; on every other platform it
returns an empty result. `RepairCoderModules` with an explicit predicate is
what the test drives on any platform.

## Version

- 1.0.0 (2026-10-01): first version.
