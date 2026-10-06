# Packaging the Windows Applications as MSIX

How the Windows build becomes one MSIX package. The package installs every
UltraCanvas application with its own Start menu entry. This page covers
building the package in each of its three modes, putting it into the
Microsoft Store, and what changes for an application once it runs from a
package.

The zip that `package-win.sh` produces stays as it is. The MSIX is built from
the same `dist/` folder, so it holds the same files, plus a manifest and the
logos.

## Which route to the Store

The Microsoft Store takes a Win32 application in two shapes. Here is how each
one fits this repository today:

| | MSIX (this page) | EXE / MSI installer |
|---|---|---|
| What we upload | the `.msix` itself, to Partner Center | a URL to an installer on our own server |
| Code signing | **the Store signs it**: no certificate needed | every `.exe` **and every `.dll`** must be signed with a certificate from a CA in Microsoft's Trusted Root Program. `dist/` holds several hundred DLLs, including about 130 ImageMagick coder modules |
| Hosting | Microsoft's CDN | ours, at a versioned URL whose file must never change after submission |
| Installer | none: Windows installs the package | one we would have to write (WiX, Inno Setup, NSIS), and it must install silently |
| Updates | the Store delivers them | a new versioned URL for each release |

We have no installer and no CA-issued certificate. The signing scripts
`SignUltraDemo.ps1` and `SignUltraTexter.ps1` make a self-signed one, which
neither route accepts. So MSIX is the shorter road, and Microsoft calls it the
recommended format.

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

## What is in the package

`scripts/make_msix_layout.py` builds the package layout from `dist/`:

- **Everything in `dist/`**, except `uc-diagnose.bat` / `.ps1`. Those start an
  `.exe` from its own folder, and Windows does not allow that inside
  `C:\Program Files\WindowsApps`.
- **`AppxManifest.xml`**: the identity, and one `<Application>` (one Start
  menu entry) for each application in the script's `APPS` table whose `.exe`
  is in `dist/`. Executables that are not in the table still ship, but they
  get no entry. These are the command-line tools (`ultramsg`,
  `ultrafibu_cli`, `ultrawin-setup`) and the ULTRA OS desktop components
  (UltraDesktop, UOS-Settings, UltraWinManager).
- **File associations** for Texter (text and source code) and UltraViewer
  (images, video, audio, PDF, e-books, `.ucd`). These put the applications in
  Explorer's *Open with* list, the way `package-macos.sh` declares their
  document types on macOS. The package never makes them the default; only
  the user does. Windows refuses some extensions in a package (`.bat`,
  `.cmd`, `.js`, `.ps1` and others that run code), so they are left out.
- **`Assets/`**: the Start menu, taskbar and Store logos, rendered from
  `media/appicon/` (the same PNGs CMake embeds in each `.exe`). They come in
  each display scale (`scale-100` to `scale-400`) and at each exact taskbar
  size (`targetsize-16` to `-256`). The exact-size icons also come in
  `altform-unplated` versions, so the taskbar draws the icon itself and not
  the icon on an accent-coloured square.
- **`resources.pri`**: built by `makepri`. It tells Windows which of those
  files to draw at which size.

The whole suite is one package, because the applications share the same
several hundred DLLs. A package per application would carry its own copy of
all of them. The macOS suite folder was made one shared folder for the same
reason (see *Packaging a new app for macOS* in `AGENTS.md`).

### Adding an application

Add a row to `APPS` in `scripts/make_msix_layout.py`. Each row gives the
`.exe` name without `.exe`, the Start menu name, the icon in
`media/appicon/` and a one-line description. This is all a new application
needs: `package-win.sh` already copies every `.exe`. The Application Id is
the `.exe` name with everything but letters and digits removed, so
`UOS-Settings` would become `UOSSettings`. Two applications must not end up
with the same Id.

### Checking a manifest without Windows

`makeappx` runs only on Windows. Microsoft's open-source
[MSIX SDK](https://github.com/microsoft/msix-packaging) builds a `makemsix`
on Linux and macOS that validates `AppxManifest.xml` against the same
schemas. Build it with `./makelinux.sh --pack --skip-tests`. It needs ICU,
and CMake 3.29 or newer. Run it on a layout from `make_msix_layout.py`:

```bash
python3 scripts/make_msix_layout.py --dist dist --layout /tmp/layout \
    --pri-root /tmp/pri --pri-config /tmp/priconfig.xml --version 0.9.173 \
    --arch x64 --identity-name ULTRAOSDevelopment.UltraCanvas \
    --publisher "CN=Test" --publisher-display-name Test --unsigned-test
LD_LIBRARY_PATH=<sdk>/.vs/lib <sdk>/.vs/bin/makemsix pack -d /tmp/layout -p /tmp/t.msix
```

A manifest that breaks the schema fails here with the line and the rule it
broke, for example an Application Id with a `-` in it.
