# Packaging the Windows Applications as MSIX

How the Windows build becomes one MSIX package, with a Start menu entry for
each application. This page covers building the package in each of its three
modes, putting it into the Microsoft Store, and what changes for an
application once it runs from a package.

The zip and the installer that `package-win.sh` builds stay as they are. The
MSIX is built from the same `dist/` folder, so all three ship the same files.
The MSIX adds a manifest and the logos.

## Which route to the Store

The Microsoft Store takes a Win32 application in two shapes, and both are
open to this repository now that `package-win.sh --installer` builds an NSIS
installer (`Windows/Installer/UltraCanvasSetup.nsi`). That installer is per
user, needs no administrator rights, and installs silently with `/S`.

| | MSIX (this page) | EXE installer (`UltraCanvas-Setup-Windows-*.exe`) |
|---|---|---|
| What we upload | the `.msix` itself, to Partner Center | a URL to the installer on our own server, plus its silent switch (`/S`) |
| Code signing | **the Store signs it**: no certificate needed | the installer **and every `.exe` and `.dll` in it** must be signed with a certificate from a CA in Microsoft's Trusted Root Program. `dist/` holds hundreds of DLLs, including about 130 ImageMagick coder modules. The signing scripts make only a self-signed certificate today |
| Hosting | Microsoft's CDN | ours, at a versioned URL whose file must never change after submission (a GitHub release asset can be one) |
| Updates | the Store delivers them | a new versioned URL for each release |
| UltraDesktop in place of the Windows desktop | **not possible**, and UltraDesktop says so: see *What changes inside a package* | works: the installer even offers it on its finish page |
| UltraFiler's *Delete as administrator* | only with a capability the Store grants on request | works |

What decides it is the certificate. With MSIX, the suite can be in the Store
without buying one, but UltraDesktop cannot take over the desktop from the
Store version. The installer route keeps everything working, but needs a
CA-issued code-signing certificate and a signature on each of several hundred
files. The two routes can coexist: the installer from our own download page,
the MSIX in the Store.

## Building it

Run it in the MSYS2 CLANG64 (x64) or CLANGARM64 (arm64) shell, after the
normal Windows packaging:

```bash
./package-win.sh --no-sign          # builds dist/
./package-win-msix.sh               # --mode test, the default
```

The package lands in `dist-msix/` as
`UltraCanvas-Windows-<version>-<arch>[-test|-store].msix`. The version comes
from the first line of `Docs/UltraCanvas/CHANGELOG.md`, with a `.0` appended,
because MSIX takes four numbers and the Store reserves the fourth. The
architecture is the one `dist/` was built for.

It needs the Windows SDK (`makeappx.exe`, `makepri.exe`, `signtool.exe`),
Python 3 and ImageMagick. The script finds the newest SDK under
`Program Files (x86)\Windows Kits\10\bin`. If the SDK is somewhere else, set
`WINDOWS_SDK_BIN` to the folder that holds `makeappx.exe`. The GitHub x64 and
ARM64 Windows runners have all three tools installed.

### The three modes

| Mode | Signed? | Identity | Install it with | For |
|---|---|---|---|---|
| `--mode test` (default) | no | ours, plus Microsoft's "unsigned" OID | `Add-AppxPackage -AllowUnsigned <file>` in an **elevated** PowerShell, Windows 11 only | trying a build as a packaged app; never distribute it |
| `--mode store` | no | Partner Center's: `MSIX_IDENTITY_NAME`, `MSIX_PUBLISHER`, `MSIX_PUBLISHER_DISPLAY_NAME` | the Store | uploading to Partner Center, which signs it |
| `--mode signed` | yes, with `MSIX_SIGN_PFX` + `MSIX_SIGN_PASSWORD` or `MSIX_SIGN_THUMBPRINT` | `MSIX_PUBLISHER` = the certificate's subject, exactly | double-click | distribution outside the Store |

In a package, the publisher is a certificate subject (`CN=...`), and Windows
refuses a signature whose certificate subject differs from it in any letter.
This is why `store` mode refuses to run without Partner Center's values:
an upload with any other identity is rejected. In `signed` mode, set
`MSIX_PUBLISHER` to the subject of the certificate you sign with.

`--mode signed` with the self-signed certificate from `SignUltraDemo.ps1`
produces a package that installs only on a machine that trusts that
certificate. To trust it, import it into *Local Machine > Trusted People*.
This is fine for a test machine, but it is no way to ship to users.

### CI

`build.yml` builds the test package on both Windows legs of every run and
uploads it as `UltraCanvas-Windows-MSIX-<version>-<arch>`. A release build
(main) also builds the Store package, once the repository has Partner
Center's identity in its Actions **variables**:

