- **Windows: "Delete as administrator" in UltraFiler put up "The procedure
  entry point `WNetGetConnectionW` could not be located in
  `C:\WINDOWS\SYSTEM32\pcacli.dll`" after the consent prompt - and the box
  0.9.83 saw in `daxexec.dll` was the same bug.** The package shipped
  ImageMagick's coder module `coders\mpr.dll` (the `MPR:` in-memory image
  registry). ImageMagick loads every coder into the process the first time
  libvips asks it whether it recognises a file, and Windows keys the modules
  of a process by base name: from then on any system DLL that imports
  `MPR.dll` by name - `pcacli.dll`, which the shell loads for the `runas`
  verb, `daxexec.dll`, which activates a Store app - was bound to the coder
  instead of to the real one, and its import failed.
  - `package-win.sh` lets no coder into the package under a Windows system
    DLL's name. `mpr.dll` and `url.dll` (fetch over HTTP), pseudo-formats of
    no use here, are dropped with their `.la` files. A real format whose
    name collides - `dpx.dll` (SMPTE DPX, which the export dialog offers)
    and `vid.dll` on Windows 10 and 11, plus anything the packaging
    machine's `System32` turns up - ships as `<name>-coder.dll`, with its
    `.la` pointing at the new file; ImageMagick opens coders through the
    `.la`, so nothing changes for it. The build then refuses any DLL of a
    system DLL's name anywhere in the package, with the file named.
  - `uc-diagnose.ps1` lists the DLLs of an installed package that carry a
    system DLL's name, so an older extraction can be fixed by deleting them.
  - The elevated-delete backend turns the loader's hard-error boxes off on
    its worker thread around the launch, as the file-associations backend
    already did: a system DLL that still fails to load comes back as
    `ShellExecuteEx`'s error in the "Cannot Delete" dialog, not as a modal box
    behind the progress window.
  - Anyone on a 0.9.92 or older package: delete
    `lib\ImageMagick-*\modules-Q16HDRI\coders\mpr.dll` and `mpr.la` (and
    `url.dll`, `url.la`) from it, or extract the next package into a fresh
    folder.
