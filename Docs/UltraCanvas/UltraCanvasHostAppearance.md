# UltraCanvasHostAppearance

<!-- doc-check: UltraCanvasApplication* app; -->

**What the desktop looks like, read from the host.** A desktop is light or
dark, has an accent colour, an interface font at a size, a monospace font,
a text scale, and may ask for high contrast or less motion. `HostAppearance`
is that value; `UltraCanvasApplication::GetHostAppearance()` reads it from
the platform once, keeps it, and reads it again when the host says it
changed, so an application that asks hears about a switch to dark while it
runs. It is phase T1 of
[`UltraCanvasHostThemeProposal.md`](UltraCanvasHostThemeProposal.md): the
reading. The theme built from it (`FromHost`, the policy) is T3, and until
then the value is there for an application's own handler and for the
system fonts, which now come from it.

- Header: `UltraCanvas/include/UltraCanvasHostAppearance.h` (header-only, pure)
- Application: `GetHostAppearance`, `RefreshHostAppearance`, `ScheduleHostAppearanceRefresh`, `AddHostAppearanceHandler` / `RemoveHostAppearanceHandler` (`UltraCanvasApplication.h`)
- Platform code: `OS/MSWindows/UltraCanvasWindowsHostAppearance.cpp`, `OS/MacOS/UltraCanvasMacOSHostAppearance.mm`; the other platforms answer the default with their font detectors' fonts
- Tests: `Tests/HostAppearanceTest.cpp` (target `HostAppearanceTest`, no display)

## Contents

