# UltraCanvasSystemFont

<!-- doc-check: UltraCanvasApplication* app; -->

**The desktop's interface font, found rather than assumed.** An element
whose `FontStyle::fontFamily` is empty is drawn in the system font, which
is `UltraCanvasApplication::GetSystemFontStyle()`; code, hex and timecodes
take `GetDefaultMonospacedFontStyle()`. Until 0.9.362 both were the literal
Ubuntu 12 on Linux and on Windows, so a Windows desktop's Segoe UI and a
GNOME desktop's Cantarell never reached a window. Each platform now asks
its desktop, and this header holds the parsers of the forms the desktops
write a font in, so a test covers every rule without a desktop.

- Header: `UltraCanvas/include/UltraCanvasSystemFont.h` (header-only, pure)
- Platform code: `OS/Linux/UltraCanvasLinuxSystemFont.cpp`,
  `OS/MSWindows/UltraCanvasWindowsSystemFont.cpp`,
  `OS/MacOS/UltraCanvasMacOSApplication.mm` (`DetectSystemFontStyleNative`)
- Tests: `Tests/SystemFontTest.cpp` (target `SystemFontTest`, no display)

```cpp
FontStyle ui = app->GetSystemFontStyle();                 // Segoe UI 9, Cantarell 11, SF 13 ...
FontStyle code = app->GetDefaultMonospacedFontStyle();    // Consolas, the GNOME monospace font, Menlo ...
```

Sizes are in points, as the text engine takes them. Both are the host
appearance's fonts ([`UltraCanvasHostAppearance.md`](UltraCanvasHostAppearance.md)):
read once, kept, and read again when the desktop says its look changed.

```cpp
FontStyle pinned;
pinned.fontFamily = "Ubuntu";
pinned.fontSize = 12.0;
app->SetSystemFontStyle(pinned);                 // what GetSystemFontStyle() answers from now on
app->SetDefaultMonospacedFontStyle(pinned);      // an empty family or a size of 0 is ignored
```

`SetSystemFontStyle` and `SetDefaultMonospacedFontStyle` pin the answers
instead of the desktop's fonts: for a program that brings its own, or a
test that measures text and must not depend on whatever font the machine
has. The demo's layout audit (`UltraCanvasDemo --audit-layout`) pins the
bundled Ubuntu 12 and Ubuntu Mono 12, so its pages are measured in the
font they were laid out for, whatever the runner's desktop font is.

## What each platform reads

| Platform | Interface font | Monospace font |
|---|---|---|
| Windows | The message font of the non-client metrics (`SPI_GETNONCLIENTMETRICS`, `lfMessageFont`), asked at 96 dpi through `SystemParametersInfoForDpi` where it exists (Windows 10 1607), else at the system dpi and converted to points: Segoe UI 9 on a stock install, whatever the person set under Accessibility | Consolas, at the same size |
| macOS | `[NSFont systemFontOfSize:0]`, 13 pt; the family `Helvetica Neue` when AppKit names a private font Pango cannot resolve | The system monospace (`monospacedSystemFontOfSize:`), which is SF Mono and private, so Menlo at the interface size |
| Linux, BSD | On a KDE session (`XDG_CURRENT_DESKTOP`), `~/.config/kdeglobals` `[General] font=` first; otherwise GSettings `org.gnome.desktop.interface font-name` first, read through GIO (a machine without the schema answers nothing); then the other; then `~/.config/gtk-4.0/settings.ini` and `gtk-3.0/settings.ini` `gtk-font-name`. GSettings is asked only inside a desktop session (`XDG_CURRENT_DESKTOP` or `DESKTOP_SESSION` set): with no desktop running - a bare window manager, an SSH session, a test under Xvfb - the schema would answer its compiled default, Cantarell 11, which nobody chose, so only the files are read | `monospace-font-name` and `[General] fixed=` the same way; the GTK file has no monospace key |
| Everywhere | The bundled Ubuntu at 12 pt when nothing answers | Ubuntu Mono at 12 pt |

## The parsers

```cpp
std::string family;
double points = 0;
bool gtk = ParseGtkFontName("Noto Sans Bold 10.5", family, points);                 // "Noto Sans", 10.5
bool book = ParseGtkFontName("DejaVu Sans Mono Book 9", family, points);            // "DejaVu Sans Mono", 9: Book is a style
bool px = ParseGtkFontName("Sans 12px", family, points);                            // "Sans", 9: pixels become points
bool kde = ParseKdeFontEntry("Noto Sans,10,-1,5,50,0,0,0,0,0", family, points);     // "Noto Sans", 10
std::string value = ReadIniValue("[Settings]\ngtk-font-name=Cantarell 11\n", "Settings", "gtk-font-name");   // "Cantarell 11"
```

`ParseGtkFontName` reads the GTK and Pango form - the family, then any
style words (Bold, Italic, Book, Condensed, ...), then the size, in points
or as `12px` - and leaves its outputs untouched when there is no size.
`ParseKdeFontEntry` reads the `kdeglobals` form, the family then the point
size then fields it ignores. `ReadIniValue` reads a `key=value` under a
`[section]` (any section when the name is empty), comments with `#` or
`;`, quotes around a value removed, so the GTK 2 `.gtkrc-2.0` reads too.
A size is parsed dot-decimal whatever the locale, never through `strtod`:
`Cantarell 1,5` is not a size.

## Related

- [`UltraCanvasFontFile.md`](UltraCanvasFontFile.md) - registering a font the application brings
- [`UltraCanvasEmojiSettings.md`](UltraCanvasEmojiSettings.md) - the emoji font, a setting rather than the desktop's
- [`UltraCanvasHostAppearance.md`](UltraCanvasHostAppearance.md) - the rest of what the host says: dark or light, the accent, high contrast, the text scale, and the change events the fonts follow too
- [`UltraCanvasHostThemeProposal.md`](UltraCanvasHostThemeProposal.md) - the theme built from it
