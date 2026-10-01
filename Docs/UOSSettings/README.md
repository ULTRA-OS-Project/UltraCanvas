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
| `ui/UOSSettingsWindow.*` | The window: page tree, pages, the Last used folder table |
| `UOS-Settings.desktop` | The freedesktop entry |
| `media/appicon/UOS-Settings.{svg,png}` | The application icon |
