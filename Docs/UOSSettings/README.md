# UOS-Settings

## Overview

UOS-Settings is the ULTRA OS settings application: it holds the settings
that belong to the system rather than to one application. It ships as
`Apps/UOSSettings` and builds the program `UOS-Settings`
(`-DBUILD_UOS_SETTINGS=ON`, the default). Its version is the first line of
`Docs/UOSSettings/CHANGELOG.md` and shows in the window title.

The window is laid out like UltraFiler's settings: a tree of pages on the
left, the selected page on the right and Close at the foot. There is no
Apply - every change is written at once and is picked up the next time an
application opens the setting.

## Pages

### Desktop

UltraDesktop's settings, which used to be a window of the desktop's own.
UOS-Settings opens on this page.

| Field | What it does |
|---|---|
| **Taskbar** | The screen edge the taskbar sits on: Left, Top or Bottom. |
| **Wallpaper** | The picture behind the desktop; empty is the framework's picture. *Browse...* opens the file dialog. |
| **RAM disc** | The folder the desktop's drive button opens. |
| **File manager** | The program the desktop's folder buttons start. |
| **Virtual desktops** | How many the organiser offers (1-9); asked of the window manager, which may offer another number. |

A dropdown is saved when it changes, a text field when it is left or Return
is pressed. The fields go to UltraDesktop's settings file -
`<config dir>/ultraos/desktop.json` (`$XDG_CONFIG_HOME`, else `~/.config`;
`%APPDATA%` on Windows) - read, changed and written back, so the sticky notes
UltraDesktop keeps in the same file are not touched. A running UltraDesktop
checks the file once a second and rebuilds its bars when one of these fields
changed. The file is read and written by UltraDesktop's own settings code
(`Apps/UltraDesktop/ui/UltraDesktopSettings.*`, compiled into UOS-Settings
too), so the two programs agree on it.

### File dialogs > Last used folder

Where the framework's file dialog (`UltraCanvasFileDialog`) opens when the
application does not name a folder itself - UltraMail's *Attach file*, for
one.

| Control | What it does |
|---|---|
| **Applications use: One common folder** | Every application shares one last used folder: whichever used a file dialog last, the next opens there. |
| **Applications use: Their own folders** | Each application decides, in the table below. |
| Table: **Application** | The applications that use the framework's file dialog (`KnownFileDialogApplications()`), plus any other that has opened one. |
| Table: **Opens in** | The folder that application's next file dialog opens in. |
| Table: **Global \| Individual** | Global: the application shares the common folder. Individual: it keeps its own, starting from the common one until it has used a folder of its own. Only active under *Their own folders*. |

The table has a header and scrolls vertically when there are more
applications than fit.

### Devices > Trusted certificates

The network scanners and printers whose self-signed HTTPS certificate was
trusted the first time they were reached (IODeviceManager's trust on first
use - see `Docs/Modules/IODeviceManager/Architecture.md`). From then on each
is reached only while it presents the same key; a device that was reset or
replaced presents a new one and is refused until its old key is forgotten.

| Column | What it shows |
|---|---|
| **Device** | The name the device was discovered under - its DNS-SD instance name, or, for a device named by its address in configuration, the name a printer gives itself or the make and model a scanner reports - kept beside its key. *(name not known)* for a key learned before any name was, until the device is listed again. |
| **Address** | The address the key is kept under, `host:port`. |
| **Key** | The SHA-256 of the device's public key, base64; the tooltip has it in full. |
| **Forget** | Removes the key. The device's new key is learned the next time it is reached - on the same terms as the first time, so forget one only when you know it was reset or replaced. |

A device whose certificate a certificate authority vouches for is never
listed: it is checked the ordinary way. With nothing trusted yet, the page
says when a device will appear. The keys are kept in
`DeviceCertificates.conf` in the same settings folder as `FileDialog.conf`
(below), read and written through `UltraCanvasIODeviceTlsTrust.h`, so
UOS-Settings and the applications that reach the devices agree on it.

## Where the settings live

`FileDialog.conf` in the UltraCanvas settings folder - `%APPDATA%\UltraCanvas`
on Windows, `~/Library/Application Support/UltraCanvas` on macOS,
`$XDG_CONFIG_HOME/UltraCanvas` (or `~/.config/UltraCanvas`) elsewhere. Both
the file dialog and UOS-Settings use `UltraCanvas::FileDialogSettings`
(`UltraCanvasFileDialogSettings.h`); every write re-reads the file first, so
one program never throws away what another wrote meanwhile.

```
lastfolder.mode=individual        # global | individual
folder=/home/me/Documents         # the common folder
app.UltraMail.scope=individual    # global | individual
app.UltraMail.folder=/home/me/Mail attachments
```

## Files

| File | What it is |
|---|---|
| `main.cpp` | Starts the application and the window |
| `ui/UOSSettingsWindow.*` | The window: page tree, the Desktop page, the Last used folder and Trusted certificates tables |
| `UOS-Settings.desktop` | The freedesktop entry |
| `media/appicon/UOS-Settings.{svg,png}` | The application icon |
