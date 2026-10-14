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
**Version:** 1.6.0 - the readability the presets keep, measured by the test
**Last Modified:** 2026-10-12
**Author:** UltraCanvas Framework

## Status

The theme, its two presets, the current theme and its change handlers are
in place. Every panel element reads it: the button, label, dropdown,
slider, switch, text input, toolbar, split pane and album through a
`FromTheme` on their style struct and an `ApplyTheme` on the element; the
tabbed container and the text area through `ApplyTheme` alone; the menu
and the tooltip through `MenuStyle::FromTheme` and `TooltipStyle::FromTheme`,
which put the roles over their own `Dark()` and `Light()` presets, so a
dark menu or tooltip agrees with the rest of a dark window. An element
follows the theme only when asked (`SetFollowTheme`); styled by hand it
keeps its look, as before.

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

An element asked to **follow the theme** styles itself from the current
theme at once and again whenever it changes: `ApplyTheme` puts the theme's
colours over the element's own style (`FromTheme(theme, GetStyle())`), so
a radius, a font or a toolbar's icon size set before survive a switch. A
container passes a change on to its children, a window to its whole tree.

```cpp
input->SetFollowTheme(true);                                // styled now, and on every change
panel->SetFollowTheme(true);
button->SetFollowTheme(false);                              // keeps the look it has
bool follows = input->GetFollowTheme();
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

## Related

- `Docs/UltraCanvas/UltraCanvasVideoTimeline.md`, `UltraCanvasTimecodeField.md` — the two readers with element roles of their own
- `Docs/UltraCanvas/UltraCanvasTooltipManager.md` — `TooltipStyle::FromTheme`, the manager's style
- `Apps/UltraVideo/UltraVideoTheme.h` — a design's tokens mapped onto the theme (`ThemeFor`)