- [The value](#the-value)
- [Reading it](#reading-it)
- [Hearing a change](#hearing-a-change)
- [What each platform reads](#what-each-platform-reads)
- [The override](#the-override)
- [The pure half](#the-pure-half)
- [Related](#related)

## The value

```cpp
const HostAppearance& host = app->GetHostAppearance();
bool dark = host.dark;                         // the desktop prefers a dark look
bool contrast = host.highContrast;             // Windows High Contrast, macOS Increase Contrast
bool calm = host.reduceMotion;                 // the animation preference
std::optional<Color> accent = host.accent;     // the desktop's accent, when it has one
FontStyle ui = host.uiFont;                    // Segoe UI 9, Helvetica Neue 13, Ubuntu 12 ...
FontStyle mono = host.monoFont;                // Consolas, Menlo, Ubuntu Mono ...
double scale = host.textScale;                 // Windows "Text size": 1.0 to 2.25
std::string source = host.source;              // "windows-registry", "macos-appkit", "default", "+override"
```

Font sizes are in points, as the text engine takes them; the text scale is
a factor over them that the theme (T3) will apply. `source` names where
the value came from, for the start-up report (`UltraCanvas: host
appearance dark, accent #0078d4, font Segoe UI 9, mono Consolas 9, from
windows-registry` in the debug log).

`GetSystemFontStyle()` and `GetDefaultMonospacedFontStyle()` are `uiFont`
and `monoFont`, unless `SetSystemFontStyle` / `SetDefaultMonospacedFontStyle`
pinned them ([`UltraCanvasSystemFont.md`](UltraCanvasSystemFont.md)); a pin
outlives a host change.

## Reading it

The first `GetHostAppearance()` asks the platform (`DetectHostAppearanceNative`)
and applies the override; later calls return the kept value. It is read on
the UI thread. `RefreshHostAppearance()` reads it again now and, when the
value differs, runs the handlers. The platforms call
`ScheduleHostAppearanceRefresh()` from their change events, from any
thread: it posts one refresh to the UI thread per burst of events, so the
several `WM_SETTINGCHANGE` a switch to dark sends become one read, and the
handlers run once.

```cpp
app->RefreshHostAppearance();            // read the host again now; handlers when it changed
app->ScheduleHostAppearanceRefresh();    // the same, posted to the UI thread, bursts coalesced
```

## Hearing a change

```cpp
int handler = app->AddHostAppearanceHandler([](const HostAppearance& host) {
    // re-style a hand-painted panel, pick a dark or light image, ...
    (void)host.dark;
});
app->RemoveHostAppearanceHandler(handler);
```

A handler runs on the UI thread after the value changed, with the new
value. Until T3 nothing in the framework acts on the change by itself: the
theme stays what the application set. An application that wants to follow
today installs a handler and calls `UltraCanvasTheme::SetCurrent` with
`Dark()` or `Light()`, as UltraVideo's `ThemeFor` does.

## What each platform reads

| Platform | Dark, contrast, motion | Accent | Fonts and scale | Change event |
|---|---|---|---|---|
| Windows | `HKCU\...\Themes\Personalize\AppsUseLightTheme` (0 is dark; missing is light); `SPI_GETHIGHCONTRAST`; `SPI_GETCLIENTAREAANIMATION` off is "reduce" | `HKCU\...\Explorer\Accent\AccentColorMenu` (the colour Settings shows), else `DwmGetColorizationColor` | the message font and Consolas ([`UltraCanvasSystemFont.md`](UltraCanvasSystemFont.md)); `HKCU\Software\Microsoft\Accessibility\TextScaleFactor` (100 to 225) | `WM_SETTINGCHANGE`, `WM_THEMECHANGED`, `WM_DWMCOLORIZATIONCOLORCHANGED` in the window procedure, every top-level window getting the broadcast and the application reading once |
| macOS | `NSApp.effectiveAppearance` best match of Aqua / DarkAqua (10.14); `NSWorkspace` `accessibilityDisplayShouldIncreaseContrast`, `...ShouldReduceMotion` | `NSColor.controlAccentColor` in sRGB (10.14) | the system font and the system monospace; scale 1.0, macOS scales the display | KVO on `effectiveAppearance`, `NSSystemColorsDidChangeNotification`, `NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification`, installed once `NSApp` exists |
| Linux, BSD, WebAssembly, Android | light (T2 of the proposal brings XSETTINGS, GSettings, `kdeglobals`, the media queries) | none | the platform's font detectors | none yet |
| Everywhere | The `default`: light, no accent, the bundled Ubuntu 12 and Ubuntu Mono 12, when nothing answers | | | |

## The override

`ULTRACANVAS_HOST_APPEARANCE` is applied over what the host said, on every
read - for a dark, light or 14 pt run of an application on any desktop,
and for CI screenshots. Comma-separated, every part optional, in any order;
a part it does not understand is ignored; the source gains `+override`.

```
ULTRACANVAS_HOST_APPEARANCE="dark,font=Noto Sans 14,mono=Fira Code 13,accent=#e95420,scale=1.25,contrast=high,motion=reduce"
```

| Part | Meaning |
|---|---|
| `dark`, `light`, `look=dark`, `look=light` | the look |
| `contrast=high`, `contrast=normal`, `high-contrast` | high contrast |
| `motion=reduce`, `motion=full`, `reduce-motion` | reduced motion |
| `accent=#rrggbb`, `accent=none` | the accent, or none |
| `font=<family> <points>`, `mono=<family> <points>` | the fonts, in the GTK form (`Noto Sans Bold 10.5`: style words dropped) |
| `scale=<factor>` | the text scale, 0.5 to 4 |

## The pure half

The header holds what a test can hold without a desktop: the value, the
default, the conversions of the raw values the Windows backend fetches,
the colour-as-text helpers, the override and the report line.

```cpp
HostAppearance host = DefaultHostAppearance();                                // light, no accent, Ubuntu 12, Ubuntu Mono 12
bool applied = ApplyHostAppearanceOverride(host, "dark,font=Noto Sans 14");   // true; host.dark, host.uiFont Noto Sans 14
std::string line = HostAppearanceReport(host);                                // "dark, no accent, font Noto Sans 14, mono Ubuntu Mono 12, from default+override"
Color accent;
bool ok = ColorFromHex("#0078d4", accent);                                   // true
std::string hex = HexOfColor(accent);                                         // "#0078d4"
bool dark = HostAppearanceWindows::DarkFromAppsUseLightTheme(0);              // true
Color menu = HostAppearanceWindows::AccentFromAbgr(0xFFD47800u);              // #0078d4: the registry's 0xAABBGGRR
Color dwm = HostAppearanceWindows::AccentFromArgb(0xC40078D4u);               // #0078d4: DWM's 0xAARRGGBB
double factor = HostAppearanceWindows::TextScaleFromPercent(125);             // 1.25; outside 100-225 gives 1.0
```

## Related

- [`UltraCanvasHostThemeProposal.md`](UltraCanvasHostThemeProposal.md) - the whole: the theme from the host (T3), every element following (T4), the setting and UOS-Settings (T5)
- [`UltraCanvasSystemFont.md`](UltraCanvasSystemFont.md) - the font detectors the value's fonts come from, and the pins
- [`UltraCanvasTheme.md`](UltraCanvasTheme.md) - the colour roles a handler sets today
