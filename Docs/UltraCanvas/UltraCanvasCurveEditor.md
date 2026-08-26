# UltraCanvas Curve Editor and Curves Dialog

## Overview

The **tone curve** stack is the framework's "Curves" facility — the per-channel
remap of image tones every photo editor puts behind that name. It comes in
three layers, each usable on its own:

| Layer | What it is | Files |
|---|---|---|
| `UltraCanvasToneCurve` / `ToneCurveSet` | The model: control points, monotone interpolation, 256-entry lookup tables | `include/UltraCanvasToneCurve.h`, `core/UltraCanvasToneCurve.cpp` |
| `UltraCanvasCurveEditor` | The element the user drags points in (grid, histogram backdrop, selection) | `include/UltraCanvasCurveEditor.h`, `core/UltraCanvasCurveEditor.cpp` |
| `UltraCanvasCurvesDialog` | The window: channel selector, editor, preview toggle, OK / Cancel / Reset | `dialogs/UltraCanvasCurvesDialog.h`, `dialogs/UltraCanvasCurvesDialog.cpp` |

**Version**: 1.0.0
**Last Modified**: 2026-08-25
**Author**: UltraCanvas Framework

None of the three touches pixels. They produce lookup tables;
`PixelFX::Colour::MapLut()` applies them (see below). That split is what makes
the same curve usable for a live preview, a saved file and a batch job, and it
keeps the model free of any image-library include.

The first caller is **UltraCanvasMediaViewer** (the *Curves* toolbar button of
UltraViewer / the UltraFiler preview), which previews the curve live on the
image it is already showing.

## The model

### `ToneCurvePoint`

```cpp
struct ToneCurvePoint {
    float input  = 0.0f;   // original tone, 0..1
    float output = 0.0f;   // tone it maps to, 0..1
};
```

Both coordinates are normalised, so a curve does not depend on the bit depth it
is later applied at.

### `UltraCanvasToneCurve`

A curve always holds at least the two endpoints. Their **input** is pinned to 0
and 1; their **output** is draggable — that is how the black and white points
are set. Interior points stay strictly between their neighbours, so the mapping
can never fold back on itself.

```cpp
UltraCanvasToneCurve curve;                 // identity: 0->0, 1->1
int i = curve.AddPoint(0.5f, 0.65f);        // lift the midtones
curve.MovePoint(i, 0.5f, 0.70f);            // returns the point's new index
curve.RemovePoint(i);                       // interior points only
curve.Reset();

float y = curve.Evaluate(0.25f);            // monotone cubic, clamped to 0..1
std::array<uint8_t, 256> lut = curve.BuildLut();
bool untouched = curve.IsIdentity();        // skip the whole pass when true
```

Interpolation is **Fritsch–Carlson monotone cubic**: the curve passes through
every control point, never overshoots them and never dips backwards, which a
plain Catmull-Rom spline does on a steep S-curve (and which shows up in a photo
as a bright halo where the curve bulges past 1.0).

Curves serialise to a short text form for settings files and presets:

```cpp
std::string text = curve.ToString();        // "0.000,0.000;0.500,0.650;1.000,1.000"
UltraCanvasToneCurve restored;
if (!restored.FromString(text)) { /* malformed: `restored` is untouched */ }
```

Limits: `UltraCanvasToneCurve::MaxPoints` (16) control points;
`MinPointDistance` (1/255) is the closest two points may sit in input.

### `ToneCurveSet`

The four curves an image adjustment is made of — the master plus one per colour
channel:

```cpp
ToneCurveSet set;
set.Channel(ToneCurveChannel::Blue).AddPoint(0.5f, 0.42f);   // cool the midtones
set.rgb.MovePoint(1, 1.0f, 0.9f);                            // pull the whites down

if (!set.IsIdentity()) {
    std::array<std::array<uint8_t, 256>, 3> luts = set.BuildChannelLuts();
    // luts[0] = red, [1] = green, [2] = blue — the channel curve composed
    // with the master curve, in that order.
}
```

## Applying a curve to an image — `PixelFX::Colour::MapLut`

```cpp
PFXImage MapLut(const PFXImage& image,
                const std::vector<std::vector<uint8_t>>& tables);
```

