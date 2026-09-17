# UltraCanvasHostFileIcons — the icons the host desktop draws

`UltraCanvasHostFileIcons.h` answers one question: **what does the system this
application is running on draw for a file of this kind?** The picture Explorer
puts on a `.txt`, the one Finder puts on a `.pdf`, the one the Linux icon theme
puts on a folder. A file display that asks it looks like the rest of the
desktop instead of like a second file manager.

It is the type-wide half of a pair, and the two are easy to confuse:

| Module | Question | Where the answer lives |
|---|---|---|
| `UltraCanvasNativeFileIcons` | what icon does **this file** carry? | inside the file — a `.exe`'s resources, an `.ico`, the icon a `.lnk` or `.desktop` names, a bundle's `.icns` |
| `UltraCanvasHostFileIcons` | what does **this system** draw for this **kind** of file? | the host's shell or icon theme — nothing in the file to read |

So a program is drawn as itself on every platform (the native module reads the
picture out of the file, which is why a Windows disk mounted on Linux still
shows its programs correctly), while a `.txt` is drawn as *this* desktop draws
a text file, and looks different on a different desktop. A caller asks the
native module first and this one for everything else.

```cpp
#include "UltraCanvasHostFileIcons.h"

if (UltraCanvas::HostFileIconsAvailable()) {
    // The key files of this kind share — ask once per key, not once per file.
    const std::string key = UltraCanvas::HostFileIconKey(path, isDirectory);
    // Blocking: on a worker thread, holding a NativeFileIconThreadScope.
    auto pixmap = UltraCanvas::LoadHostFileIconPixmap(path, isDirectory, 48);
}
```

| Function | Answers |
|---|---|
| `HostFileIconsAvailable()` | is there a desktop to ask on this build at all? |
| `HostFileIconKey(path, isDirectory)` | what the answer depends on — the cache key, pure string work |
| `LoadHostFileIconPixmap(path, isDirectory, desiredSize)` | the icon as a `UCPixmap`, or null |
| `RefreshHostFileIcons()` | forget the lookups — after a theme change |

## The key is the whole design

A host icon belongs to a **type**, not to a file, and `HostFileIconKey` is
what says which type. That one string decides the cost of the feature:

- key two kinds the same, and a folder draws one of them with the other's
  icon;
- key them too finely, and a folder of four thousand `.txt` files performs
  four thousand icon-theme lookups instead of one.

The keys:

| What | Key | Why |
|---|---|---|
| a directory | `dir` | every folder is one icon |
| a file that carries its own icon | `file:<path>` | not a type at all — two programs must never share an icon |
| a name with no extension | `name:<lowercased name>` | the freedesktop database matches `makefile` and `README` by name |
| everything else | `ext:<suffix chain>` | `archive.tar.gz` keys as `tar.gz`, not `gz` — a tarball and a gzip file are different types and are drawn differently |

The suffix chain runs from the **first** dot, which is the finest distinction
the type databases make. A leading dot is part of the name (`.bashrc` is a
name, not a `bashrc` file).

## The three backends

**Linux / BSD** — `OS/Linux/UltraCanvasLinuxHostFileIcons.cpp`. Two steps,
neither of which is this module's own work:

1. the file's MIME type, from `FileAssociations::GetMimeType` — the
   shared-mime-info globs the "Open with" menu is already built from, so the
   framework keeps exactly one reader of that database;
2. the icon **names** that type is drawn under, per the icon-naming
   specification, looked up in the installed themes by
   `FindDesktopIconFile` — the same resolver a `.desktop` entry's `Icon=`
   goes through.

The names are tried in order, most specific first:

| Order | Name | Example for `archive.tar.gz` |
|---|---|---|
| 1 | the MIME name with its slash replaced | `application-x-compressed-tar` |
| 2 | the generic icon the type database names (`FileAssociations::GetMimeGenericIcon`) | `package-x-generic` |
| 3 | the generic icon of the media type | `application-x-generic` |
| 4 | `application-x-generic`, then `unknown` | — |

Step 2 is what makes a PDF an office document and a tarball a package: no
theme ships an icon for each of the two thousand known types, and
`mime/generic-icons` is the database saying which picture each belongs under.
A machine with no icon theme installed answers nothing at every step, and the
caller draws its own icon.

**Windows** — `OS/MSWindows/UltraCanvasWindowsHostFileIcons.cpp`. The shell's
system image list, which is the list Explorer itself draws from, indexed by
`SHGetFileInfoW` with `SHGFI_USEFILEATTRIBUTES`: the type is decided from the
file **name**, so the shell answers from the registry without opening — or
even finding — the file, and a path on a disconnected volume still gets its
icon. The smallest image list that will not be scaled up is used, down to the
plain associated icon where a locked-down shell refuses the lists. A
transparent border is trimmed off what comes back: the jumbo list is a 256×256
canvas, and a type whose icon exists only at 48 sits in the middle of it with
empty space all round, which drawn into a tile would be a postage stamp.

**macOS** — `OS/MacOS/UltraCanvasMacOSHostFileIcons.mm`. `NSWorkspace`, asked
for the content type the extension names rather than for the file, so one
lookup serves every file of a kind. Where the content-type API is not
available the path itself is asked, which also picks up a file's custom icon —
what Finder shows there too. Drawing an `NSImage` into a bitmap is the one
piece of AppKit rendering that is safe off the main thread, which is where a
file display calls this from.

**WebAssembly and Android** report unavailable: there is no desktop to ask, so
`LoadHostFileIconPixmap` returns null and a caller keeps its own icons.

## Threading

`LoadHostFileIconPixmap` blocks — it reads icon themes or calls the shell —
and belongs on a worker thread. Hold a
`NativeFileIconThreadScope` for the life of
that thread: on Windows the shell expects the calling thread to have joined a
COM apartment, and a thread that has not can fail an extraction that works
perfectly well elsewhere. Elsewhere the scope is an empty object.

Null is not a failure. It is the ordinary answer on a machine with no icon for
the type, and callers must draw something of their own for it — the filer
widget falls back to its built-in icons, so the display is never empty and
never blocks on a lookup.

## Who uses it

`UltraCanvasFilerWidget`, under **Display > File icons > Host OS icons** (see
[`UltraCanvasFilerWidget.md`](UltraCanvasFilerWidget.md)). The widget holds
its own cache keyed by `HostFileIconKey` plus the icon size, resolved on one
background thread, and draws its simple icons until an answer lands.
