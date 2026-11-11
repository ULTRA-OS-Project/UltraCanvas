# UltraCanvasHostAppearance — Following the Host's Look Proposal

Status: **T1, T2 and T3 implemented** (T1, 2026-11-04: `HostAppearance`, the
Windows and macOS backends with their change events, the system fonts
served from it, `ULTRACANVAS_HOST_APPEARANCE`, `HostAppearanceTest`; T2,
2026-11-06: the Linux backend - the portal, GSettings, XSETTINGS with its
`PropertyNotify`, `kdeglobals` and `settings.ini`, the config folders
watched - for X11 and Wayland, and WebAssembly's media queries -
[`UltraCanvasHostAppearance.md`](UltraCanvasHostAppearance.md); T3,
2026-11-06: `ThemeFonts` and the text roles, `FromHost` with the accent
derivation and the two high-contrast presets, the theme policy in
`Initialize` and on every host change, a window's ground from the theme -
[`UltraCanvasTheme.md`](UltraCanvasTheme.md)); **T4 implemented**
(2026-11-10, six pull requests: following by default under the policy
(`SetFollowThemeByDefault`), the `SetStyle` rule, fonts by role in the
existing readers, then `FromTheme` and `ApplyTheme` for every style
struct that lacked them - the input elements, the lists, the containers
and pickers, the rest of the panel elements, the media elements and the
viewers - with element roles for the colours no shared role fits);
**T5 implemented** (2026-11-10: `Appearance.conf` through
`AppearanceSettings`, applied over the host on every read; the settings
watcher owned by the application base, native or a five-second poll;
UOS-Settings > Appearance > Look with a preview strip; DemoApp's theme
page); **T6 in progress** (the four `Own` applications on the policy).
The
companion document
[`UltraCanvasLocalisationProposal.md`](UltraCanvasLocalisationProposal.md)
covers the language half of the same job and shares §6 (the settings file
and the watcher) with this one.

Author: UltraCanvas Framework
Last Modified: 2026-11-10

---

## 1. Where we are

The colour half of a theme exists and works; what it lacks is a source
and a reach. Counted on 2026-10-21:

| Piece | State |
|---|---|
| `UltraCanvasTheme` (`UltraCanvasTheme.md`) | 17 colour roles, `Light()` and `Dark()` presets whose contrast `UltraCanvasThemeTest` measures, `Current()` / `SetCurrent()`, change handlers; `Initialize()` installs the handler that walks every window |
| Elements that follow it (`FromTheme` on the style, `ApplyTheme` on the element) | 27 headers: button, label, dropdown, slider, switch, text input, text area, toolbar, split pane, tabbed container, menu, tooltip, modal dialog, album, video timeline, timecode field, and the diagram elements |
| Elements with a style struct and **no** `FromTheme` | 42 headers: checkbox, radio, list view, tree view, columns tree view, scrollbar, container, group box, segmented control, spinner, stepper, rating, chip, badge, breadcrumb, pagination, date and time picker, colour picker, swatch bar, palette manager, filer widget, rich text edit, auto-complete, busy indicator, curve editor, font viewer, slideshow, the media elements, ... |
| Applications that set a theme | 2 of 30: UltraVideo (its design tokens → `ThemeFor`) and UltraPress (brand colours over the presets, a `look` file of its own). Texter has a dark mode of its own (`ApplyDarkTheme`) that predates the theme |
| Fonts | `FontStyle.fontFamily` empty means "the system font", resolved through `UltraCanvasApplication::GetSystemFontStyle()` → `DetectSystemFontStyleNative()`. **Linux and Windows return the literal `Ubuntu` at 12 pt**; macOS reads `[NSFont systemFontOfSize:0]` and then falls back to `Helvetica Neue` because Pango cannot resolve `.AppleSystemUIFont`. Eight files call it; the rest write `"Arial"` (73 sites), `"Sans"` (22), `"Ubuntu"` (11). Style structs default to `fontSize = 12` (9 structs) or `11` (6) |
| Reading the host's look | **T1 (2026-11-04):** `GetHostAppearance()` on Windows (the registry, the metrics, DWM) and macOS (AppKit); the fonts on every platform through the detectors of 0.9.362 (`UltraCanvasSystemFont.md`). **T2 (2026-11-06):** Linux and the BSDs read the portal, GSettings, XSETTINGS, `kdeglobals` and `settings.ini`; WebAssembly its media queries. Android still answers light with its fonts |
| Hearing that it changed | **T1:** the Windows window handles `WM_SETTINGCHANGE`, `WM_THEMECHANGED` and `WM_DWMCOLORIZATIONCOLORCHANGED`; the macOS application observes `effectiveAppearance`, the system colours and the accessibility options; each posts one read per burst and the handlers hear a difference. **T2:** the X11 loop hears the XSETTINGS manager's `PropertyNotify`, both Linux backends watch the config folders, WebAssembly listens to its media queries' `change` |
| Per-window DPI | Done: `backingScaleFactor` on macOS, `GetDpiForWindow()` on Windows, the X11 scale on Linux. Font sizes are logical points; this proposal keeps them so |