Maps an image through per-channel 8-bit lookup tables. Every table holds 256
entries; pass **one** table to map all colour bands the same way, or **one per
colour band**. A trailing alpha band is passed through untouched, and the image
is mapped as 8-bit (a wider one is cast down first). Any other table size throws
`PixelFXException`.

```cpp
auto luts = set.BuildChannelLuts();
std::vector<std::vector<uint8_t>> tables;
for (const auto& lut : luts) tables.emplace_back(lut.begin(), lut.end());
PixelFX::PFXImage graded = PixelFX::Colour::MapLut(source, tables);
```

## The editor element

```cpp
auto editor = CreateCurveEditor("Curves", 0, 0, 320, 320);
editor->SetCurves(existing);                       // seed from a stored adjustment
editor->SetActiveChannel(ToneCurveChannel::Red);   // which curve is edited/drawn
editor->SetHistogram(ToneCurveChannel::Red, bins); // 256 bin counts, optional
editor->onCurveChanged  = [](const ToneCurveSet& s) { /* live preview */ };
editor->onEditFinished  = [](const ToneCurveSet& s) { /* commit / undo step */ };
editor->onSelectionChanged = [](int index) { /* update a readout */ };
```

Input (x, left to right) is plotted against output (y, bottom to top) over a
grid, the identity diagonal and — when supplied — the histogram of the image
being edited (drawn on a square-root scale so a dominant background tone does
not flatten the rest of the distribution).

Interaction follows the convention of every image editor's curves box:

| Action | Result |
|---|---|
| Left click on empty space | Adds a point there and starts dragging it |
| Left drag on a point | Moves it; endpoints slide along their edge |
| Right click / double click on a point | Removes it |
| `Delete` / `Backspace` | Removes the selected point |
| Arrow keys | Nudges the selected point one 8-bit step (`Shift`: ten) |

`ResetActiveChannel()` / `ResetAllChannels()` return curves to the identity.
`GetSelectedPointValues(in, out)` gives the selected point in 8-bit units
(both `-1` when nothing is selected) — what a readout line shows.

Appearance comes from `CurveEditorStyle` (`Default()` / `Dark()`): grid,
diagonal, curve, point, selection and histogram colours, plus `gridDivisions`,
`pointSize`, `grabRadius` and `curveWidth`. The active channel's curve is drawn
in its own colour (red / green / blue; the master curve in `curveColor`).

## The dialog

```cpp
auto dlg = CreateCurvesDialog(currentCurves);
dlg->SetHistogram(ToneCurveChannel::RGB, bins);        // one call per channel
dlg->onCurvesChanged = [this](const ToneCurveSet& s) { Preview(s); };
dlg->onAccept        = [this](const ToneCurveSet& s) { Commit(s); };
dlg->onCancel        = [this]()                       { Restore(); };
dlg->Create();   // hands ownership to the application window list
dlg->Show();
```

The window holds the channel dropdown (RGB / Red / Green / Blue), a **Preview**
checkbox, the curve editor, the selected-point readout and
*Reset channel* / *Reset all* / *OK* / *Cancel*.

`onCurvesChanged` fires on every drag step **while Preview is ticked**;
unticking it sends an identity set so the host shows the untouched image, and
re-ticking sends the edited curves back. Closing the window through its frame
counts as Cancel. The dialog previews on the host's own display — it carries no
thumbnail of its own, so the user judges the edit at full size.

A host that keeps a pointer to the dialog must clear its callbacks (or close
it) when the host itself goes away: the window lives in the application's window
list, not in the host.

## Notes

- The model has its own unit test (`Tests/ToneCurveTest.cpp`, target
  `ToneCurveTest`): point management, monotonicity, lookup tables, channel
  composition and serialisation. Build with `-DBUILD_TESTS=ON`.
- See also: [UltraCanvasMediaViewer.md](UltraCanvasMediaViewer.md) (the Curves
  button and how the viewer stacks curves with its other adjustments).
- A tone curve is a **function** `y = f(x)`, not free-form geometry. For
  editing Bézier *paths* — anchors with tangent handles, closed shapes — see
  [UltraCanvasBezierEditorProposal.md](UltraCanvasBezierEditorProposal.md),
  which proposes a sibling element rather than a generalisation of this one.
