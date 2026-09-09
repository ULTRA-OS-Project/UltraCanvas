# UltraCanvasIconResource — Windows icon resources without Windows

`UltraCanvasIconResource.h` reads the icons a Windows file carries: the frames
of an `.ico`, and the `RT_GROUP_ICON` / `RT_ICON` resources of a PE binary
(`.exe`, `.dll`, and the icon libraries that share the format). It is what
gives a program its own icon in a file display on a system that has no Windows
shell to ask.

Implemented in `core/UltraCanvasIconResource.cpp`: the PE resource directory
is a small tree of fixed-size records, and an icon frame is either a PNG
(handed to the image pipeline) or a DIB with a 1-bit transparency mask
(decoded here). No Windows API, no new dependency, safe on a background
thread.

```cpp
#include "UltraCanvasIconResource.h"

if (UltraCanvas::HasIconResourceExtension(path)) {          // no file access
    auto pixmap = UltraCanvas::LoadIconResource(path, 0, 48);  // blocking
}
```

| Function | Answers |
|---|---|
| `HasIconResourceExtension` | can this **kind** of file hold icons? (`.ico`, `.exe`, `.dll`, `.icl`, `.cpl`, `.ocx`, `.scr`, `.mun`) — extension only, no file access |
| `LoadIconResource(path, index, desiredSize)` | the icon as a `UCPixmap`, or null |
| `DecodeIconFileBytes(bytes, desiredSize)` | the same for bytes already in hand — an icon inside an archive, one fetched over the network |

## Which icon, and which size

`index` follows the convention a Windows shortcut's icon index uses: a
**non-negative** index selects the n-th icon of the file in resource order, a
**negative** one names a resource id (`-index`). An index that names nothing
falls back to the file's first icon — a stale index written into a shortcut
years ago must not blank the tile.

An icon holds the same picture at several sizes. The frame nearest
`desiredSize` is the one decoded: the exact size if it is there, else the
smallest frame **larger** than it (downscaling keeps detail), else the largest
frame there is, with colour depth breaking a tie. The pixmap comes back at the
frame's own size — the caller's fit mode is what scales it into place.

The chosen frame is not the only one tried: a file can name a resource that is
not there, or hold a frame in a form this build cannot decode, and a picture
at the wrong size beats no picture at all.

## What it reads, and what it refuses

- **Frames**: PNG frames (the Vista-and-later 256 px form) go through
  `UCImageRaster::LoadFromMemory`. DIB frames are decoded here at 1, 4, 8, 16,
  24 and 32 bits per pixel, bottom-up, with the palette and the AND mask the
  format puts below the colour bitmap. A 32-bit frame whose alpha band is
  entirely zero is a pre-XP icon that means the mask, not "invisible", and is
  read as such.
- **The file is not read whole.** A program's headers are its first few
  kilobytes and its icons are one section; a 300 MB installer is not pulled
  into memory to find a 32-pixel picture. Only the header range and the
  resource section are read.
- Every offset in these formats comes out of the file itself, so all of them
  are treated as untrusted: a record pointing outside the data it lives in
  produces "no icon", never a read past the end.
- The first bytes decide which reader a file gets, not its name — an `.exe`
  that is a script and an `.ico` that is something else both exist.

## Where it is used

`LoadNativeFileIconPixmap` (`UltraCanvasNativeFileIcons.h`) off Windows, for
`.exe` / `.dll` / `.ico` and for the `.lnk` shortcuts that name one of them
(read with [UltraCanvasShellLink](UltraCanvasShellLink.md)). On Windows the shell is asked first and this reader is
the fallback for a file the shell declines. The filer widget's thumbnail
workers are what call it; see
[UltraCanvasFilerWidget.md](UltraCanvasFilerWidget.md#native-application-icons).

## Test

`Tests/IconResourceTest.cpp` assembles an `.ico` and a minimal PE binary in
memory — section table, three-level resource tree, a group icon naming its
`RT_ICON` — and checks the decoded pixels, including the mask-driven
transparency.
