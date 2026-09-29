- **Windows: double-clicking a JPG in UltraFiler put up an "entry point not
  found" box (`WNetGetConnectionW` in `daxexec.dll`) and did not open the
  picture.** When Photos or another Store app is the default, the shell loads
  its activation DLL into UltraFiler's own process. There that DLL failed to
  resolve an import: the loader showed its modal box, and `ShellExecuteEx`
  came back "access was denied".
  - The loader's hard-error boxes are now off on the launching thread around
    every default open and "Open with" launch.
  - A registered handler that still fails to start is handed to
    `explorer.exe`, which activates it from its own process, as a
    double-click in Explorer would.
