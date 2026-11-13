# UltraCanvas Theme

<!-- doc-check: std::shared_ptr<UltraCanvasVideoTimeline> timeline; std::shared_ptr<UltraCanvasTimecodeField> field; std::shared_ptr<UltraCanvasButton> button; std::shared_ptr<UltraCanvasTextInput> input; std::shared_ptr<UltraCanvasMenu> menu; std::shared_ptr<UltraCanvasContainer> panel; -->

`UltraCanvasTheme` — the framework's **colour roles**: the object a window's
light or dark look lives in, which an element reads when its style is
built, so an application fills the roles from its own design once and every
element that reads the theme follows. A role is named for what it is for
(the panel, the ink, the accent, the focus ring), never for what it looks
like.

**Namespace:** `UltraCanvas`
**Header:** `include/UltraCanvasTheme.h`
**Source:** `core/UltraCanvasTheme.cpp`
**Readers:** `FromTheme` on `ButtonStyle`, `LabelStyle`, `DropdownStyle`,
`SliderVisualStyle`, `SwitchVisualStyle`, `TextInputStyle`,
`ToolbarAppearance`, `SplitPaneStyle`, `AlbumConfig`, `MenuStyle`,
`TooltipStyle`, `VideoTimelineStyle`, `TimecodeFieldStyle`; `ApplyTheme` on
those elements, the tabbed container and the text area
(`include/UltraCanvasUIElement.h`)
**Tests:** `Tests/UltraCanvasThemeTest.cpp` (without a display)
**Design:** `Docs/Research/UltraVideoDesignProposal.md` §5.8
**Version:** 1.15.0 - TakeHostFonts for an Own provider; the four Own applications on the policy (T6)
**Last Modified:** 2026-11-11
**Author:** UltraCanvas Framework

## Contents

