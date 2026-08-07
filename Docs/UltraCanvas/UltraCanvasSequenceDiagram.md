# UltraCanvasSequenceDiagram, SequenceModel and text export

UML 2 sequence diagrams: a UI-free interaction model, the rendering element,
and PlantUML/Mermaid export.

| Unit | Header | Source |
|---|---|---|
| Model | `include/Plugins/Diagrams/UltraCanvasSequenceModel.h` | `Plugins/Diagrams/UltraCanvasSequenceModel.cpp` |
| Text export | `include/Plugins/Diagrams/UltraCanvasSequenceTextExport.h` | `Plugins/Diagrams/UltraCanvasSequenceTextExport.cpp` |
| Element | `include/Plugins/Diagrams/UltraCanvasSequenceDiagram.h` | `Plugins/Diagrams/UltraCanvasSequenceDiagram.cpp` |

Tests: `Tests/SequenceModelTest.cpp` (target `SequenceModelTest`).
Demo: `Apps/DemoApp/UltraCanvasSequenceDiagramExamples.cpp` (six tabs).
Version: 1.0.0.

The layering matches the class-diagram feature
([`UltraCanvasUMLModel.md`](UltraCanvasUMLModel.md)): the model owns meaning
and includes nothing from the widget or render stack, so activation nesting,
message numbering, validation and export are unit-tested without a window.
The element owns geometry and appearance; text is measured with the real
render context on the first paint.

The load-bearing idea: **vertical position is message order**. Every y
coordinate derives from a message index, so fragments and notes anchor to
message indices, never to coordinates, and the layout is a single walk down
the message list.

---

## 1. The model

```cpp
#include "Plugins/Diagrams/UltraCanvasSequenceModel.h"

UltraCanvasSequenceModel model;
model.SetTitle("User Login");

model.AddActor("Student");                 // stick figure
model.AddLifeline("Login Screen");         // plain object box
model.AddLifeline("Validate User");
model.AddDatabase("Database");             // cylinder
model.SetActivateOnStart("Student", true); // execution bar from the top

model.AddSynchronousMessage("Student", "Login Screen", "click on login button");
model.AddSynchronousMessage("Login Screen", "Validate User", "ValidateUser(userid, password)");
model.AddSynchronousMessage("Validate User", "Database", "checkUser()");
model.AddReturnMessage("Database", "Validate User", "userID found, password matched");
model.AddReturnMessage("Validate User", "Login Screen", "user login successful");
model.AddReturnMessage("Login Screen", "Student", "display class(upgrades)");
```

Endpoints accept a lifeline **id or name**; names resolve on insert. Every
`Add*Message` returns the new message's index — the value fragments and notes
anchor to.

### Lifelines

`SequenceLifelineKind` selects the head: `Object` (named box), `Actor`
(stick figure), `Boundary` / `Control` / `Entity` (Jacobson robustness
icons), `Database` (cylinder). A lifeline carries an optional `stereotype`
(drawn as `«stereotype»` above the name), `activateOnStart`, and a packed
RGBA presentation override (`SetLifelineColor`) that the model carries but
never interprets.

### Message kinds

| `SequenceMessageKind` | Line | Head | Notes |
|---|---|---|---|
| `Synchronous` | solid | filled triangle | opens an execution bar on the receiver |
| `Asynchronous` | solid | open arrow | also opens a bar |
| `Return` | dashed | open arrow | closes the sender's innermost bar |
| `Create` | dashed | open arrow | points at the target's head box, drawn at the message row |
| `Destroy` | solid | filled triangle | X at the target's lifeline end |
| `Found` | solid | filled triangle | starts at a filled circle (sender unknown) |
| `Lost` | solid | open arrow | ends at a filled circle (receiver unknown) |

`AddSelfMessage(lifeline, label)` is a synchronous message with
`fromId == toId`, rendered as a rectangular loop-back. `AddFoundMessage` /
`AddLostMessage` take only the known endpoint.

### Activations are computed, not drawn by hand

