# UltraCanvas Slide Editing: SlideEdit, SlideDeckEditor

<!-- doc-check: std::shared_ptr<PptxPresentation> deck; UltraCanvasWindow* window; -->

## Contents

## Overview

The **slide editor** edits a presentation: the deck model that the
PowerPoint and OpenDocument readers and writers share
([`UltraCanvasPresentationPptx.h`](../../UltraCanvas/include/UltraCanvasPresentationPptx.h)),
drawn by the same slide painter as the thumbnails, the show and PDF export.
It follows the rich-text editor's split: a core without a window holds the
deck, the selection and the history, and an element draws it and turns the
pointer and the keys into the core's commands. The design is
[`Docs/Research/UltraOfficeDesignProposal.md`](../Research/UltraOfficeDesignProposal.md),
§5.3; UltraOffice's presentation tab is its first host.

| Class | What it is | Files |
|---|---|---|
| `PptxPresentation` (`UCPresentation`) | The deck: slides of shapes in EMU, masters and layouts, transitions, the pictures' bytes | `include/UltraCanvasPresentationPptx.h` |
| `UCSlideDeckEditor` | The editing core: the current slide, the selection, every change as a command with undo and redo, hit testing, handles, snapping | `include/UltraCanvasSlideDeckEditor.h`, `core/UltraCanvasSlideDeckEditor.cpp` |
| `UltraCanvasSlideEdit` | The element: the slide strip, the slide, the selection's handles, dragging, the keys, a simple full-screen show | `include/UltraCanvasSlideEdit.h`, `core/UltraCanvasSlideEdit.cpp` |
| `DrawPptxSlide` | A slide on any render context, into a rectangle; prompt text for empty placeholders | `include/UltraCanvasPptxSlideRender.h` |
| `PresentationPrintPages` | A page per slide for PDF and printers | `include/UltraCanvasPresentationPrint.h` |

## The core

```cpp
#include "UltraCanvasSlideDeckEditor.h"

auto deck = std::make_shared<PptxPresentation>();
UCSlideDeckEditor editor(deck);
editor.NewSlide(-1);                       // Title and Content (or the current slide's layout)
editor.SetShapeText(0, "Quarterly report");
const int box = editor.InsertTextBox(SlideBox{914400, 914400, 4 * 914400, 914400}, "Revenue grew");
editor.Select({box});
editor.MoveSelection(914400 / 2, 0);       // half an inch to the right
editor.Undo();
```

Every change is one undo step. A change to the slide being edited keeps that
slide before and after (its shapes, not the pictures' bytes, which stay in
the deck), with the selection; the slide list's changes - insert, duplicate,
move, delete - keep the slide they move. A run of arrow-key nudges is one
step. `ChangeSlide(description, change, selectAfter)` makes any other change
a step: `change` edits a copy of the slide.

| Group | Calls |
|---|---|
| Slides | `NewSlide(layout)`, `InsertSlide`, `DuplicateSlide`, `DeleteSlide`, `MoveSlide`, `SetSlideHidden`, `SetSlideNotes`, `SetCurrentSlide` |
| Selection | `Select`, `SelectAll`, `ClearSelection`, `ToggleSelected`, `SelectionBox` |
| Shapes | `MoveSelection`, `NudgeSelection`, `SetShapeBox`, `SetSelectionBox`, `SetShapeRotation`, `DeleteSelection`, `DuplicateSelection`, `BringToFront`, `SendToBack`, `BringForward`, `SendBackward`, `SetShapeText` |
| Inserting | `InsertTextBox`, `InsertGeometry` (a preset outline), `InsertPicture` (bytes, kept in `deck.pictures`), `InsertShape` |
| Clipboard | `CopySelection`, `CutSelection`, `Paste` (in place, or offset on the slide the shapes came from) |
| Finding | `ShapeAt` (a turned shape in its own frame), `ShapesInside`, `HandleAt`, `Snap` |
| History | `Undo`, `Redo`, `UndoDescription`, `IsModified`, `onChange`, `onSelectionChange` |

