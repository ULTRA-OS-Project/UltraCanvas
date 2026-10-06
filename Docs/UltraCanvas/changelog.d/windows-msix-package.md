- **The Windows applications now also come as an MSIX package, the format the
  Microsoft Store recommends.** `package-win-msix.sh` turns the `dist/` folder
  that `package-win.sh` builds into one package for the whole suite.
  `scripts/make_msix_layout.py` writes the manifest. It gives each application
  its own Start menu entry, and gives Texter and UltraViewer file associations
  for Explorer's *Open with*. It also renders the Start menu and taskbar logos
  from `media/appicon/` at every scale and taskbar size, including the
  unplated taskbar icons. The script has three modes:
  - `test`: unsigned. Windows 11 installs it with
    `Add-AppxPackage -AllowUnsigned`.
  - `store`: unsigned, with Partner Center's identity. The Store signs it, so
    no certificate is needed.
  - `signed`: signed with our own certificate, for distribution outside the
    Store.

  CI builds the test package on both Windows legs and uploads it as
  `UltraCanvas-Windows-MSIX-<version>-<arch>`. Release builds also build the
  Store package once the repository has the `MSIX_*` Actions variables.
  `Docs/UltraCanvas/UltraCanvasWindowsMSIX.md` describes the modes, the Store
  submission, and why MSIX is a better fit than the EXE/MSI route. The
  EXE/MSI route needs every DLL signed with a CA-issued certificate. The page
  also lists what behaves differently inside a package. EmailCleaner and
  UltraSocial have no Start menu entry yet: on Windows they keep their data
  under the working folder, and a packaged application starts in System32.