`ComputeActivations()` derives the execution bars from the message order:
sync/async/found arrival opens a bar on the receiver, a `Return` closes the
sender's innermost open bar, `Destroy` closes everything on the target,
`activateOnStart` opens a bar at the top, and a self-call nobody replied to
collapses to a one-row stub instead of running to the bottom. Bars still open
after the last message run to the bottom of the diagram. Each
`SequenceActivation` carries `lifelineId`, `startMessage`/`endMessage`
(−1 = top/bottom) and `depth` — nested bars are drawn offset to the right.

### Message numbering

`ComputeMessageNumbers(hierarchical)` returns one label per message.
Sequential is `1, 2, 3, …`; hierarchical follows the call nesting the way
Visual Paradigm numbers it — a synchronous call numbered `1.1` numbers its
nested calls `1.1.1`, `1.1.2`, and its reply also takes the next child
number:

```
click on login button   1
  ValidateUser          1.1
    checkUser           1.1.1
    return matched      1.1.1.1
  return valid          1.1.2
return welcome          1.2
```

### Combined fragments

Ranges are **inclusive message indices**:

```cpp
size_t add   = model.AddSynchronousMessage("StoreFront", "Cart", "AddItem");
// ...
size_t added = model.AddReturnMessage("Cart", "StoreFront");
model.AddLoop(add, added, "more items");

std::string alt = model.AddAlt(confirm, placed, "order accepted");
model.AddFragmentOperand(alt, "payment failed", place);   // the "else" band
```

`SequenceFragmentKind` covers `Loop`, `Alt`, `Opt`, `Par`, `Break`,
`Critical`. `Alt` and `Par` take further operands via `AddFragmentOperand`
(each starts strictly inside the range); the others have exactly one, and
`Validate()` warns when they don't. Fragments may nest; partial overlap is a
validation error.

### Notes

```cpp
model.AddNote("passwords are hashed", SequenceNotePlacement::RightOf,
              "Database", /*afterMessage*/ 2);
model.AddNoteOver("critical section", "Cart", "Inventory", 4);
```

`LeftOf` / `RightOf` hang beside the anchor lifeline; `Over` spans one or two
lifelines. `afterMessage = -1` places the note above the first message.

### Maintenance and validation

`RemoveMessage(index)` re-anchors every fragment and note behind the removed
message (a fragment whose range becomes empty is removed with it);
`RemoveLifeline` also removes every message and note touching it. `Validate()`
diagnoses — without modifying — dangling endpoints, messages to a lifeline
before its create or after its destroy, returns that end no activation,
malformed fragment ranges and operands, fragments that cross without nesting,
dangling note anchors and duplicate lifeline names, each as a
`SequenceDiagnostic` with `Info` / `Warning` / `Error` severity.

---

## 2. Text export

```cpp
#include "Plugins/Diagrams/UltraCanvasSequenceTextExport.h"

std::string plant   = SequenceTextExport::ToPlantUML(model);
std::string mermaid = SequenceTextExport::ToMermaid(model);
```

Both dialects get participant declarations (`actor` / `participant` /
`database`, …), the message arrows, `activate`/`deactivate` lines derived
from `ComputeActivations()`, `loop`/`alt`/`opt`/`par`/`break`/`critical`
blocks with guards, and notes. `SequenceTextExportOptions` toggles the title,
activations, fragments, notes, the tool's own `autonumber` directive, and the
Mermaid indent.

Dialect corners handled for you:

- A lifeline entering through a `Create` message is not pre-declared — it is
  introduced inline (`create participant …`), so both tools draw its head at
  the message row.
- Mermaid cannot activate a lifeline no message has reached, so
  `activateOnStart` bars are exported to PlantUML only, and every emitted
  Mermaid `activate` is balanced with a `deactivate` before the end.
- Mermaid has no lost/found gate syntax; those messages become `%%` comments.
- The Mermaid title travels in YAML front matter; `par` operands separate
  with `and`, `critical` ones with `option`.

---

## 3. The element

