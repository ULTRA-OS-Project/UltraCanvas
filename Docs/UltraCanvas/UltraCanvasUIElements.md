# UltraCanvas UI elements — the catalogue

Every other document here describes **one** component. This one answers the
question that comes first: *does an element for this already exist?* Read it
before you draw anything.

The framework ships around sixty UI elements. New UI is assembled from them; it
is not painted from scratch.

> **If it takes input, shows a picture, or presents a value, it is an element.**
> Never build one out of `ctx->DrawText` / `FillRoundedRectangle` plus a private
> buffer, caret, `hovered`/`focused` flag or key handler. Take one from the
> tables below, or add a new element so the next caller finds it too.

A hand-painted control looks right in a screenshot and then fails everywhere the
framework already solved the problem: no caret or selection, no clipboard, no
undo, no IME or multi-byte input, no keyboard-focus semantics, no theming, no
DPI scaling, no tooltip. A real case from this repository: the Filer's compress
dialog painted its own name field, so the name could only be appended to and
backspaced at the end, and it stopped answering the keyboard entirely as soon as
another element took the focus. It is an `UltraCanvasTextInput` child now.

The **header** column is authoritative — it is checked by the compiler, unlike a
doc file name. For the prose on a given element, use
`grep -l <Class> Docs/UltraCanvas/*.md` or search `llms.txt`.

## Text entry

| You need | Element | Header |
|---|---|---|
| Single-line text | `UltraCanvasTextInput` | `UltraCanvasTextInput.h` |
| Multi-line text or code | `UltraCanvasTextArea` | `UltraCanvasTextArea.h` |
| Text with a suggestion list | `UltraCanvasAutoComplete` | `UltraCanvasAutoComplete.h` |
| Tokens / tags typed into a field | `UltraCanvasTagInput` | `UltraCanvasChip.h` |
| A number with up/down steppers | `UltraCanvasSpinner` | `UltraCanvasSpinner.h` |
| Password quality feedback | `UltraCanvasPasswordStrengthMeter`, `UltraCanvasPasswordRuleLegend` | matching `*.h` |

`UltraCanvasTextInput` covers what a text field is expected to do: caret,
click-to-position, drag selection, Home/End/arrows, Delete, cut/copy/paste,
undo/redo, placeholder, max length, validation state, horizontal scrolling and
multi-byte text. Reproducing even half of that by hand is a week of bugs.

Password fields also get a reveal control: `SetShowPasswordToggle(true)` paints
an eye button inside the field, and `SetPasswordRevealed()` drives the same state
from an external "Show password" checkbox. See
[UltraCanvasTextInputExamples.md](UltraCanvasTextInputExamples.md).

## Buttons and choices

| You need | Element | Header |
|---|---|---|
| Push, split or icon button | `UltraCanvasButton` | `UltraCanvasButton.h` |
| Checkbox | `UltraCanvasCheckbox` | `UltraCanvasCheckbox.h` |
| Radio button / group | `UltraCanvasRadio`, `UltraCanvasRadioGroup` | `UltraCanvasRadio.h` |
| On-off switch | `UltraCanvasSwitch` | `UltraCanvasSwitch.h` |
| Pick one of a list | `UltraCanvasDropdown` | `UltraCanvasDropdown.h` |
| Pick one of a few, shown side by side | `UltraCanvasSegmentedControl` | `UltraCanvasSegmentedControl.h` |
| A value on a range | `UltraCanvasSlider` | `UltraCanvasSlider.h` |
| A tone / mapping curve (Curves) | `UltraCanvasCurveEditor` | `UltraCanvasCurveEditor.h` |
| A score out of N | `UltraCanvasRating` | `UltraCanvasRating.h` |
| Step through a sequence | `UltraCanvasStepper` | `UltraCanvasStepper.h` |

## Text, images and media

