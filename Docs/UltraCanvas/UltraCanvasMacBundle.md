# UltraCanvasMacBundle — macOS bundles and shortcuts

`UltraCanvasMacBundle.h` is the third of the three desktop readers, after
[`UltraCanvasShellLink`](UltraCanvasShellLink.md) (Windows `.lnk`) and
[`UltraCanvasDesktopEntry`](UltraCanvasDesktopEntry.md) (freedesktop
`.desktop`). macOS spreads the same idea over three things, and this covers
all of them:

| | What it is | Read where |
|---|---|---|
| **`.app` bundle** | a directory the Finder presents as one object: `Info.plist` names the application, its executable and its `.icns` | every platform |
| **`.webloc`** | a property list holding a web address | every platform |
| **Finder alias** | a file whose contents are bookmark data — hints that let the system find a target that has since *moved* | macOS only |

The first two are read from the files themselves through
[`UltraCanvasPropertyList`](UltraCanvasPropertyList.md), with no Apple API,
so a Mac disk mounted on ULTRA OS, Linux or Windows shows its applications
with their real names and icons — the same courtesy the other two readers do
for their formats.

## An application bundle

```cpp
#include "UltraCanvasMacBundle.h"

UCAppBundle bundle;
if (UltraCanvas::ReadApplicationBundle("/Applications/Mail.app", bundle)) {
    bundle.displayName;   // "Mail" — CFBundleDisplayName, else CFBundleName,
                          // else the folder name without ".app"
    bundle.identifier;    // "com.apple.mail"
    bundle.executable;    // Contents/MacOS/Mail, when it is really there
    bundle.iconFile;      // Contents/Resources/…​.icns
    bundle.isApplication; // CFBundlePackageType APPL, or it has an executable
}                         // where an application keeps one
```

`ReadApplicationBundle` returns false for a directory with no readable
`Info.plist`, so a folder that merely ends in `.app` is never mistaken for an
application. `IsBundlePath` covers the wider set of packages the Finder shows
as single objects (`.app`, `.framework`, `.bundle`, `.prefPane`, `.appex`, …);
`IsApplicationBundlePath` is the narrower question activation cares about.

**Finding the icon** is the fiddly part. `CFBundleIconFile` often omits the
`.icns` extension, and a modern bundle may instead name an entry in a compiled
asset catalog — which is not a file at all. So: the named file, then the same
name with `.icns` appended, then `CFBundleIconName`, and failing all of that
the `.icns` sitting in `Contents/Resources` (preferring `AppIcon.icns`). What
comes back is a path [`UltraCanvasIconResource`](UltraCanvasIconResource.md)
decodes.

## Web locations and aliases

```cpp
std::string url;
UltraCanvas::ReadWebLocation("/Users/sam/Desktop/Site.webloc", url);
```

A Finder alias is different in kind. Its bookmark data is a set of hints —
volume, file id, path, creation date — and *following* them is the point: an
alias survives its target being moved, which a symlink does not. Only the
system that wrote it can resolve that, so `ResolveFinderAlias` is implemented
on macOS (`OS/MacOS/UltraCanvasMacOSAlias.mm`, via
`URLByResolvingBookmarkData` without UI and without mounting, so a folder
listing never puts a volume password dialog on screen) and returns false
everywhere else. `IsFinderAliasFile` is the cheap check that precedes it: an
alias carries no extension, so the file's first four bytes (`book`, or the
older `alis`) are the only thing to go on.

## Where it is used

- **`UltraCanvasNativeFileIcons`** draws a bundle with the icon inside it, on
  every platform.
- **`UltraCanvasFilerWidget`** shows a bundle by the application's name, with
  its icon, typed `Application`; on macOS double-clicking launches it rather
  than opening the folder it is. A `.webloc` opens its address, and on macOS
  an alias resolves like any other shortcut. See
  [UltraCanvasFilerWidget.md](UltraCanvasFilerWidget.md#shortcuts).

## Test

`Tests/MacBundleTest.cpp` builds a bundle directory (Info.plist, an
executable, an icon), a web location and an alias-shaped file in a temp
folder and reads them back — including the fallbacks for a bundle that names
no icon and no display name, and the directories that only look like bundles.
It runs on every platform, because the reading has to work on every platform.
