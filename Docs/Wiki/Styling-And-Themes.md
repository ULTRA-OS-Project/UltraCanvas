# Styling and themes

UltraCanvas styles elements at three levels, from broad to narrow:

1. **The theme** - a set of colour *roles* (`UltraCanvasTheme`) that every
   element reads. Switch the theme and every following element re-styles.
2. **The element's style struct** - `ButtonStyle`, `TextInputStyle`,
   `LabelStyle`, `DropdownStyle`, `SliderVisualStyle`, `MenuStyle` ... -
   every colour, size, font and flag of one element, set with `SetStyle`.
3. **Convenience setters** on the element - `SetColors`, `SetFont`,
   `SetCornerRadius`, `SetBorder` ... - that change one field of the style.

Each element page lists its style struct and fields under **Option types in
this header**, and its setters under **Options and setters**.

In depth: [`UltraCanvasTheme.md`](../UltraCanvas/UltraCanvasTheme.md) and
[`UltraCanvasHostAppearance.md`](../UltraCanvas/UltraCanvasHostAppearance.md).

## Colour roles

A role is named for what it is *for*, never for how it looks:

| Role | What it is for |
|---|---|
| `ground` | the window ground and the gutters between panels |
| `panel` | a panel's surface |
| `well` | an inset surface: a field, a lane |
| `raised` | a raised surface: headers, a menu, a button at rest |
| `hover`, `pressed` | the fills under the pointer and pressed |
| `line`, `lineStrong` | hairline dividers; control borders, grips |
| `ink`, `inkMuted`, `inkFaint` | primary text and icons; secondary text; placeholders and disabled text |
| `accent`, `accentHover`, `accentSoft`, `onAccent` | primary fills and selection; the accent under the pointer; the tint behind a selected row; text on the accent |
| `focus` | the keyboard focus ring, snap lines, drop outlines |
| `danger` | destructive actions, errors, an invalid entry's border |

Elements with colours beyond these read **element roles** by name
(`slider.handle`, `button.primary`, `label.heading`,
`video-timeline.playhead` ...) with a fallback derived from the shared roles.

## Presets and the host

| Theme | |
|---|---|
| `UltraCanvasTheme::Light()` | the framework's light look |
| `UltraCanvasTheme::Dark()` | the dark look |
| `UltraCanvasTheme::LightHighContrast()`, `DarkHighContrast()` | every ink at 7:1 |
| `UltraCanvasTheme::FromHost(host)` | what the desktop asks for: light or dark, high contrast, its accent, fonts and text scale |

What the application's theme follows is its **policy**,
`app.SetThemePolicy(ThemePolicy::...)`: `FollowHost` (the default - follows
the desktop live, so switching it to dark re-styles every window), `Light`,
`Dark`, or `Own` with a provider (`SetThemeProvider`) that builds the
application's own theme from the host's.

## Using the theme

```cpp
// Before building the window: a dark theme with the application's accent.
UltraCanvasTheme theme = UltraCanvasTheme::Dark();
theme.name = "My app";
theme.roles.accent = Color(242, 165, 58);
theme.SetRole("button.primary", Color(242, 165, 58));
UltraCanvasTheme::SetCurrent(theme);

// React when the theme changes (the host switched to dark, say).
int handler = UltraCanvasTheme::AddChangeHandler([](const UltraCanvasTheme& t) {
    // re-read colours you draw yourself
});
UltraCanvasTheme::RemoveChangeHandler(handler);
```

## Styling one element

```cpp
auto ok = std::make_shared<UltraCanvasButton>("Ok", Tr("OK"));
ok->SetThemeVariant(ButtonVariant::Primary);      // the one main action: the accent fill

auto field = std::make_shared<UltraCanvasTextInput>("Search");
TextInputStyle rounded = field->GetStyle();
rounded.borderRadius = 8;
field->SetStyle(TextInputStyle::FromTheme(UltraCanvasTheme::Current(), rounded));   // theme colours, own shape
```

- `Style::FromTheme(theme, base)` maps the roles onto a style struct while
  keeping the base's sizes, fonts and flags; `FromTheme()` alone is the
  themed default.
- An element styled **by hand** with `SetStyle` keeps its look when the theme
  changes; an element left alone follows the theme. Fonts set with
  `SetFont` / `SetFontSize` are kept while the colours follow.
- Variants pick a part in the window instead of colours:
  `ButtonVariant::Standard / Ghost / Primary / Selected`,
  `LabelVariant::Body / Muted / Faint / Heading`.
- Tooltips and menus: `UltraCanvasTooltipManager::SetStyle(TooltipStyle::FromTheme())`,
  `menu->SetStyle(MenuStyle::FromTheme())`.

## Fonts and sizes

Fonts are set per element (`SetFont(family, size, weight)`) or through the
style struct's `fontFamily` / `fontSize` / `fontWeight`; the theme carries
the host's interface and monospace fonts and text scale. Sizes are in
device-independent pixels: the window's device scale (`GetDeviceScale`) maps
them to the screen, so a layout looks the same on a high-DPI display.

Next: [Rendering](Rendering.md)