`Snap(box, tolerance, ignore, moving, handle)` lines a moved box's edges and
centre (or a resized box's dragged edges) up with the slide's edges and
centre and with the other shapes' edges and centres, and says which guide
lines it used.

`PptxSlideFromLayout(deck, layout)` makes a new slide from one of the deck's
layouts: its placeholders, emptied, keeping their look; Title and Content
when the deck has no layouts.

## The element

```cpp
#include "UltraCanvasSlideEdit.h"

auto edit = CreateSlideEdit("deck", 0, 0, 1200, 700);
edit->SetDeck(deck);
edit->onEditText = [edit](int shape) {
    edit->Editor().SetShapeText(shape, "New text");   // a host opens its own editor here
};
edit->onChange = []() { /* mark the document modified */ };
```

| Where | What |
|---|---|
| The strip | Click a slide to show it; drag it to move it; Delete deletes it, Ctrl+D duplicates it; hidden slides are dimmed |
| The slide | Click selects a shape (Shift or Ctrl adds or takes away), a band selects what it covers; dragging moves the selection, snapping (Alt: not; Shift: across or down only); the handles resize (Shift keeps the proportions, Alt from the centre) and the one above turns (Shift: by 15°) |
| Keys | Arrows nudge (Ctrl: by a point), Delete, Ctrl+A, Escape, Ctrl+D, Ctrl+C / X / V, Ctrl+Z / Y, Tab the next shape, Enter or F2 its text, Page Up / Down, Ctrl+M a new slide, Ctrl+wheel zoom |

A drag draws a preview copy of the slide and becomes one command when the
button is let go. Empty placeholders show their prompt ("Click to add title")
and a dashed outline. `onContextMenu` reports a right-click in the strip or on
the slide, for a host's menu. `SlideRect()` and `EmuPerPixel()` say where the
slide is drawn, for a host that lays its own editor over a shape - UltraOffice
edits a shape's text in a text area placed there.

`StartShow(fromSlide)` turns the element into a simple show: each slide as
large as the element on black, the hidden ones left out, a click or the
arrows moving on, Escape ending it (`onShowEnd`). The host puts it in a
full-screen window of its own; transitions come with the show player of
Phase 4.

## Drawing and print

```cpp
#include "UltraCanvasPresentationPrint.h"

PresentationPrintPages pages(*deck);          // hidden slides left out
std::string error;
pages.ExportPdf("Talk.pdf", error);
```

`DrawPptxSlide(ctx, deck, slide, rect, options)` draws a slide into any
rectangle of any render context, filling only that rectangle with the slide's
background; `PptxSlideDrawOptions` adds prompt text for empty placeholders
and leaves one shape's text out while a host edits it. A picture is decoded
once and drawn from then on.

## Not yet

- Text is edited as plain lines (the host's in-place editor); the rich
  in-place editor through `UltraCanvasRichTextEdit` comes with Phase 4.
- Tables are drawn and moved, not edited cell by cell.
- Grouping, alignment and distribution commands, guides of one's own.
- The show plays no transitions or animations.

## Tests

- `Tests/SlideDeckEditorTest.cpp` - every command with undo and redo, the
  selection put back, nudges joined, hit testing of turned shapes, handles,
  snapping.
- `Tests/SlideEditElementTest.cpp` - the element driven by pointer and key
  events: a click selects, a drag moves (one step, snapping to the slide's
  centre), a corner with Shift keeps the proportions, the turning handle
  turns by 15° with Shift, a band selects, the strip chooses and moves
  slides, the keys nudge, delete, undo and add a slide, a double-click asks
  for the text.
- `Tests/PresentationPrintTest.cpp` - a slide drawn into a rectangle and
  nothing around it, prompt text, pages and the PDF.
