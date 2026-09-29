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
  - `package-win.sh` no longer ships a coder whose name Windows also uses
    (`mpr.dll`, `url.dll`, and anything else in `System32`; the `.la` beside
    it stays behind with it), and refuses to build a package that has a DLL
    of such a name anywhere in it, with the file named.
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
