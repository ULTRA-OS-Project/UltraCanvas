# UltraCanvas Timecode Field

<!-- doc-check: std::shared_ptr<UltraCanvasVideoTimeline> timeline; std::shared_ptr<UltraCanvasVideoPlayerElement> player; -->

`UltraCanvasTimecodeField` — a field for entering and showing **`HH:MM:SS:FF`
timecode**, and the large read-only **readout** a transport bar shows. The
value is a count of whole frames at a frame rate, so a time typed, stepped
or pasted is always on the frame grid. It is the timecode entry of a video
editor's inspector and transport, and of anything else that names a frame.

**Namespace:** `UltraCanvas`
**Header:** `include/UltraCanvasTimecodeField.h`
**Source:** `core/UltraCanvasTimecodeField.cpp`; the frame arithmetic
(`TimelineFramesToTimecode`, `TimelineTimecodeToFrames`) in
`include/UltraCanvasVideoTimelineModel.h`
**Base class:** `UltraCanvasUIElement` (an input element in its own right:
four segments, no caret)
**Demo:** `Apps/DemoApp/UltraCanvasVideoTimelineExamples.cpp` (Video > Video
Timeline: the playhead field and the readout)
**Tests:** `Tests/TimecodeFieldElementTest.cpp` (under Xvfb);
`Tests/VideoTimelineModelTest.cpp` (the arithmetic, without a display)
**Design:** `Apps/UltraVideo/screens/TimecodeField-light.svg` and
`TimecodeField.md`; `Docs/Research/UltraVideoDesignProposal.md` §7.6
**Version:** 1.0.0
**Last Modified:** 2026-10-11
**Author:** UltraCanvas Framework

## Status

Fully implemented: the field, the readout, segments, typed and relative
entries, validation, stepping by keys and wheel, Tab, the maximum, copy and
paste, the accessible value. Light and dark palettes.

## How it behaves

- **At rest** it shows the value, `00:00:17:12`, with the leading zero
  segments dimmed so the eye lands on the digits that matter.
- **Click** a segment (hours, minutes, seconds, frames) to select it; `↑` /
  `↓` and the wheel step it by one of its unit, `←` / `→` and `Tab` /
  `Shift+Tab` move the selection, `Home` and `End` jump to the ends. A step
  commits at once.
- **Type digits** and they fill the timecode from the right: `1`, `7`, `1`,
  `2` shows `00:00:17:12` with the four untyped leading zeros dimmed.
  `Enter` commits; `Backspace` takes the last digit back; a ninth digit
  pushes the first out.
- **A leading `+` or `-`** makes the entry an offset in frames from the
  value: `+12` is twelve frames later, `-100` a second earlier.
- **An entry that is not a timecode** — seconds of 75, a frame at or past
  the rate, an offset that goes below zero, a value past the maximum —
  shows a danger border while it stands. `Enter` does not commit it; `Esc`
  drops the entry and the last valid value stays. Focus leaving the field
  commits a valid entry and drops an invalid one.
- **`Ctrl+C`** copies the timecode; **`Ctrl+V`** takes a timecode from the
  clipboard, or fewer groups counted from the right (`5:12`), or an offset
  (`+12`).
- **The readout** (`SetReadout(true)`) is the large, bold variant with no
  box, takes no focus and no input, and shows what `SetFrames` /
  `SetSeconds` give it.

## Public API surface

```cpp
auto field = CreateTimecodeField("in", 20, 20, 120, 28);
field->SetFrameRate(25.0);                 // 29.97 counts 30 frames a second
field->SetFrames(17 * 25 + 12);            // 00:00:17:12
field->SetSeconds(72.32);                  // rounded to the frame grid
field->SetMaximumFrames(30 * 60 * 25);     // 0 = none; steps and entries stop there
long long frames = field->GetFrames();
double seconds = field->GetSeconds();
std::string shown = field->GetText();      // the value, or the entry so far
bool editing = field->IsEditing();
bool invalid = field->IsInvalid();
field->SelectSegment(3);                   // 0 hours .. 3 frames; -1 none
field->Step(3, 1);                         // one frame later; commits
field->Commit();                           // apply the entry: false when it is not a timecode
field->Cancel();                           // drop the entry, keep the value

field->onCommit = [&](long long f) { timeline->SetPlayhead(f / 25.0); };

auto readout = CreateTimecodeField("transport", 300, 20, 170, 40);
readout->SetReadout(true);
readout->SetFrameRate(25.0);
```

The usual pairing with a timeline and a player: the timeline's or the
player's position feeds the field and the readout, and the field's commit
seeks both.

```cpp
timeline->onPlayheadChanged = [&](double s) { field->SetSeconds(s); readout->SetSeconds(s); };
field->onCommit = [&](long long f) {
    timeline->SetPlayhead(f / 25.0);
    player->Seek(f / 25.0);
};
```

### The arithmetic on its own

```cpp
std::string tc = TimelineFramesToTimecode(437, 25.0);            // "00:00:17:12"
long long f = 0;
bool ok = TimelineTimecodeToFrames("17:12", 25.0, f);            // 437: fewer groups count from the right
ok = TimelineTimecodeToFrames("+12", 25.0, f);                   // 12, with the sign kept by the caller
ok = TimelineTimecodeToFrames("00:00:75:00", 25.0, f);           // false
```

## Style

`TimecodeFieldStyle`: `background`, `border`, `borderFocused` (the focus
ring), `borderInvalid` (danger), `ink`, `inkFaint` (leading zeros and
untyped digits), `selection` and `onSelection` (the selected segment),
`sign` (the `+` or `-` of a relative entry), `fontFamily` (a monospace with
tabular figures, so the digits never jitter), `fontSize` and `readoutSize`
in pixels, `cornerRadius`, `paddingX`. `LightPalette()` is the default,
`DarkPalette()` the dark one.

```cpp
TimecodeFieldStyle style = TimecodeFieldStyle::DarkPalette();
style.selection = Color(242, 165, 58);
field->SetStyle(style);
```

## Notes

- The value is clamped at zero and at the maximum; `SetFrames` ends any
  entry in progress.
- The accessible role is a text field; `SetAccessibleValueText("1:00")`
  sets the value from text the way a paste does, and the accessible value
  is the shown text.
- Fractional frame rates round to whole frames a second for display and
  entry, as the timeline element does.