So a person on a dark GNOME, a dark Windows 11 or a dark macOS opens
UltraFiler and gets a white window in Ubuntu 12 pt, with the menu and the
tooltip - which do follow the theme - in the framework's dark greys only
if the application happened to ask for them.

## 2. Goal and scope

1. **Every application looks like the desktop it runs on** - its light or
   dark look, its accent colour, its UI font and size, its text scaling
   and high-contrast mode - without the application doing anything.
2. **It follows live**: switching the desktop to dark re-styles every
   open window, as the theme's change handler already allows.
3. **A person can overrule it** once for all UltraCanvas applications, in
   UOS-Settings: light, dark or system; the system's accent or one of
   their own; the system's font or one of their own, at a size.
4. **The existing applications migrate by one call and a review of their
   hand-painted panels**, not by restyling every element.
5. **The design apps keep their designs**: UltraVideo's tokens and
   UltraPress's brand stay; they take the host's dark/light flag and
   font, not its colours, unless they ask for more.

Out of scope: native widgets (the framework draws its own and keeps
doing so - compliance is in colours, fonts and sizes, not in borrowing
`NSButton`), icon theme following (the desktop-entry module has that for
file icons already), and the window frame, which the window manager
draws.

## 3. `UltraCanvasHostAppearance` - reading the host

A value type, a detector on the application, and an event. Header
`include/UltraCanvasHostAppearance.h`, platform code in
`OS/<Platform>/UltraCanvas<Platform>HostAppearance.{cpp,mm}`, the pure
parsers in `core/UltraCanvasHostAppearance.cpp` where a test reaches them
without a display.

```cpp
namespace UltraCanvas {

struct HostAppearance {
    bool dark = false;              // the desktop prefers a dark look
    bool highContrast = false;      // Windows High Contrast, GNOME high-contrast, macOS Increase Contrast
    bool reduceMotion = false;      // the animation preference, for the elements that animate
    std::optional<Color> accent;    // the desktop's accent, when it has one
    FontStyle uiFont;               // family and size in points: Segoe UI 9, SF 13, Cantarell 11, Noto Sans 10
    FontStyle monoFont;             // Consolas, SF Mono / Menlo, the GNOME monospace font
    double textScale = 1.0;         // Windows "Text size", GNOME text-scaling-factor, Android fontScale
    std::string source;             // "windows-registry", "macos-appkit", "xsettings", "gsettings", "kdeglobals", "wasm-media-query", "default": for a start-up report
};

class UltraCanvasApplicationBase {
public:
    // What the host says now (cached; re-read on the change event).
    const HostAppearance& GetHostAppearance();
    // Called on the UI thread when the host's look changed, after the
    // cache is refreshed and the theme policy (§4) has re-applied.
    using HostAppearanceHandler = std::function<void(const HostAppearance&)>;
    int AddHostAppearanceHandler(HostAppearanceHandler handler);
    void RemoveHostAppearanceHandler(int id);
protected:
    virtual HostAppearance DetectHostAppearanceNative() = 0;   // replaces DetectSystemFontStyleNative / DetectMonospacedFontStyleNative
};

}
```

`GetSystemFontStyle()` and `GetDefaultMonospacedFontStyle()` stay as the
accessors of `uiFont` and `monoFont`, so the eight callers and the Cairo
"empty family" path need no change and stop getting `Ubuntu`.

### The backends

