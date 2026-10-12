# UltraCanvas Theme

<!-- doc-check: std::shared_ptr<UltraCanvasVideoTimeline> timeline; std::shared_ptr<UltraCanvasTimecodeField> field; -->

`UltraCanvasTheme` — the framework's **colour roles**: the object a window's
light or dark look lives in, which an element reads when its style is
built, so an application fills the roles from its own design once and every
element that reads the theme follows. A role is named for what it is for
(the panel, the ink, the accent, the focus ring), never for what it looks
like.

**Namespace:** `UltraCanvas`
**Header:** `include/UltraCanvasTheme.h`
**Source:** `core/UltraCanvasTheme.cpp`
**Readers:** `VideoTimelineStyle::FromTheme`, `TimecodeFieldStyle::FromTheme`
(`include/UltraCanvasVideoTimeline.h`, `include/UltraCanvasTimecodeField.h`)
**Tests:** `Tests/UltraCanvasThemeTest.cpp` (without a display)
**Design:** `Docs/Research/UltraVideoDesignProposal.md` §5.8
**Version:** 1.0.0
**Last Modified:** 2026-10-12
**Author:** UltraCanvas Framework

## Status

The theme, its two presets and the current theme are in place; the video
timeline and the timecode field read it. The other elements keep their own
style structs and presets (`MenuStyle::Dark()`, `TooltipStyle::Light()`,
the text area's `ApplyDarkTheme()`) until each gains a `FromTheme`; an
application themes those as it did before.

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
`video-timeline.muted-hatch` and `video-timeline.scrollbar`.

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

**No automatic re-style.** An element takes its colours when its style is
set. To switch theme at run time, set the new current theme and re-apply
the elements the window built, as UltraVideo's `ApplyPalette` does for
every panel: the theme is one object, the re-application is the
application's.

## Related

- `Docs/UltraCanvas/UltraCanvasVideoTimeline.md`, `UltraCanvasTimecodeField.md` — the two readers
- `Apps/UltraVideo/UltraVideoTheme.h` — a design's tokens mapped onto the theme (`ThemeFor`)