| Variable | Partner Center, the app's *Product identity* page |
|---|---|
| `MSIX_IDENTITY_NAME` | *Package/Identity/Name* |
| `MSIX_PUBLISHER` | *Package/Identity/Publisher* (`CN=` and a GUID) |
| `MSIX_PUBLISHER_DISPLAY_NAME` | *Package/Properties/PublisherDisplayName* |

These values are public: they are written into every package. Keep them in
variables, not secrets.

## Into the Store

1. In Partner Center, create the app and reserve its name. Copy the three
   identity values above into the repository's Actions variables.
2. Let a release build run, or run `--mode store` locally. Take
   `UltraCanvas-Windows-<version>-x86_64-store.msix` and the `arm64` one.
3. In the submission's *Packages* page, upload both. The Store offers each
   device the one that matches its architecture.
4. In the submission, the *runFullTrust* capability asks for a reason. Give
   this one: these are desktop (Win32) applications.
5. Each later submission needs a higher version. The framework version rises
   with every release, so this happens without extra steps.

## What changes inside a package

The applications run as ordinary full-trust Win32 programs, but Windows
handles four things differently for a packaged program. The table says how
each one affects the applications in this repository as of this writing.

| Inside a package | Effect here |
|---|---|
| The install folder (`C:\Program Files\WindowsApps\...`) is **read-only** | Fine. Everything next to the `.exe` is only read: `Resources/`, `data/`, `lib/`, `Plugins/`, `cacert.pem`. Fonts, caches, crash dumps and settings go to `%APPDATA%` / `%LOCALAPPDATA%`. The one exception is the ImageMagick coder repair (`UltraCanvasCoderModuleRepair.cpp`), which renames coders in place. It does nothing on a `package-win.sh` package, because that script already renamed them. |
| Registry writes under `HKEY_CURRENT_USER` **stay inside the package** | **UltraDesktop** takes over the desktop through two such values: the `Run` key (beside Explorer) and Winlogon's `Shell` (instead of Explorer, through UltraCanvasShellGuard). From a package both land in the package's own registry, which sign-in never reads, so a packaged copy cannot do it. It knows this: `UltraCanvasDesktopShell::CanReplaceSystemDesktop()` is false from a package, `GetSystemDesktopSwitchUnavailableReason()` says why, and UltraDesktop's settings menu and UOS-Settings' Desktop page show that sentence where the switch would be, pointing to the installer. UltraDesktop still runs as a trial window and has its Start menu entry, as in the installer. **Toast notifications** register their AppUserModelID and COM activator under `HKEY_CURRENT_USER\Software\Classes` outside a package. From the package nothing is written. The manifest declares each activator (`windows.comServer` and `windows.toastNotificationActivation`, from `NOTIFICATION_ACTIVATORS` in the script), and the presenter shows each toast under the posting application's own package identity - see *From the MSIX package* in [UltraCanvasNotificationActivation](UltraCanvasNotificationActivation.md). Not yet tried on Windows. File associations come from the manifest, and OAuth sign-in uses a loopback listener, not a URL scheme. |
| The program starts with **System32 as its working folder**, not its own folder | Fine. A package cannot choose the working folder (Microsoft's Package Support Framework can), so nothing may depend on it. **UltraNet's plug-in folder** defaults to `Plugins/UltraNet` beside the executable, or one or two folders above it (`UltraNetPlugins.cpp`), and never to the working folder unless the executable's own path is unknown. Network printer and scanner discovery (the mDNS plug-in, used by DeviceExplorer) finds its plug-in. **The file dialog** with no folder remembered opens in the working folder, unless that is the Windows folder or the root of the drive: then it opens in the home folder (`IsSystemStartFolder` in `UltraCanvasModalDialog.cpp`). |
| Writes to `%APPDATA%` / `%LOCALAPPDATA%` are kept per package | Fine within the package. All the applications share one package, so they still see each other's settings (`%APPDATA%\UltraCanvas`, `%APPDATA%\ultraos`). They read what an unpackaged copy wrote before. |

Two more points:

- **Elevation.** UltraFiler's *Delete as administrator* restarts its own
  `.exe` with `runas`. A packaged program can do that only with the
  restricted capability `allowElevation`. Microsoft grants that capability
  to Store apps only on request, with a written justification, so the
  manifest does not declare it unless asked: `MSIX_ALLOW_ELEVATION=1`
  (`make_msix_layout.py --allow-elevation`), for a sideloaded or enterprise
  package. Without it, `ElevatedFileOperations::IsAvailable()` is false in
  the package, so the Filer does not offer the command. A permission error
  then ends with *Skip*, as on the platforms without the retry.
  `OS/MSWindows/UltraCanvasWindowsPackage.h` reads the installed
  `AppxManifest.xml` to tell. Whether `runas` works in a package that does
  declare it has not been tried on Windows yet.
- **Non-ASCII file names.** When a file type association opens a file,
  Windows passes the file to the application as its first argument, which a
  MinGW program reads as a narrow `argv` in the active code page. Texter and
  UltraViewer embed the framework's manifest with the UTF-8 code page
  (`ultracanvas_embed_windows_manifest`), so a Thai or CJK name arrives
  intact. Windows honours that manifest from Windows 10 1903 on, which is
  why the package's minimum Windows is 1903 (`--min-version 10.0.18362.0`).

## What is in the package

`scripts/make_msix_layout.py` builds the package layout from `dist/`:

- **Everything in `dist/`**, except `uc-diagnose.bat` / `.ps1`. Those start an
  `.exe` from its own folder, and Windows does not allow that inside
  `C:\Program Files\WindowsApps`.
- **`AppxManifest.xml`**: the identity, and one `<Application>` (one Start
  menu entry) for each application. Which programs are applications follows
  the installer's Start menu rule (`scripts/build-windows-installer.sh`), so
  the two list the same programs:
  - a program with a desktop entry (`Apps/*/*.desktop`) is one, under the
    entry's `Name=`, with its `Comment=` as the description and its `Icon=`
    from `media/appicon/`;
  - a GUI program without a desktop entry (Texter, UltraCanvasDemo,
    UltraFiler) is one under its own name, with the icon CMake embeds in it;
  - a console program without a desktop entry (`ultramsg`, `videofx`) is a
    command-line tool and is not one, and neither is UltraCanvasShellGuard.

  A program without an entry still ships in the package.
- **File associations** for Texter (text and source code) and UltraViewer
  (images, video, audio, PDF, e-books, `.ucd`). These put the applications in
  Explorer's *Open with* list, the way `package-macos.sh` declares their
  document types on macOS. The package never makes them the default; only
  the user does. A package names file types by extension, not by MIME type,
  so `FILE_TYPES` in the script spells them out. Windows refuses some
  extensions in a package (`.bat`, `.cmd`, `.js`, `.ps1` and others that run
  code), so they are left out.
- **`Assets/`**: the Start menu, taskbar and Store logos, rendered from
  `media/appicon/` (the same PNGs CMake embeds in each `.exe`). They come in
  each display scale (`scale-100` to `scale-400`) and at each exact taskbar
  size (`targetsize-16` to `-256`). The exact-size icons also come in
  `altform-unplated` versions, so the taskbar draws the icon itself and not
  the icon on an accent-coloured square.
- **`resources.pri`**: built by `makepri`. It tells Windows which of those
  files to draw at which size.

The whole suite is one package, because the applications share the same
hundreds of DLLs. A package per application would carry its own copy of all
of them. The macOS suite folder was made one shared folder for the same
reason (see *Packaging a new app for macOS* in `AGENTS.md`).

### Adding an application

Nothing to do here. A new application with a desktop entry, or built as a
GUI program, gets its Start menu entry in the package as it does in the
installer. The Application Id is the `.exe` name with everything but letters
and digits removed, so `UOS-Settings` becomes `UOSSettings`, and the script
refuses two programs that end up with the same Id. Opening file types from
Explorer is the one thing to add by hand, in `FILE_TYPES`.

### Checking a manifest without Windows

`makeappx` runs only on Windows. Microsoft's open-source
[MSIX SDK](https://github.com/microsoft/msix-packaging) builds a `makemsix`
on Linux and macOS that validates `AppxManifest.xml` against the same
schemas. Build it with `./makelinux.sh --pack --skip-tests`. It needs ICU,
and CMake 3.29 or newer. Run it on a layout from `make_msix_layout.py`:

```bash
python3 scripts/make_msix_layout.py --dist dist --layout /tmp/layout \
    --pri-root /tmp/pri --pri-config /tmp/priconfig.xml --version 0.9.347 \
    --arch x64 --identity-name ULTRAOSDevelopment.UltraCanvas \
    --publisher "CN=Test" --publisher-display-name Test --unsigned-test
LD_LIBRARY_PATH=<sdk>/.vs/lib <sdk>/.vs/bin/makemsix pack -d /tmp/layout -p /tmp/t.msix
```

A manifest that breaks the schema fails here with the line and the rule it
broke, for example an Application Id with a `-` in it.
