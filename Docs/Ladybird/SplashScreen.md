# Ladybird splash screen

The panel the Ladybird port shows while it starts: the Ladybird mark, the
product name, the port's version and release date, and the line crediting the
toolkit the browser's UI is built on. It stays up for **two seconds** and closes early if
the user clicks it.

```
        ┌──────────────────────┐   440 x 640, white, thin black border
        │                      │
        │         ◍            │   media/appicon/Ladybird.png   (250 px box)
        │                      │
        │      Ladybird        │   title,                 28 pt bold
        │     Version 0.1.0    │   LADYBIRD_VERSION,      14 pt
        │      2026-08-28      │   LADYBIRD_VERSION_DATE, 14 pt
        │       GUI by         │   attributionText,       14 pt
        │                      │
        │         ⬡            │   media/images/UltraCanvas-logo.png (100 px)
        │     Ultra Canvas     │   attributionName,       12 pt
        └──────────────────────┘
```

It is not a custom window: it is
[`UltraCanvasSplashScreen`](../UltraCanvas/UltraCanvasSplashScreen.md) with the
port's strings and assets. The `attributionText` / `attributionImagePath` /
`attributionName` fields exist for exactly this — crediting UltraCanvas under
the host's own branding — and were added to the framework for this splash
(UltraCanvas 0.3.93).

## Wiring it into the port

The call belongs in the port's startup, **after** the first browser window has
been created and **before** the event loop is entered — the splash needs a
running loop to time itself out, and a parent window so it lands on the same
monitor as the browser on a multi-monitor desktop.

```cpp
#include "UltraCanvasSplashScreen.h"
#include "UltraCanvasUtils.h"

using namespace UltraCanvas;

UltraCanvasSplashScreen splash;               // must outlive the two seconds

SplashScreenConfig config;
config.width       = 440;
config.height      = 640;
config.showTimeout = 2000;                    // two seconds, then it closes itself

config.imagePath     = NormalizePath(GetResourcesDir() + "media/appicon/Ladybird.png");
config.logoSize      = 250;
config.title         = "Ladybird";
config.titleFontSize = 28;
config.version       = LADYBIRD_VERSION;      // rendered as "Version 0.1.0"
config.versionDate   = LADYBIRD_VERSION_DATE; // the release date, "2026-08-28"

config.attributionText      = "GUI by";
config.attributionImagePath = NormalizePath(GetResourcesDir() + "media/images/UltraCanvas-logo.png");
config.attributionName      = "Ultra Canvas";
config.attributionLogoSize  = 100;

// The design carries the version and the credited name as dark as the title,
// rather than the mid grey the framework uses for secondary lines by default.
config.versionFontSize         = 14;
config.attributionFontSize     = 14;
config.attributionNameFontSize = 12;
config.secondaryTextColor      = Color(20, 20, 20);

splash.Show(config, browserWindow.get());
```

`Show()` returns straight away and the splash takes itself down; nothing has to
be polled or slept on. Startup work that opens a window of its own — a
restore-session prompt, a profile picker — goes in `splash.onSplashClosed` so
it is not fighting an always-on-top window for the foreground, and is repeated
inline behind `if (!splash.IsVisible())` for the case where the splash could
not be created at all. The component doc spells that pattern out.

## The version number and its date

Both come from the same place: the first line of
[`CHANGELOG.md`](CHANGELOG.md) in this directory, which
`cmake/UltraCanvasVersion.cmake` parses into `LADYBIRD_VERSION` and
`LADYBIRD_VERSION_DATE`. The port's CMake already gets them by including that
module; pass them to the compiler and read them here:

```cmake
include(${ULTRACANVAS_REPO}/cmake/UltraCanvasVersion.cmake)
target_compile_definitions(ladybird PRIVATE
    LADYBIRD_VERSION="${LADYBIRD_VERSION}"
    LADYBIRD_VERSION_DATE="${LADYBIRD_VERSION_DATE}")
```

Do not write either into the source. Releasing the port is one new line at the
top of the changelog, and the splash follows it; a literal here is a second
copy that will drift, which is the whole reason the module exists.

The date shown is **when that release shipped**, not when the build ran. A
`__DATE__` stamp would give two builds of one release two different dates and
make a reported "version 0.1.0 of 3 September" impossible to identify.

## Assets

Both files live in this repository's shared resource tree and must be present
under the port's `GetResourcesDir()` — the packaging step copies them alongside
the executable the same way it does for the other applications.

| File | What it is |
|---|---|
| `media/appicon/Ladybird.png` | The Ladybird mark: the black line-art knot on the white-to-lavender-to-blue disc. 512 × 512, transparent outside the disc, so it also serves as the window and taskbar icon. |
| `media/images/UltraCanvas-logo.png` | The UltraCanvas hexagon. Shared with the rest of the framework — do not take a second copy. |

`media/appicon/Ladybird.png` is generated, not hand-drawn:
`scripts/make_ladybird_icon.py` renders it, so the geometry, the stroke weight
and the gradient stops are all in one place if the mark ever needs to be
re-cut at another size.

```sh
python3 scripts/make_ladybird_icon.py media/appicon/Ladybird.png
```