- [Status](#status)
- [The roles](#the-roles)
  - [Readability](#readability)
- [Using it](#using-it)
  - [Following the theme](#following-the-theme)
- [Fonts and text roles](#fonts-and-text-roles)
- [The theme from the host](#the-theme-from-the-host)
- [The policy](#the-policy)
- [The setting](#the-setting)
- [Related](#related)

## Status

The theme, its two presets, the current theme and its change handlers are
in place. Every panel element reads it: the button, label, dropdown,
slider, switch, text input, toolbar, split pane and album, and the input
elements - checkbox, radio, segmented control, spinner, stepper, rating,
the calendar view and the date, date range and time pickers with the
clock view - the lists - the scrollbar, list view, columns tree view and
filer widget - and the containers and pickers - the container's
scrollbars, group box, breadcrumb, pagination, the colour picker, swatch
bar and palette manager, the rich text edit - and the badge, card, chip
and tag input, collapsible panel, sidebar, busy indicator, auto-complete,
new-document dialog and reference point picker - and the media elements
and viewers - the audio and video players and recorders, the video tile,
the slideshow, the font viewer and the curve editor - through a
`FromTheme` on their style struct and an `ApplyTheme` on the element; the tabbed
container, the text area and the tree view through `ApplyTheme` alone;
the spell check's marks and the clipboard history's rows through a
`FromTheme` their host applies; the menu
and the tooltip through `MenuStyle::FromTheme` and `TooltipStyle::FromTheme`,
which put the roles over their own `Dark()` and `Light()` presets, so a
dark menu or tooltip agrees with the rest of a dark window. Under the
policy (`FollowHost`, the default, `Light` or `Dark`) every new element
follows the theme: it is styled from the current theme as it is built and
again on every change. Styled by hand (`SetStyle`) it keeps its look, as
before. The readers set their fonts by role as well as their colours.
Every style struct with colours now reads the theme: T4 of the proposal is
complete.

## The roles

```cpp
struct ThemeRoles {
    Color ground, panel, well, raised, hover, pressed;   // surfaces, back to front
    Color line, lineStrong;                               // hairlines; control borders
    Color ink, inkMuted, inkFaint;                        // text and icons
    Color accent, accentHover, accentSoft, onAccent;      // the one accent
    Color focus, danger;                                  // the focus ring; errors
};
```

| Role | What it is for |
|---|---|
| `ground` | The window ground and the gutters between panels |
| `panel` | A panel's surface: a bin, an inspector, a timeline body |
| `well` | An inset surface: a field, a lane |
| `raised` | A raised surface: headers, a ruler, a menu, a button at rest |
| `hover`, `pressed` | The fills of a row or control under the pointer and pressed |
| `line`, `lineStrong` | Hairline dividers; control borders, grips and strong ticks |
| `ink`, `inkMuted`, `inkFaint` | Primary text and icons; secondary text; placeholders, ruler labels, disabled text |
| `accent`, `accentHover`, `accentSoft`, `onAccent` | Primary fills and selection rings; the accent under the pointer; the tint behind a selected row; text on the accent |
| `focus` | The keyboard focus ring, snap lines, drop outlines |
| `danger` | Destructive actions, errors, an invalid entry's border |

An element with colours of its own beyond these reads them as **element
roles** by name, with a fallback it derives from the shared roles or from
its light or dark palette, so a theme that does not name them still looks
right. The video timeline's are `video-timeline.playhead`,
`video-timeline.clip-video`, `video-timeline.clip-video-edge` and the same
for `image`, `card`, `title` and `audio`, `video-timeline.waveform`,
`video-timeline.transition-fill`, `video-timeline.on-transition`,
`video-timeline.beat`, `video-timeline.trim-overflow`,
`video-timeline.drop-soft`, `video-timeline.range-fill`,
`video-timeline.muted-hatch` and `video-timeline.scrollbar`. The slider's
are `slider.handle` and `slider.handle-border` (the raised surface and the
control border otherwise), the switch's `switch.thumb` and
`switch.thumb-border`: a design whose handle is ink or whose thumb is the
text-on-accent colour names them and the controls follow the theme too.
The button's variants (`ButtonVariant`: `Standard`, `Ghost`, `Primary`,
`Selected`, set on the element with `SetThemeVariant`) read
`button.ghost-hover`, `button.ghost-pressed`, `button.primary`,
`button.primary-hover`, `button.on-primary`, `button.selected` and
`button.on-selected`, the shared hover, pressed, accent, accent-hover,
on-accent and soft-accent roles otherwise. The label's variants
(`LabelVariant`: `Body`, `Muted`, `Faint`, `Heading`) read `label.muted`,
`label.faint` and `label.heading`, the shared ink roles otherwise. A modal
dialog follows the theme too (`ModalDialogStyle::FromTheme`, the dialog's
`ApplyTheme`): the panel role behind its sections, ink for the message,
its footer buttons by their role.

### Readability

Both presets keep these contrast ratios (WCAG 2 relative luminance), and
`UltraCanvasThemeTest` measures them so they cannot slip:

| Ink | Reaches | On |
|---|---|---|
| `ink` | 4.5:1 | every surface |
| `inkMuted` | 4.5:1 | `ground`, `panel`, `well`, `raised`, `hover` |
| `inkFaint` | 4.5:1 | `panel`, `well`, `raised` - where ruler labels, placeholders and disabled text sit |
| `lineStrong` | 3:1 | `panel`, `raised` - a control border, a grip |
| `focus` | 3:1 | every surface |
| `danger` | 4.5:1 | `panel` |
| `onAccent` | 4.5:1 | `accent` (`Light`'s is the Windows blue on white, 4.4995) |

`Dark`'s greys came from the dark menu, tooltip and text area and hold
these as they are: the faint ink is 4.7:1 on `raised` and 5.6:1 on
`panel`. `Light` differs from the elements' defaults in one colour:
`lineStrong` is a mid grey (128) rather than `ButtonShadow` (160, 2.6:1 on
white), so a border an element draws because it follows the theme reaches
3:1; an element styled by hand keeps its default border. A theme of your
own should hold the same ratios on the surfaces each ink is meant for -
muted text is not for `pressed`, faint text not for `hover`.

## Using it

The framework starts with `Light`, built from the colours the elements
default to; `Dark` is the greys the dark menu, tooltip and text area already
use. An application sets the theme before it builds its window, and reads
it where it builds a style:

```cpp
UltraCanvasTheme::SetCurrent(UltraCanvasTheme::Dark());
timeline->SetStyle(VideoTimelineStyle::FromTheme());       // the current theme
field->SetStyle(TimecodeFieldStyle::FromTheme());
```

An application with a design of its own fills the roles from its tokens and
names the element roles it has colours for:

```cpp
UltraCanvasTheme theme = UltraCanvasTheme::Dark();
theme.name = "My app";
theme.roles.panel = Color(21, 23, 26);
theme.roles.accent = Color(242, 165, 58);
theme.SetRole("video-timeline.playhead", Color(255, 93, 79));
UltraCanvasTheme::SetCurrent(theme);
timeline->SetStyle(VideoTimelineStyle::FromTheme(theme));
bool named = theme.HasRole("video-timeline.playhead");                        // true
Color beat = theme.Role("video-timeline.beat", Color(255, 255, 255, 41));     // the fallback: not named
```

### Following the theme

Every element's style struct that has colours gained a `FromTheme`: the
roles mapped onto its fields, over a **base** style whose shape (sizes,
fonts, flags) stays - the struct's default when none is given, so a
`FromTheme()` with no arguments is a themed default. The menu and the
tooltip start from their `Dark()` or `Light()` preset by the theme's
`dark` flag; the text input from `Default()`.

```cpp
button->SetStyle(ButtonStyle::FromTheme());                 // the current theme over the default shape
TextInputStyle rounded = input->GetStyle();
rounded.borderRadius = 8;
input->SetStyle(TextInputStyle::FromTheme(UltraCanvasTheme::Current(), rounded));   // its own shape kept
menu->SetStyle(MenuStyle::FromTheme());                     // Dark() or Light() by the theme, the roles over it
UltraCanvasTooltipManager::SetStyle(TooltipStyle::FromTheme());
```

An element that **follows the theme** styles itself from the current
theme at once and again whenever it changes: `ApplyTheme` puts the theme's
colours and fonts over the element's own style
(`FromTheme(theme, GetStyle())`), so a radius or a toolbar's icon size set
before survive a switch. A container passes a change on to its children, a
window to its whole tree.

Whether a new element follows is the application-wide default,
`UltraCanvasUIElement::SetFollowThemeByDefault`: the policy turns it on
(`FollowHost`, `Light`, `Dark`) in `Initialize`, so every element built
after it takes the theme at the end of its constructor; an `Own`
application turns it on itself when its theme is in place. A style **set by
hand** (`SetStyle`, a dialog's too; `SetConfig` on an album, `SetAppearance`
on a toolbar, `SetVisualStyle` on a switch) stops the following for that
element, so it keeps its look through every change, as before; an element
that should keep both calls `SetStyle` and then `SetFollowTheme(true)`,
which puts the roles over the given style and follows on from there. A
setter that changes one thing (`SetTextColor`, `SetFont`) does not stop
the following: the next theme change puts the theme's value back, so a
colour an application means to keep goes through `SetStyle`, or is set
again from its own change handler.

```cpp
UltraCanvasUIElement::SetFollowThemeByDefault(true);        // what the policy does; an Own application's own call
input->SetFollowTheme(true);                                // styled now, and on every change
panel->SetFollowTheme(true);
button->SetFollowTheme(false);                              // keeps the look it has
bool follows = input->GetFollowTheme();
bool byDefault = UltraCanvasUIElement::FollowThemeByDefault();
ButtonStyle own = button->GetStyle();
own.cornerRadius = 10;
button->SetStyle(own);                                      // styled by hand: follows no more
button->SetFollowTheme(true);                               // the roles over the hand-set style, following again
```

`SetCurrent` calls every **change handler** after the switch. The
application installs one at start-up (`UltraCanvasApplicationBase::Initialize`)
that walks its windows - every element that follows re-styles - and sets
the tooltip manager's style from the theme, so an application switches
its look with one call and re-applies by hand only what it styled by hand:

```cpp
int handler = UltraCanvasTheme::AddChangeHandler([](const UltraCanvasTheme& theme) {
    // an application's own panels, drawn from the roles
    (void)theme.roles.panel;
});
UltraCanvasTheme::SetCurrent(UltraCanvasTheme::Dark());     // the handlers run, the windows re-style
UltraCanvasTheme::RemoveChangeHandler(handler);
```

A handler added during a change is not called for that change; one
removed during a change is not called again.

An element with a colour of its own that no shared role fits names an
**element role** with a derived fallback, as the timeline does: the
checkbox's mark and the radio's dot (`checkbox.mark`, `radio.dot`, the
accent), a filled rating symbol with its hover and outline (`rating.on`,
`rating.hover`, `rating.border`, the amber the struct defaults to on any
window), a stepper's error glyph (`stepper.on-error`, white), the
calendar's weekends (`calendar.weekend`, its red), the clock's dial
(`time-picker.clock-face`, the pressed fill), a scrollbar's track and
thumb (`scrollbar.track`, `scrollbar.thumb`, the panel and the control
border), a columns tree's section header (`columns-tree.group-header`,
`columns-tree.on-group-header`, its black bar), the filer's overlays
(`filer.icon-menu`, `filer.on-icon-menu`, `filer.icon-menu-done`,
`filer.extension-bar`, `filer.extension-tag`, `filer.on-extension-tag`),
the colour picker's markers (`color-picker.marker`,
`color-picker.marker-outline`, white and black), the rich text edit's
paper (`rich-text.page`, the well), the busy indicator's second arc
(`busy-indicator.second-arc`, its orange), the recorder's pulse
(`audio-recorder.pulse`, its red), the video player's, recorder's and
tile's dark stages and bars (`video-player.stage`,
`video-player.control-bar`, `video-recorder.stage`,
`video-recorder.control-bar`, `video-tile.stage`, `video-tile.caption`),
the slideshow's stage (`slideshow.stage`, its black) and the curve
editor's identity line (`curve-editor.diagonal`, its red).

```cpp
UltraCanvasTheme brand = UltraCanvasTheme::Light();
brand.SetRole("rating.on", Color(242, 165, 58));                 // the brand's star
brand.SetRole("calendar.weekend", brand.roles.inkMuted);         // weekends quiet, not red
CheckboxVisualStyle box = CheckboxVisualStyle::FromTheme(brand); // the accent for the mark: not named
RatingStyle stars = RatingStyle::FromTheme(brand, RatingStyle());
```

## Fonts and text roles

The font half of a look lives in the theme too: `fonts.ui`, the interface
font at its base size in points; `fonts.mono`, the monospace font; and
`fonts.scale`, the host's text scale over both (the window's DPI is
applied after). A preset carries the bundled Ubuntu 12 and Ubuntu Mono 12;
`FromHost` puts the desktop's in. An element's text plays a role, and the
role has a size: `Body` 1.0 of the base, `Caption` 0.92 (the 11 pt of a
12 pt base), `Small` 0.85, `Heading` 1.25, `Title` 1.5, `Mono` the
monospace font at the body size, each to the quarter point.

```cpp
UltraCanvasTheme theme = UltraCanvasTheme::Current();
double caption = theme.FontSize(ThemeTextRole::Caption);         // 11 for a 12 pt base at scale 1
FontStyle heading = theme.Font(ThemeTextRole::Heading);          // the interface font at 1.25 of the base
FontStyle code = theme.Font(ThemeTextRole::Mono);                // the monospace font
theme.fonts.ui.fontFamily = "Segoe UI";
theme.fonts.ui.fontSize = 9;
theme.fonts.scale = 1.25;                                        // Body is then 11.25
```

Every `FromTheme` reader sets its style's family and size from `Font(role)`
by the role its text plays: the button, label, dropdown, text input, menu,
switch, checkbox, radio, segmented control, spinner, rating, calendar,
the date and time pickers, the list's rows, the tree, the filer, a group
box's caption, the breadcrumb, the pagination, the colour picker, the
chip, the collapsible panel's title, the sidebar's items, the
auto-complete's list and the new-document dialog's types at `Body`, a
stepper's titles too; a heading label, a card's title and the
new-document dialog's title at `Heading`; the tooltip, a slider's value,
a dialog's details, a stepper's descriptions, a list's header, the
filer's small text, the palette manager, the badge, the collapsible
panel's detail, the sidebar's sections and badges, the video tile's
caption and the font viewer's captions at `Caption`, the dialog's message
at `Body`, the tab titles at `Caption`; the rich text edit keeps the
document's fonts, the slideshow its display sizes; the album, the timecode field and
the timeline take the families only (their sizes are pixels of a design).
The style struct's own literal stays the default of an element styled by
hand, as the colour defaults do. The text area keeps its font: it is the
document's, not the window's.

## The theme from the host

`FromHost` builds the theme a desktop asks for from what
[`UltraCanvasHostAppearance.md`](UltraCanvasHostAppearance.md) read:
`Light()` or `Dark()` by the host's dark flag, `LightHighContrast()` or
`DarkHighContrast()` when it asks for high contrast (black on white or
white on black, every ink at 7:1), the five accent roles derived from the
host's one accent, its fonts and text scale; the name says so (`Host
Dark`, `Host Light High Contrast`).

```cpp
UltraCanvasTheme host = UltraCanvasTheme::FromHost(app->GetHostAppearance());
ThemeRoles derived = UltraCanvasTheme::DeriveAccent(UltraCanvasTheme::Dark().roles, Color(0xe9, 0x54, 0x20), true);
double ratio = UltraCanvasTheme::Contrast(derived.onAccent, derived.accent);   // at least 4.5
```

`DeriveAccent` keeps the readability the test enforces: text on the
accent is white or the dark ink, whichever reads better, and the accent is
moved toward black or white a step at a time until that text reaches
4.5:1 (7:1 for high contrast) - a yellow or a mid-grey accent is nudged, a
blue stays itself; `accentHover` is the accent moved 8 % toward the ink;
`accentSoft` the accent at 18 % over the panel when dark, 14 % when light;
`focus` the accent unless it fails 3:1 on the ground, then `lineStrong`.

## The policy

The application decides what the theme follows
(`UltraCanvasApplication::SetThemePolicy`): `FollowHost`, the default,
builds `FromHost(GetHostAppearance())` in `Initialize` before the first
window and again on every host change, so switching the desktop to dark
re-styles every element that follows and every window built with the
default background takes the theme's ground; `Light` and `Dark` take the
framework's preset with the host's fonts; `Own` leaves the theme to the
application, with a provider - a function from the host to the
application's theme, UltraVideo's `ThemeFor` is one - called at start and
on every change when it gave one. An application that calls `SetCurrent`
itself under another policy has taken over: the policy does not put its
theme back over it.

An `Own` provider builds the application's theme and takes the host's
fonts onto it with `TakeHostFonts` - the interface and monospace fonts
where the host names one, the text scale where it is positive, as
`FromHost` does - so the application's colours come with the desktop's
fonts. UltraVideo, UltraPress, ChatCube and UltraDBAdmin do this: their
`ThemeFor` / `AppTheme` is the provider, the look picked from their own
setting (`system`, which follows the desktop, `light` or `dark`) where
they have one.

```cpp
app->SetThemePolicy(ThemePolicy::Own);
app->SetThemeProvider([](const HostAppearance& host) {
    UltraCanvasTheme theme = host.dark ? UltraCanvasTheme::Dark() : UltraCanvasTheme::Light();
    theme.name = "My app";
    theme.TakeHostFonts(host);
    return theme;
});
ThemePolicy policy = app->GetThemePolicy();
bool fromPolicy = app->ThemeIsFromPolicy();                      // the current theme is the policy's
```

## The setting

A person's choices over the desktop, for every UltraCanvas application at
once: `Appearance.conf` in the UltraCanvas settings folder
(`UltraCanvasSettingsFolder.h`), the `Emoji.conf` pattern - every line
optional, a missing or `system` line meaning "what the host says":

```
look=system          system | light | dark
accent=system        system | #rrggbb
font=system          system | <family>
fontsize=system      system | <points>
scale=1.0            over the host's text scale, 0.5 to 4
contrast=system      system | high | normal
```

`AppearanceSettings` (`UltraCanvasAppearanceSettings.h`) is the file:
`Parse` and `Serialize` are the text and back, `ApplyTo` puts the choices
over a `HostAppearance` (the source gains `+settings`), `Load` and `Save`
are the file, written beside and renamed over. The application applies it
on every read of the host, before the `ULTRACANVAS_HOST_APPEARANCE`
override, which still wins for one run; under `FollowHost` the theme is
then built from the result, so `look=dark` on a light desktop is a dark
theme in the desktop's font, and `scale=1.25` makes every text a quarter
larger than the desktop's. UOS-Settings > Appearance > Look writes it
(`Docs/UOSSettings/README.md`); an application may write it too, with
`Save`, when a choice of its own should be a choice for every application.

A change reaches a running application without a restart: the application
base owns one `UltraCanvasSettingsWatcher` (`UltraCanvasSettingsWatcher.h`)
on the settings folder - one `UltraCanvasFolderWatcher` where the build has
a native backend, a five-second poll of the files' modification times where
it has none - and a change to `Appearance.conf` re-reads the host, so the
policy, the elements that follow and the host appearance handlers all hear
it as they hear a change of the desktop. `Watch(fileName, handler)` takes
another file; the handlers run on the UI thread once per burst of changes.

```cpp
AppearanceSettings settings = AppearanceSettings::Load();      // the file, or the defaults
settings.look = "dark";
settings.scale = 1.25;
settings.Save();                                                 // every application follows within a moment
HostAppearance host = app->GetHostAppearance();                  // the host with the file applied
bool fromSettings = host.source.find("+settings") != std::string::npos;
```

## Related

- `Docs/UltraCanvas/UltraCanvasVideoTimeline.md`, `UltraCanvasTimecodeField.md` — the two readers with element roles of their own
- `Docs/UltraCanvas/UltraCanvasTooltipManager.md` — `TooltipStyle::FromTheme`, the manager's style
- `Apps/UltraVideo/UltraVideoTheme.h` — a design's tokens mapped onto the theme (`ThemeFor`)
- [`UltraCanvasHostAppearance.md`](UltraCanvasHostAppearance.md) — what `FromHost` reads: the desktop's dark flag, contrast, accent, fonts and text scale
- [`UltraCanvasHostThemeProposal.md`](UltraCanvasHostThemeProposal.md) — the whole: every element following by default (T4), the setting and UOS-Settings (T5)
