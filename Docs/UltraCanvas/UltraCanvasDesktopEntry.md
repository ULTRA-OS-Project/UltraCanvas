# UltraCanvasDesktopEntry — freedesktop desktop entries (.desktop)

`UltraCanvasDesktopEntry.h` reads the `.desktop` files a Linux or BSD desktop
is built from — the format's own name for a shortcut — and resolves the icon
names they carry into image files.

It is the counterpart of [`UltraCanvasShellLink`](UltraCanvasShellLink.md),
which does the same for a Windows `.lnk`, and it is the framework's **single**
reader for the format: the "Open with" service builds its application index
with it, and the filer widget draws and launches the launchers a folder holds.
Two readers would eventually disagree about what the same file is called.

Plain text parsing plus `std::filesystem` in `core/UltraCanvasDesktopEntry.cpp`
— no GIO, no GTK, no dependency, and safe on a background thread (which is
what lets the filer's thumbnail workers resolve icons). Nothing here launches
anything; that is `UltraCanvasFileAssociations`.

## Reading an entry

```cpp
#include "UltraCanvasDesktopEntry.h"

UCDesktopEntry entry;
if (UltraCanvas::ReadDesktopEntry("/usr/share/applications/firefox.desktop", entry)) {
    entry.kind;      // Application / Link / Directory / Unknown
    entry.name;      // "Firefox Web Browser", in the user's language
    entry.exec;      // "firefox %u" — verbatim, field codes intact
    entry.program;   // "/usr/bin/firefox" — what it actually runs, or ""
    entry.iconName;  // "firefox" — a name, not a file
}
```

`ReadDesktopEntry` returns false for a file that cannot be read or that has no
`[Desktop Entry]` group, so a file that merely ends in `.desktop` is never
mistaken for one. Only the main group is read: a `[Desktop Action …]` group
below it cannot overwrite the entry's own name or command.

| Field | What it holds |
|---|---|
| `kind` | `Type=` — an Application to run, a Link to open, a Directory's metadata |
| `name` / `genericName` / `comment` | the localized strings, in the user's language |
| `exec` / `tryExec` / `workingDirectory` | `Exec=`, `TryExec=`, `Path=` |
| `program` | the executable those resolve to on **this** machine; empty when it is not installed |
| `iconName` / `iconFile` | `Icon=` and, when a caller resolves it, the image file it names |
| `url` | `URL=`, for a `Type=Link` web shortcut |
| `mimeTypes` | `MimeType=` — what the application declares it opens |
| `terminal` / `noDisplay` / `hidden` | the flags, reported rather than acted on |

**Localization** follows the specification: `Name[de_DE]` beats `Name[de]`
beats `Name`, whichever order the file lists them in, matched against
`LC_MESSAGES` / `LC_ALL` / `LANG`. A language the user does not read is
ignored entirely. The language is resolved per file rather than once per
process, so an application that changes locale is not stuck with the language
its first folder listing happened to use.

## Icons: a name, not a file

`Icon=` almost never names a file. It names an *icon*, to be found in the icon
themes installed on the machine, at whatever size the caller wants:

```cpp
std::string file = UltraCanvas::FindDesktopIconFile(entry.iconName, 48);
```

The lookup is the icon-theme specification's, in this order:

1. The **configured theme**, detected from the desktop's own settings — GTK's
   `settings.ini` (`gtk-icon-theme-name`), KDE's `kdeglobals`, the GTK2
   `.gtkrc-2.0` — or set by the host with `SetDesktopIconTheme()`.
2. Everything that theme **inherits**, breadth first, from each theme's
   `index.theme`.
3. **hicolor**, which every theme is required to fall back to.
4. The flat **pixmaps** directories that predate the spec.

Within a theme the size decides the directory: the **exact** size if it is
installed, else a **scalable** (SVG) icon — which is every size — else the
nearest larger, else the largest smaller. Base directories are the standard
set (`$XDG_DATA_HOME/icons`, `~/.icons`, `$XDG_DATA_DIRS/*/icons`), and an
`Icon=` that is an absolute path is taken as it stands.

Each theme's directory listing and every lookup are cached, so a folder of a
hundred launchers costs one listing per theme rather than a directory walk per
icon. `RefreshDesktopIconThemes()` drops all of it — after the user changes
theme, or a package installs icons.

## The command it runs

```cpp
std::vector<std::string> argv =
        UltraCanvas::DesktopEntryCommand(entry, {"/home/sam/notes.txt"});
```

`Exec=` is tokenized the way the specification says (double quotes group, a
backslash escapes inside them) and its field codes are expanded: `%f`, `%F`,
`%u` and `%U` are replaced by the files, `%i`, `%c`, `%k` and unknown codes
are dropped, `%%` is a literal percent, and an entry with no file code at all
gets the files appended. The result is ready for `exec()`; launching it —
detached, with the right working directory — is
[`UltraCanvasFileAssociations`](UltraCanvasFileAssociations.md)'s
`OpenWithApplicationPath`, which accepts a `.desktop` path directly.

## Where it is used

- **The "Open with" service** builds its Linux application index from it, and
  draws each row with `FindDesktopIconFile`.
- **`UltraCanvasFilerWidget`** resolves every `.desktop` a folder holds: the
  entry is drawn by its `Name=`, with the icon its `Icon=` names, and
  activating it runs what it says. See
  [UltraCanvasFilerWidget.md](UltraCanvasFilerWidget.md#shortcuts).

## Test

`Tests/DesktopEntryTest.cpp` builds a throwaway icon theme (a custom theme
inheriting hicolor, icons at several sizes, a scalable one, a flat pixmap) and
points `XDG_DATA_HOME` at it, so what it asserts about the lookup does not
depend on what the machine running it has installed. It also covers the
localized keys, the `[Desktop Action]` group, `Type=Link`, the `Exec=` field
codes and the files that end in `.desktop` without being entries.