| You need | Element | Header |
|---|---|---|
| Static text | `UltraCanvasLabel` | `UltraCanvasLabel.h` |
| Count or status pill | `UltraCanvasBadge`, `UltraCanvasChip` | `UltraCanvasBadge.h`, `UltraCanvasChip.h` |
| Show an image (file, memory, SVG, animation) | `UltraCanvasImageElement` | `UltraCanvasImageElement.h` |
| Zoomable / pannable image | `UltraCanvasZoomPanImage` | `UltraCanvasImageViewer.h` |
| Any media file — image, video, audio, PDF, text, spreadsheet, eBook | `UltraCanvasMediaViewer` | `UltraCanvasMediaViewer.h` |
| Video / audio playback | `UltraCanvasVideoPlayerElement`, `UltraCanvasAudioPlayerElement` | matching `*.h` |
| Video / audio capture | `UltraCanvasVideoRecorderElement`, `UltraCanvasAudioRecorderElement` | matching `*.h` |
| Audio waveform, input level | `UltraCanvasWaveformElement`, `UltraCanvasLevelMeter` | `UltraCanvasWaveformElement.h`, `UltraCanvasAudioRecorderElement.h` |
| A gallery or a timed slideshow | `UltraCanvasAlbum`, `UltraCanvasSlideshow` | matching `*.h` |
| eBooks | `UltraCanvasEBookViewer` | `UltraCanvasEBookViewer.h` |
| Raw OpenGL | `UltraCanvasGLSurface` | `UltraCanvasGLSurface.h` |

`UltraCanvasImageElement` is the answer to "I just need to draw a picture" — it
goes through the shared `UCImage` cache, handles scaling modes, SVG and animated
formats, and does not re-decode on every frame the way an ad-hoc
`DrawImage(path, …)` in a `Render()` does.

## Layout and structure

| You need | Element | Header |
|---|---|---|
| Group children, scroll them | `UltraCanvasContainer` (scrolling is built in) | `UltraCanvasContainer.h` |
| A standalone scrollbar | `UltraCanvasScrollbar` | `UltraCanvasScrollbar.h` |
| Resizable panes | `UltraCanvasSplitPane`, `UltraCanvasSplitter` | `UltraCanvasSplitPane.h` |
| Tabs | `UltraCanvasTabbedContainer` | `UltraCanvasTabbedContainer.h` |
| A titled frame | `UltraCanvasGroupBox` | `UltraCanvasGroupBox.h` |
| A toolbar | `UltraCanvasToolbar` | `UltraCanvasToolbar.h` |
| A rule or a gap | `UltraCanvasSeparator`, `UltraCanvasSpacer` | matching `*.h` |

Positioning inside a container is the CSS layout engine's job (`layout` /
`layoutItem` — see [CSSLayout.md](../CSSLayout.md)), not manual arithmetic on
`SetBounds`.

## Navigation and data views

| You need | Element | Header |
|---|---|---|
| Menu bar, popup and context menus | `UltraCanvasMenu` (+ `MenuBuilder`, `MenuItemData`) | `UltraCanvasMenu.h` |
| Editable menu configuration | `UltraCanvasMenuConfigWidget` | `UltraCanvasMenuConfigWidget.h` |
| Path bar | `UltraCanvasBreadcrumb` | `UltraCanvasBreadcrumb.h` |
| Page selector | `UltraCanvasPagination` | `UltraCanvasPagination.h` |
| List, tree, table, sheet | `UltraCanvasListView`, `UltraCanvasTreeView`, `UltraCanvasTableView`, `UltraCanvasSpreadsheet` | matching `*.h` |
| Folder contents / file browsing | `UltraCanvasFilerWidget` | `UltraCanvasFilerWidget.h` |
| System information (CPU, GPU, NPU, memory, drives, network, USB, Bluetooth) | `UltraCanvasHardwareInfoPanel` | `UltraCanvasHardwareInfoPanel.h` |

## Pickers, dialogs and feedback