```cpp
#include "Plugins/Diagrams/UltraCanvasSequenceDiagram.h"

auto diagram = CreateSequenceDiagramWithModel("seq1", 0, 0, 900, 600,
                                              SequenceDiagramSamples::UserLogin());
window->AddChild(diagram);

diagram->SetTheme(SequenceDiagramTheme::Professional);
diagram->SetNumbering(SequenceNumbering::Hierarchical);
diagram->SetShowFrame(true);        // "sd Title" pentagon frame
diagram->SetShowFootBoxes(true);    // repeat heads at the bottom
diagram->FitView();

diagram->onMessageClick = [](size_t index, const SequenceMessage& message) {
    printf("message %zu: %s\n", index, message.label.c_str());
};
```

Simple diagrams can skip the model object entirely — the element forwards
`AddLifeline` / `AddActor` / `AddDatabase` / `AddMessage` /
`AddReturnMessage` / `AddSelfMessage` / `AddLoop` / `AddAlt`, each marking
the diagram dirty. After mutating `Model()` directly, call
`MarkModelChanged()`.

### Themes

`SequenceDiagramTheme`: `Default` (neutral blue-grey), `Classic` (golden
head boxes, hairline arrows), `Professional` (teal), `Colorful` (a distinct
accent per lifeline), `Minimal`, `Dark`. A theme is just a preset filling
`SequenceDiagramStyle`, where every colour, font size and spacing knob is
settable; tweak `Style()` and call `SetStyle()` / `MarkModelChanged()`.
`ApplyPaletteByLifeline()` stamps the qualitative palette onto the model's
per-lifeline colours, usable under any theme.

### View and interaction

Mouse wheel zooms around the cursor; dragging empty canvas (any button)
pans; `FitView()` / `ResetView()` / `SetZoomLevel` / `SetPanOffset` steer
programmatically. Clicking selects messages and lifelines (hover
highlights them) and fires `onMessageClick` / `onLifelineClick` /
`onFragmentClick` / `onSelectionChanged`. `SetInteractive(false)` keeps
zoom and pan but disables selection — presentation mode.

### Layout behaviour worth knowing

- Lifeline spacing is solved from head widths **and** message label widths:
  a long label between two lifelines pushes them apart (distributed across
  the gaps it spans), so labels never overlap their neighbours.
- Arrow endpoints attach to the edge of the **deepest execution bar** at that
  row, and nested bars shift right by `activationNestingOffset`.
- A `Create` target's head drops down to its create-message row; a destroyed
  lifeline ends in an X at its destroy row.
- Fragment frames wrap everything their message range touches, and outer
  frames get more side padding than the frames they contain.

---

## 4. Samples

`SequenceDiagramSamples::` ships six ready models, one per demo tab:
`CardGame()` (loop over dealing, self message, always-active driver),
`UserLogin()`, `Authentication()` (robustness icons, async messages, notes),
`OnlineShop()` (loop + alt with two operands), `ObjectLifecycle()`
(create/destroy plus lost/found gates), and `UltraCanvasEventFlow()` — the
framework's own event pipeline, verified against
`core/UltraCanvasApplication.cpp`: XNextEvent → ConvertXEventToUCEvent →
PushEvent → DispatchEvent → FindElementAtPoint → Button::OnEvent →
onClick → UpdateAndRender → FlushToSurface, wrapped in a
`loop [while running]` fragment.

```cpp
auto flow = CreateSequenceDiagramWithModel(
    "ucFlow", 0, 0, 980, 640, SequenceDiagramSamples::UltraCanvasEventFlow());
flow->SetNumbering(SequenceNumbering::Hierarchical);
flow->SetShowFrame(true);
flow->FitView();
```

## See also

- [`UltraCanvasUMLModel.md`](UltraCanvasUMLModel.md) — the class-diagram
  model this feature's layering follows
- [`UltraCanvasClassDiagramProposal.md`](UltraCanvasClassDiagramProposal.md)
  — the UML family's roadmap
