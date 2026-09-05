# UltraCanvasShellLink — Windows shortcuts (.lnk) on every platform

`UltraCanvasShellLink.h` reads the Windows shell link format (MS-SHLLINK) —
the `.lnk` files a Windows desktop is made of — and answers the two questions
a file display asks about one: **what does it point at**, and **which icon is
it drawn with**.

Windows resolves a shortcut through the shell. Nothing else does, which is why
a filer looking at a mounted Windows disk, at the `drive_c` of a Wine prefix,
or at a folder synced from a Windows machine used to show a desktop full of
nameless "LNK" sheets. This module is the missing piece: it parses the file
itself, so the answer is the same on Windows, ULTRA OS, Linux and macOS.

Implemented in `core/UltraCanvasShellLink.cpp` on byte parsing plus
`std::filesystem` — no Windows API, no new dependency, and safe to call from a
background thread (it holds no state between calls).

## Reading a link

```cpp
#include "UltraCanvasShellLink.h"

UCShellLink link;
if (UltraCanvas::ReadShellLink("/mnt/win/Users/Sam/Desktop/Etcher.lnk", link)) {
    link.targetPath;      // "C:\\Program Files\\Etcher\\Etcher.exe"
    link.hostTargetPath;  // "/mnt/win/Program Files/Etcher/Etcher.exe"
    link.arguments;       // "--no-sandbox"
    link.iconLocation;    // the file holding the icon, when the link names one
    link.iconIndex;       // index into it (negative = a resource id)
    link.targetIsDirectory;
}
```

`ReadShellLink` returns false — leaving the output untouched — for a file that
is missing, truncated, or does not carry the format's header signature and
CLSID. A file that merely ends in `.lnk` is therefore never mistaken for a
shortcut.

| Field | What it holds |
|---|---|
| `targetPath` | the target as the link stores it, a Windows path |
| `hostTargetPath` | the same file as **this** host opens it; empty when it is not here |
| `arguments` / `workingDirectory` | the command line the link starts the target with |
| `description` | the link's comment |
| `relativePath` | `".\\App.exe"` — the target relative to the link itself |
| `iconLocation` / `hostIconLocation` / `iconIndex` | the icon the link is drawn with |
| `targetIsDirectory` | the target is a folder (from its stored attributes) |
| `targetIsNetworkPath` | the target is a UNC name, resolved through the network |
| `targetSize` / `targetModifiedTime` | what the target was when the link was made |

The icon a shortcut is drawn with follows Explorer's rule, and every caller
walks the same chain: `hostIconLocation` at `iconIndex` when the link names an
icon, else icon 0 of `hostTargetPath`. That is what
`LoadNativeFileIconPixmap` does for a `.lnk` — through the shell on Windows
(which adds the target's file association for a shortcut to a document or a
folder), and through
[UltraCanvasIconResource](UltraCanvasIconResource.md) everywhere else.

## Windows paths on a host that is not Windows

Every path inside a shortcut is a Windows path, and `C:\Program Files\…` means
nothing to a POSIX `open()`. `ResolveWindowsPathOnHost(windowsPath,
contextPath)` maps one, and it is what fills every `host*` field above.
`contextPath` is a host path near the file — normally the shortcut being read
— and is what makes the mapping possible: the drive the path names is looked
for by walking up from it, in this order.

1. A **Wine prefix**: an ancestor named `drive_c`, or one holding a
   `dosdevices` directory. Drive letters resolve through
   `dosdevices/<letter>:`, which is exactly the mapping a prefix defines.
2. The **root of a mounted Windows disk**: an ancestor holding both a
   `Windows` and a `Users` directory. Only `C:` is recognisable this way,
   which is the letter that matters — it is where the programs a shortcut
   points at are installed.
3. `$WINEPREFIX`, then `~/.wine`, for a shortcut that sits somewhere else
   entirely.

Path components below the drive root are matched **case-insensitively**,
because Windows wrote them that way and the host filesystem is not: a link that says `C:\PROGRA~\ETCHER`
still finds `Program Files/Etcher`. The result is canonical (symlinks
resolved), so an entry reached through a prefix's `dosdevices` symlink
compares equal to the same file listed anywhere else. An empty return means
"this host does not have that file" — never a guess.

On Windows the function only expands environment variables and confirms the
file is there.

## Environment variables

`ExpandWindowsEnvironmentPath` expands `%VAR%` references the way the shell
would: from the process environment on Windows, and off Windows from the fixed
layout of a Windows disk (`%ProgramFiles%` → `C:\Program Files`,
`%SystemRoot%` → `C:\Windows`, and the rest of the well-known set). The
per-user variables (`%USERPROFILE%`, `%APPDATA%`, `%LOCALAPPDATA%`) need a
profile, which `ResolveWindowsPathOnHost` derives from the shortcut's own
location when it sits inside one.

An **unknown variable is left as it stands** rather than replaced with
nothing: a path with a visible `%FOO%` in it is a readable failure, while one
with the reference silently dropped looks valid and is not.

This matters more than it sounds. A shortcut written on another machine often
carries an absolute path that is wrong here *and* an environment form that is
right — the format stores both, in the `EnvironmentVariableDataBlock` — and
the reader tries every form the link offers, in order, keeping the first the
host can actually find.

## What is not parsed

The **target ID list** is skipped. It describes shell items rather than files:
a shortcut to a file repeats the path in its LinkInfo block, and one to a
virtual item (a Control Panel page, a packaged app, a Recycle Bin) has no file
path at all. Such a link reads successfully with an empty `targetPath` — it is
still a shortcut, it just does not point at a file this or any other program
can open by path.

## Where it is used

`UltraCanvasFilerWidget` reads every `.lnk` it lists: the entry's type,
category, info column and icon all come from the target
(`FilerEntry::isShortcut`, `FilerEntry::linkTarget`). See
[UltraCanvasFilerWidget.md](UltraCanvasFilerWidget.md#shortcuts-lnk).

## Test

`Tests/ShellLinkTest.cpp` builds links byte by byte (`ShellLinkTestSupport.h`)
and reads them back — LinkInfo targets, `%ProgramFiles%` targets, folder
targets, non-Unicode strings, case-insensitive resolution inside a prefix, and
the files that end in `.lnk` without being links.
`Tests/FilerShortcutEntryTest.cpp` takes the same links through a folder scan
and checks the entries the file display produces from them.