| Platform | Dark / contrast | Accent | Fonts and scale | Change event |
|---|---|---|---|---|
| **Windows** | `HKCU\...\Themes\Personalize\AppsUseLightTheme` (`RegGetValueW`); `SystemParametersInfoW` with `SPI_GETHIGHCONTRAST` | `HKCU\...\Explorer\Accent\AccentColorMenu` (the colour Settings shows), else `DwmGetColorizationColor` | `SPI_GETNONCLIENTMETRICS` → `lfMessageFont` (Segoe UI, height at the window's DPI → points); `HKCU\Software\Microsoft\Accessibility\TextScaleFactor` (100-225); monospace `Consolas` | `WM_SETTINGCHANGE` with `lParam` `"ImmersiveColorSet"` (dark/accent), `WM_THEMECHANGED`, `WM_SETTINGCHANGE` for `SPI_SETNONCLIENTMETRICS` (font) - three cases in the existing `WndProc`, posted to the application once per burst |
| **macOS** | `NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:` (`NSAppearanceNameDarkAqua`); `NSWorkspace.accessibilityDisplayShouldIncreaseContrast`, `...ShouldReduceMotion` | `NSColor.controlAccentColor` in sRGB | `[NSFont systemFontOfSize:0]` size (13 pt); family: `SF Pro Text` when CoreText resolves it for Pango, else the present `Helvetica Neue`; mono `[NSFont monospacedSystemFontOfSize:]` → `SF Mono`, else `Menlo` | KVO on `NSApp.effectiveAppearance`; `NSSystemColorsDidChangeNotification` (accent); `NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification` |
| **Linux / BSD, X11** | **XSETTINGS** first (the `_XSETTINGS_S0` manager selection's property, decoded in core: GNOME, XFCE, MATE, Cinnamon and `xsettingsd` all serve it): `Net/ThemeName` ending in `-dark`, `Gtk/ApplicationPreferDarkTheme`; then **GSettings** `org.gnome.desktop.interface color-scheme` (`prefer-dark`), `gtk-theme`, `accent-color`, `font-name`, `monospace-font-name`, `text-scaling-factor` through GIO, which the Linux core already links with GTK 3; then **kdeglobals** `[General] ColorScheme`, `[Colors:Window] BackgroundNormal` (dark when its luminance is under 0.5), `[General] AccentColor`, `font`, `fixed`; then `gtk-3.0/settings.ini` `gtk-application-prefer-dark-theme`, `gtk-font-name`, `gtk-theme-name`, with `DetectIconTheme`'s reader; then the **portal** `org.freedesktop.portal.Settings` `color-scheme` over D-Bus where UltraMessage's bridge already connects to it | as left | as left; `Xft/DPI` is not read (the window's scale is) | XSETTINGS: `PropertyNotify` on the manager window, one `XSelectInput` in the application loop next to the clipboard's; GSettings: the `changed` signal, pumped by the `g_main_context_iteration` the GTK dialogs already run, else a 2 s poll of the mtime of `~/.config/dconf/user`; kdeglobals and settings.ini: `UltraCanvasFolderWatcher` (inotify) on `~/.config` |
| **WebAssembly** | `matchMedia("(prefers-color-scheme: dark)")`, `(prefers-contrast: more)`, `(prefers-reduced-motion)` through `EM_ASM` | none (CSS has no accent) | `system-ui` at 16 px → 12 pt; `ui-monospace` | the media query's `change` event, posted into the loop |
| **Android** | `Configuration.uiMode & UI_MODE_NIGHT_MASK` over the JNI bridge | Material You's `android.R.color.system_accent1_500` (API 31) | `Configuration.fontScale`; family `Roboto` / `sans-serif` | `onConfigurationChanged` |
| **ULTRA OS** | the desktop's own setting (§6) is the source; there is no host above it | | | the settings-folder watcher |

Every reader is pure where it can be: a function from a registry value, an
XSETTINGS byte buffer, an `.ini` text or a `kdeglobals` text to a
`HostAppearance` field, so `Tests/HostAppearanceTest.cpp` runs on the CI
machines with no desktop at all. When nothing answers, the result is the
`default` source: light, no accent, `Ubuntu` 12 and `Ubuntu Mono` 12 - what
every platform returns today, so a platform without a backend changes
nothing.

## 4. From the host to the theme: fonts in the theme, the policy

### Fonts are theme

`UltraCanvasTheme` gains the font half of a look:

```cpp
struct ThemeFonts {
    FontStyle ui;        // the body font: family and base size in points
    FontStyle mono;      // for code, hex, timecodes, the terminal
    double scale = 1.0;  // the host's text scale, over the per-window DPI
};

class UltraCanvasTheme {
public:
    ThemeFonts fonts;
    // The size a text role gets, from the base size: Body 1.0, Caption
    // 0.92 (the 11 pt of a 12 pt base), Small 0.85, Heading 1.25, Title 1.5
    double FontSize(ThemeTextRole role) const;
    FontStyle Font(ThemeTextRole role) const;      // ui at that size
};
```

Every `FromTheme` that today copies the roles' colours onto its style
also sets `fontFamily` and `fontSize` from `theme.Font(...)` - by the
role its text plays: a button and a label are `Body`, a tooltip, a badge
and a ruler label `Caption`, a dialog title `Heading`. The style struct's
own literal (`fontSize = 12.0f`) stays as the default for an element
styled by hand, as the colour defaults do. That retires the `Arial` and
`Sans` literals from the elements over time - each one becomes
`theme.Font(role)` as its element gains `FromTheme` (§5).

### The theme from the host

```cpp
class UltraCanvasTheme {
public:
    // Light() or Dark() by `host.dark`, the high-contrast variant of
    // either when `host.highContrast`, the accent roles from
    // `host.accent` (§4's derivation), the fonts from the host's.
    static UltraCanvasTheme FromHost(const HostAppearance& host);
};
```

An accent from the host is one colour; the theme needs five
(`accent`, `accentHover`, `accentSoft`, `onAccent`, `focus`). Derivation,
with the contrast rules `UltraCanvasThemeTest` already enforces:
`onAccent` is white or the dark ink, whichever reaches 4.5:1 on the
accent (the test refuses a theme where neither does, and the accent is
then darkened or lightened in L\* until one does); `accentHover` is the
accent moved 8 % toward the ink; `accentSoft` the accent at 18 % over
`panel` (dark) or 14 % (light); `focus` the accent itself unless it fails
3:1 on `ground`, then `lineStrong`. The two high-contrast presets,
`LightHighContrast()` and `DarkHighContrast()`, are black on white and
white on black with the system accent, every ink at 7:1.

### The policy

```cpp
enum class ThemePolicy {
    FollowHost,   // the default: FromHost(GetHostAppearance()), re-applied on every change
    Light,        // the framework's Light(), the host's fonts
    Dark,         // Dark(), the host's fonts
    Own           // the application set a theme of its own; the host's fonts and dark flag are offered through the handler, not applied
};

class UltraCanvasApplicationBase {
public:
    void SetThemePolicy(ThemePolicy policy);
    // For Own: a function from the host to the application's theme, called
    // at start and on every change - UltraVideo's ThemeFor(dark) is one.
    void SetThemeProvider(std::function<UltraCanvasTheme(const HostAppearance&)> provider);
};
```

`Initialize()` reads the host, reads `Appearance.conf` (§6, a person's
override of look, accent, font and size), builds the theme and calls
`SetCurrent` before the first window. On a host change or a change to the
file it builds it again and calls `SetCurrent`: every element that
follows re-styles, every window redraws, the application's own handler
runs for its hand-painted panels. Nothing in this is new machinery - the
walk, `OnThemeChanged` and `ApplyTheme` exist; what is new is that
something calls `SetCurrent` with a theme the host chose.

The window's own clear colour (`Colors::WindowBackground` today) becomes
`roles.ground` when the window's application follows the host, so a dark
theme is not a dark panel on a white window.

## 5. Reaching every element, and the applications

### Elements

Two changes that together make "follow the host" mean the whole window:

1. **`FromTheme` for the 42 style structs that lack one** - the input
   elements (checkbox, radio, segmented control, spinner, stepper, rating,
   date and time picker), the lists (list view, tree view, columns tree,
   filer widget, scrollbar), the containers (container, group box,
   breadcrumb, pagination), the pickers (colour picker, swatch bar, palette
   manager), the rich text edit, the viewers. Each is the pattern the 27
   already follow: the roles mapped onto the struct's fields over a base
   style, `ApplyTheme` on the element, a row in `UltraCanvasTheme.md`'s
   reader list, and the doc's snippet checked by `check_doc_examples.py`.
   Where an element has colours of its own (the tree's guide lines, the
   colour picker's checkerboard) they become named element roles with a
   derived fallback, as the timeline's did.
2. **Following by default, per application.** `SetFollowTheme` is per
   element and off by default, so an application today opts every element
   in one by one - UltraVideo's `FollowTheme` helpers (a label or a button with its variant) show
   what that looks like at scale. A new application-wide switch,
   `UltraCanvasUIElement::SetFollowThemeByDefault(bool)`, makes every
   element built afterwards start with `followTheme = true` (and so styled
   from the current theme in its constructor's tail, after its default
   style is set). `Initialize()` sets it when the policy is `FollowHost`,
   `Light` or `Dark`; an `Own` application sets it itself, as UltraVideo
   would. An element the application styles by hand after construction
   keeps its look, as today: `SetStyle` turns `followTheme` off for that
   element unless the application asks for both (`SetStyle` then
   `SetFollowTheme(true)`, which `ApplyTheme` honours by putting the roles
   over the given style, as it already does).

### What an existing application does

For 26 of the 30 applications - everything but UltraVideo, UltraPress,
Texter and DemoApp - the migration is:

1. Nothing for the policy: `FollowHost` is the default and elements follow
   by default.
2. Review the hand-painted surfaces: every `ctx->FillRectangle(...,
   Colors::White)` or `Color(245, 245, 245)` behind a panel, every
   `Colors::TextDefault` for a caption, becomes `UltraCanvasTheme::Current().roles.panel`
   / `.ink` and the application installs a theme change handler that
   redraws. `scripts/check_ui_reuse.py` already knows where applications
   paint; a one-off report from it lists the sites per application.
3. Run the application dark and light, and with a 14 pt host font (the
   `ULTRACANVAS_HOST_APPEARANCE=dark,font=Noto Sans 14` environment
   override that §3's `default` backend honours, for exactly this and for
   CI screenshots), and fix what does not fit - a fixed-height toolbar
   whose label is now clipped, a dialog whose buttons were measured for
   12 pt. Because the elements measure their text from their font, most
   of this is already right; the fixed `height = 28` literals in layout
   code are what breaks, and `FormLayout` (which measures) does not.

UltraVideo and UltraPress set `ThemePolicy::Own` with their `ThemeFor` as
the provider, so a dark desktop gives their dark palette and their
light palette follows a light one, and their fonts come from the host.
Texter retires `ApplyDarkTheme` / `ApplyLightTheme` in favour of the
policy and its menu's own View > Theme becomes a writer of
`Appearance.conf`'s `look=` (so a person's choice in Texter is a choice
for every application, as the settings page is). DemoApp gets a theme
page: policy, the host's values as detected, and a live switch.

## 6. The setting, UOS-Settings and the watcher

`Appearance.conf` in the UltraCanvas settings folder, the `Emoji.conf`
pattern (`AppearanceSettings::Load/Save/FilePath/Parse/Serialize`, written
beside and renamed over), every line optional, a missing line meaning
"the system's":

```
look=system          system | light | dark
accent=system        system | #rrggbb
font=system          system | <family>
fontsize=system      system | <points>
scale=1.0            over the host's text scale
contrast=system      system | high
```

UOS-Settings > Appearance gains a **Look** page beside Emoji: the look
(three radio buttons), the accent (system, or the colour picker), the
font (system, or `GetInstalledFontFamilies()` in a dropdown with the size
beside it), and a preview strip of a button, a field, a label and a menu
drawn through the theme, which is itself an `UltraCanvasContainer` that
follows the theme - the page shows the result by being it.

**The settings-folder watcher.** Today a shared setting reaches an
application when it next starts. One `UltraCanvasFolderWatcher` on the
UltraCanvas settings folder, owned by the application base
(`UltraCanvasSettingsWatcher`: `Watch(fileName, handler)`, handlers run
on the UI thread, bursts coalesced), turns `Appearance.conf`,
`Language.conf` and `Emoji.conf` into live settings on Linux, BSD and
Windows, where the watcher has a backend; on macOS, where it polls, a
five-second mtime poll of three files is nothing. The appearance handler
rebuilds the theme as a host change does; the language handler is the
localisation proposal's §7; the emoji handler re-runs
`ApplyEmojiSettings` and `RefreshAfterFontChange`.

On ULTRA OS the desktop has no host above it, so `Appearance.conf` *is*
the host: UOS-Settings writes it, every application follows it through
the watcher, and `UltraDesktop` reads it for its own panels. Nothing is
special-cased: the `default` backend answers "no host", and the file
decides, as it does on any platform where a person set `look=dark`.

## 7. The phases

| Phase | What | Effort |
|---|---|---|
| **T1** | **Done** (`UltraCanvasHostAppearance.md`): `HostAppearance`, the pure readers and `HostAppearanceTest`; the Windows (registry, metrics, `WM_SETTINGCHANGE`) and macOS (AppKit, KVO) backends; `GetSystemFontStyle` served from it; the `ULTRACANVAS_HOST_APPEARANCE` override. The two font detectors stay as the fonts' source under `DetectHostAppearanceNative`, so the platforms without a backend are untouched | 4 days |
| **T2** | **Done**: the Linux backend - the portal's colour scheme over the session bus, the XSETTINGS decoder and the manager's `PropertyNotify`, GSettings through GIO, `kdeglobals` and `settings.ini` readers, the config folders watched with `UltraCanvasFolderWatcher` - shared by X11 and Wayland (the BSDs with them); WebAssembly's media queries and their change events; `LinuxHostAppearanceTest`. GSettings' `changed` signal is not pumped: dconf's file under `~/.config/dconf` is watched instead, which covers the portal on GNOME too | 3-4 days |
| **T3** | **Done**: `ThemeFonts`, `ThemeTextRole`, `FromHost` with the accent derivation and the two high-contrast presets, `ThemePolicy` / `SetThemeProvider` in `Initialize()` and on every host change, the window clear colour (a window built with the default background takes the policy's ground), `UltraCanvasThemeTest` extended (accent derivation contrast over fourteen accents, high contrast at 7:1, the roles' sizes); `UltraCanvasTheme.md`. `Appearance.conf` stays T5. An application that sets its own theme is left alone by the policy, so the four that do need no change yet | 3 days |
| **T4** | **Done** (2026-11-10): `SetFollowThemeByDefault` set by the policy, every element with an `ApplyTheme` taking the theme at the end of its constructor; the `SetStyle` rule (a style set by hand stops the following, `SetStyle` then `SetFollowTheme(true)` keeps both); fonts by role in the existing readers (`Body` for the button, label, dropdown, text input, menu, switch; `Heading` for a heading label; `Caption` for the tooltip, the slider's value, the dialog's details, the tab titles; the families only where the sizes are a design's pixels); the input elements (`CheckboxVisualStyle`, `RadioVisualStyle`, `SegmentedControlStyle`, `SpinnerVisualStyle`, `StepperStyle`, `RatingStyle`, `CalendarStyle`, `DatePickerStyle`, `TimePickerStyle`) with the element roles `checkbox.mark`, `radio.dot`, `rating.on` / `rating.hover` / `rating.border`, `stepper.on-error`, `calendar.weekend`, `time-picker.clock-face`; the lists (`ScrollbarStyle`, `ListViewStyle`, `TreeColumnStyle`, `FilerStyle`, and `ApplyTheme` on the tree view, which has no struct) with `scrollbar.track` / `scrollbar.thumb`, `columns-tree.group-header` / `columns-tree.on-group-header` and the filer's overlay roles; the containers and pickers (`ContainerStyle` for the scrollbars, `GroupBoxVisualStyle`, `BreadcrumbStyle`, `PaginationStyle`, `ColorPickerStyle`, `ColorSwatchBarStyle`, `ColorPaletteManagerStyle`, `RichTextEditStyle`) with `color-picker.marker` / `color-picker.marker-outline` and `rich-text.page`; the rest of the panel elements (`BadgeStyle` with `primaryColor`, `CardStyle`, `ChipStyle`, `TagInputStyle`, `CollapsiblePanelStyle`, `SidebarStyle`, `BusyIndicatorStyle`, `AutoCompleteStyle`, `NewDocumentDialogStyle`, `ReferencePointPickerStyle`, and `SpellCheckStyle` and `ClipboardRowStyle` for their hosts) with `busy-indicator.second-arc`; the media elements and the viewers (`AudioPlayerStyle`, `AudioRecorderStyle`, `VideoPlayerStyle`, `VideoRecorderStyle`, `VideoTileStyle`, `SlideshowConfig` with `SlideshowIndicatorStyle`, `FontViewerStyle`, `CurveEditorStyle`) with `audio-recorder.pulse`, the players' and the tile's stage and bar roles, `slideshow.stage` and `curve-editor.diagonal`. Every component doc has a *Following the theme* passage | 8-10 days (the bulk; splits by element group into 4-5 PRs) |
| **T5** | **Done**: `AppearanceSettings` (`Appearance.conf`: `Parse` / `Serialize` / `ApplyTo` / `Load` / `Save`, applied in `ReadHostAppearance` before the override), `UltraCanvasSettingsWatcher` (one folder watcher on the settings folder, handlers on the UI thread once per burst, a five-second poll where the build has no backend; the application base starts it in `Initialize`), UOS-Settings > Appearance > Look (look, accent, font and size, text scale, contrast; a preview strip styled from the theme the choices give), DemoApp's Modules > Theme page; `HostAppearanceTest` and `SettingsWatcherTest`. `Emoji.conf` and `Language.conf` are not on the watcher yet: `ApplyEmojiSettings` is once per configuration, and the language handler is the localisation proposal's §7 | 3 days |
| **T6** | **In progress** - done: the four applications with a theme of their own (UltraVideo, UltraPress, ChatCube, UltraDBAdmin) on `ThemePolicy::Own` with their `ThemeFor` / `AppTheme` as the provider and the host's fonts through `UltraCanvasTheme::TakeHostFonts` (UltraPress and ChatCube with `system` as their look's default, UltraVideo's palette following the desktop until the eye picks the project's). Texter's dark mode retired onto the policy: the look is the theme's flag (a change handler repaints), View > Theme writes `Appearance.conf`'s `look=`, `--dark` is `ThemePolicy::Dark` for the run, the `darkTheme` setting is gone. UltraFiler's hand-painted surfaces onto the roles (`UltraFilerLook.h`, `ApplyLook` on a theme change, the text size the theme's body size; run light, dark and at 14 pt). Left: the hand-painted-surface review and the dark/light/14 pt run of the other applications, one PR each, paired with their L5 string pass | ½ day each, ~15 days for 30 |

T1 and T3 together already give Windows and macOS users a dark window in
their own font in the 27 elements that follow, which is the demo for the
rest; T2 brings Linux level; T4 is what makes a whole window follow; T5
and T6 are the user-facing finish.

## 8. Decisions and alternatives considered

- **Native widgets** - rejected; see scope. The framework's elements are
  the product; compliance is their colours, fonts and sizes.
- **Read GTK's theme CSS for the exact colours** (Adwaita's
  `@theme_bg_color`): fragile across GTK versions and themes, and the
  framework's two presets plus the host's accent already read as native
  on every desktop tested by eye. If a desktop wants more, `Own` with a
  provider is the door.
- **Follow the host per window** rather than per process: a process has
  one theme today (`Current()` is static) and the host has one look; a
  per-window theme is a bigger change with no desktop that wants it.
- **Make `SetFollowTheme(true)` the element default outright**, with no
  application switch: UltraVideo and UltraPress style by hand in places
  and expect those elements to keep their look; an application-wide
  switch that `Initialize()` sets by policy gives the same result for
  everyone else with no surprise for them.
- **Pixel font sizes**: the host's font is in points and the window's DPI
  is already handled per window; a theme in pixels would have to know
  the window. Points, scaled by the host's text scale, then by the
  window - the order the platforms themselves use.

## 9. Related

- `UltraCanvasTheme.md` - the roles, the readers, the contrast rules this builds on.
- `UltraCanvasLocalisationProposal.md` - the language half, the settings watcher's second user, and the scripts a CJK or Thai language needs the host's font for.
- `UltraCanvasEmojiSettings.md` - the shared-setting pattern.
- `UltraCanvasDesktopEntry.md` - the GTK/KDE settings readers lifted into the Linux backend.
- `UltraCanvasFolderWatcher.md` - the watcher under the settings watcher.
- `Apps/UltraVideo/UltraVideoTheme.h`, `Apps/UltraPress/ui/UltraPressTheme.cpp` - the two `Own` providers.
