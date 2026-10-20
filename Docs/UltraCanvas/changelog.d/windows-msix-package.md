- **The Windows applications now also come as an MSIX package, the format the
  Microsoft Store recommends.** `package-win-msix.sh` turns the `dist/` folder
  that `package-win.sh` builds into one package for the whole suite.
  `scripts/make_msix_layout.py` writes the manifest. It gives each application
  its own Start menu entry, under the same rule as the installer's Start menu:
  a desktop entry (`Apps/*/*.desktop`) makes one, and so does a GUI program
  without one. It gives Texter and UltraViewer file associations for
  Explorer's *Open with*. It also renders the Start menu and taskbar logos
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
  `Docs/UltraCanvas/UltraCanvasWindowsMSIX.md` compares MSIX with submitting
  the NSIS installer: MSIX needs no certificate, while the installer route
  needs every `.exe` and `.dll` signed with a CA-issued one. It covers the
  modes, the Store submission, and what behaves differently inside a
  package. UltraDesktop gets no Start menu entry in the package: it takes
  over the desktop through `HKEY_CURRENT_USER` registry values, and a
  package keeps those writes to itself.
