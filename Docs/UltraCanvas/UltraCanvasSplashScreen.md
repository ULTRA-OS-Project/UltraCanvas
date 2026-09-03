# UltraCanvasSplashScreen

The window an application puts up while it starts: a borderless, always-on-top
panel holding a logo, the product name, its version, an optional attribution
block and an optional website link. It closes when its timeout expires or when
the user clicks it, whichever comes first.

```cpp
#include "UltraCanvasSplashScreen.h"
```

The splash is **not modal**. It is deliberately an ordinary always-on-top
window so that a native dialog opening behind it — a crash-recovery prompt, a
file picker — still appears and can take the focus. Anything that needs the
user's attention should therefore wait for `onSplashClosed` rather than opening
while the splash is up.

```cpp
UltraCanvasSplashScreen splash;

SplashScreenConfig config;
config.width       = 440;
config.height      = 600;
config.imagePath   = NormalizePath(GetResourcesDir() + "media/appicon/Ladybird.png");
config.title       = "Ladybird";
config.version     = LADYBIRD_VERSION;
config.showTimeout = 2000;                       // 2 seconds

config.attributionText      = "GUI by";
config.attributionImagePath = NormalizePath(GetResourcesDir() + "media/images/UltraCanvas-logo.png");
config.attributionName      = "Ultra Canvas";

splash.onSplashClosed = [mainWindow]() { mainWindow->PromptCrashRecovery(); };
splash.Show(config, mainWindow.get());
```

`Show()` returns immediately; the splash lives on the application's event loop
and takes itself down. Keep the `UltraCanvasSplashScreen` alive at least as
long as the splash is on screen — destroying it closes the window.

## Configuration

`SplashScreenConfig` is a plain struct; every field has a working default and
every element it describes is omitted when its field is empty.

| Field | Meaning |
|---|---|
| `imagePath` | The main logo, drawn at the top and centred. Any format the image pipeline loads. Omit for a text-only splash. |
| `title` | Product name, bold, at `titleFontSize`. Also becomes the window title. |
| `version` | Rendered as `Version <value>`, so pass `"1.40"`, not `"Version 1.40"`. Take it from the compile definition the changelog feeds (see [Versioning](../../AGENTS.md#versioning)) — never from a literal. |
| `attributionText` | Caption above the attribution logo, e.g. `"GUI by"`. |
| `attributionImagePath` | Logo of whoever is being credited — for an UltraCanvas host, `media/images/UltraCanvas-logo.png`. |
| `attributionName` | Name under that logo, e.g. `"Ultra Canvas"`. |
| `websiteURL` | Clicking the link opens this in the user's browser. |
| `websiteDisplay` | Link text; defaults to `websiteURL` when empty. |
| `logoSize` | Main logo box, square, in pixels. Default 250. The image is fitted inside it (`ImageFitMode::Contain`), so a non-square logo keeps its aspect ratio. |
| `attributionLogoSize` | Attribution logo box, square, in pixels. Default 90. |
| `titleFontSize`, `versionFontSize`, `attributionFontSize`, `attributionNameFontSize` | The type scale, in points. Defaults 20 / 11 / 13 / 11. Each label's box grows with its font, so raising one of these cannot clip the text — but the window has to be tall enough for the result, so raise `height` with them. |
| `secondaryTextColor` | The two quiet lines — the version and the attributed name. Default is a mid grey; a design that wants them as dark as the title sets it here. |
| `width`, `height` | Window size. Default 400 × 300 — raise the height when using the attribution block. |
| `showTimeout` | Milliseconds before the splash closes itself. `0` means no timeout: the splash then stays until it is clicked or `Close()` is called. |
| `backgroundColor` | Panel background. Default white. |

The three attribution fields are independent: a caption with no logo, or a logo
with no name, are both valid. Leaving all three empty produces the plain
logo/title/version/link splash.

Elements are stacked in this order, centred in a column: logo, title, version,
attribution caption, attribution logo, attribution name, website link.

## API

| Call | Meaning |
|---|---|
| `Show(config, parentWindow)` | Creates and shows the splash. Does nothing if one is already up. `parentWindow` may be `nullptr`; when given, the splash is centred on that window's screen, which keeps it on the same monitor as the application in a multi-monitor setup. |
| `Close()` | Takes the splash down. Safe to call when nothing is showing, and safe after the user already dismissed it. |
| `IsVisible()` | Whether a splash window currently exists. |
| `onSplashClosed` | Fires once the window is gone, however it went — timeout, click or `Close()`. |

## Startup work around the splash

The splash exists to cover slow startup, so put the work where the user cannot
see it and cannot be interrupted by it:

- **Before `Show()`** — anything silent that the first frame should already
  reflect: restoring session documents, reading settings, warming caches.
- **In `onSplashClosed`** — anything that opens a window or wants the focus.
  A modal raised while an always-on-top splash is up competes with it for the
  foreground; waiting for the callback removes the race, and removes the need
  for a "wait a bit then prompt" timer.

`onSplashClosed` does **not** fire when `Show()` could not create the window —
a missing resource, or a headless session. Check `IsVisible()` afterwards and
run the phase-two work inline if the splash never appeared:

```cpp
splash.Show(config, mainWindow.get());
if (!splash.IsVisible()) {
    mainWindow->PromptCrashRecovery();
}
```

## See also

- `Apps/Texter/main.cpp` — the canonical two-phase startup shown above.
- [`Docs/Ladybird/SplashScreen.md`](../Ladybird/SplashScreen.md) — the
  Ladybird port's splash, and the assets it ships.
