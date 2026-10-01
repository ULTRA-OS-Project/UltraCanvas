- **Windows: an installed package whose ImageMagick coder carries a system
  DLL's name is repaired on start.** Packages up to 0.9.92 shipped
  `coders\mpr.dll`, and a newer package extracted over an older folder keeps
  it, so the "procedure entry point `WNetGetConnectionW` could not be
  located" box on "Delete as administrator" and on a double-click into
  Photos came back on exactly the machines that had hit it. The image
  subsystem now puts the package's coder folder right before anything can
  load a coder (`UltraCanvasCoderModuleRepair`, new): the useless `mpr` and
  `url` pseudo-formats are deleted with their `.la` files, and a real format
  whose name Windows also uses (`dpx`, `vid`, whatever else `System32`
  holds) is renamed to `<name>-coder.dll` with its `.la` pointed at the new
  file, which ImageMagick opens through unchanged. Every change is written
  to the framework log; a folder that cannot be written (a read-only
  install) is reported there and left for `uc-diagnose.ps1` to list.
  Deleting the files by hand is no longer needed.