| You need | Element | Header |
|---|---|---|
| Colour | `UltraCanvasColorPicker` | `UltraCanvasColorPicker.h` |
| A colour out of a small palette (a strip of swatches, sized to the space it gets) | `UltraCanvasColorSwatchBar` | `UltraCanvasColorSwatchBar.h` |
| Date, date range, month grid | `UltraCanvasDatePicker`, `UltraCanvasDateRangePicker`, `UltraCanvasCalendarView` | `UltraCanvasDatePicker.h` |
| Time, clock face | `UltraCanvasTimePicker`, `UltraCanvasTimeClockView` | `UltraCanvasTimePicker.h` |
| Modal dialog | `UltraCanvasModalDialog` | `UltraCanvasModalDialog.h` |
| Progress of a long operation (ring + percentage + Cancel) | `UltraCanvasProgressDialog` | `UltraCanvasProgressDialog.h` |
| Open / save a file, prompt for a value | `UltraCanvasFileDialog`, `UltraCanvasInputDialog` | `UltraCanvasModalDialog.h` |
| Native OS file dialog | `UltraCanvasNativeDialogs` | `UltraCanvasNativeDialogs.h` |
| Edit an image's tone curves (per channel, over a histogram) | `UltraCanvasCurvesDialog` | `dialogs/UltraCanvasCurvesDialog.h` |
| Hover help | `UltraCanvasTooltipManager` (+ `TooltipContent`) | `UltraCanvasTooltipManager.h` |

Charts, diagrams and document views live under `UltraCanvas/Plugins/` with their
own docs — check there before drawing a graph by hand as well.

## Creating them

Widgets are `std::shared_ptr`-managed. Use the `Create*` factory where one
exists:

```cpp
auto name   = CreateTextInput("archive-name", 0, 0, 240, 26);
auto shot   = CreateImageElement("preview", 0, 0, 320, 240, "poster.png");
auto accept = CreateButton("ok", 101, 0, 0, 104, 30, "Compress");
```

An element that belongs to a self-rendered view is added as a child of it and
positioned each frame with `UltraCanvasContainer::PlaceChildAt(child, rect)` —
**not** `SetBounds()`, which writes only `finalBounds` and is undone by the next
layout pass. Getting that wrong is subtle rather than obvious: a text input
re-clamps its horizontal scroll when its width changes, so an editor placed with
`SetBounds()` renders scrolled to the tail of its own value. See
`UltraCanvasFilerWidget`'s rename editor and compress dialog, the pickers'
fields and the spreadsheet's cell editor for the pattern.

## The two legitimate exceptions

1. **Self-rendered views.** A widget that draws thousands of cells itself —
   `UltraCanvasFilerWidget`, `UltraCanvasAlbum`, `UltraCanvasListView`, the
   chart and diagram plugins — paints its *content* directly, because creating
   an element per cell would not scale. That licence covers the content only:
   as soon as such a view needs an editable field, a button or a picker, it adds
   a real element as a child rather than painting a fake one.
2. **The element itself.** `UltraCanvasTextInput` owns a buffer and a caret
   because it *is* the text input.

Anything else that reaches for raw drawing to make a control is a bug report
waiting to be filed.

## Enforcement

`scripts/check_ui_reuse.py` looks for the two shapes that are almost always
wrong — a private edit buffer plus caret fed from a `KeyDown` handler, and a
`Draw*Button(IRenderContext*, …, bool hovered)` painter — and CI runs it on
every pull request:

```bash
python3 scripts/check_ui_reuse.py            # report
python3 scripts/check_ui_reuse.py --strict   # what CI runs
```

`scripts/ui_reuse_baseline.txt` records controls that predate the check —
it is **empty**, because every control in the tree is now built from an
element, and the intent is that it stays empty. Do not add to it to silence a
finding. A genuine exception is declared in the source instead, with a reason:

```cpp
// ui-reuse-exempt: the treemap paints its own cells; there is no element per cell.
```

## When the element does not exist

Add it under `UltraCanvas/{include,core}` with a doc in `Docs/UltraCanvas/`, a
row in the tables above, and a changelog entry. A one-off painted into a single
dialog helps one screen; an element helps every caller and gets the keyboard,
focus and theming behaviour right once.
