# UltraCanvasClassDiagram

UML class diagram element: compartment boxes sized from their members, and
typed relationships whose line style and end decorations follow the UML rules.

- Header: `include/Plugins/Diagrams/UltraCanvasClassDiagram.h`
- Source: `Plugins/Diagrams/UltraCanvasClassDiagram.cpp`
- Layout: `include/Plugins/Diagrams/UltraCanvasClassLayout.h`
- Demo: `Apps/DemoApp/UltraCanvasClassDiagramExamples.cpp` (Diagrams → Class Diagram)
- Tests: `Tests/ClassLayoutTest.cpp` (target `ClassLayoutTest`)
- Version: 1.0.0

## The three layers

| Layer | Owns | Needs a window? |
|---|---|---|
| [`UltraCanvasUMLModel`](UltraCanvasUMLModel.md) | Meaning — what the classes and edges *are* | No |
| `UltraCanvasClassLayout` | Geometry — where the boxes *go* | No |
| `UltraCanvasClassDiagram` | Appearance — measuring and drawing | Yes |

Only the element needs a render context. Feeding it a model built by the
[C++ reverse engineer](UltraCanvasUMLModel.md#3-c-reverse-engineering) is
therefore just `SetModel`.

## Quick start

```cpp
#include "Plugins/Diagrams/UltraCanvasClassDiagram.h"

auto diagram = CreateClassDiagram("bank", 40, 116, 900, 560);
diagram->SetTheme(ClassDiagramTheme::Blueprint);
diagram->SetTitle("Class Diagram for a Banking System");

diagram->AddClass("Bank", {"+BankId: int", "+Name: string"}, {});
diagram->AddClass("Teller", {"+Id: int"}, {"+CollectMoney()", "+OpenAccount()"});
diagram->AddAbstractClass("Account", {"+Id: int", "+CustomerId: int"}, {});
diagram->AddClass("Checking", {"+Id: int"}, {});

diagram->AddGeneralization("Checking", "Account");        // hollow triangle at Account
diagram->AddAggregation("Bank", "Teller")                 // hollow diamond at Bank
        .SetMultiplicity("1", "1..*");

diagram->SetLayout(ClassLayoutKind::Layered, ClassLayoutDirection::TopToBottom);
diagram->RunLayout();
container->AddChild(diagram);
```

Boxes are measured with the real render context on the first paint, so a box is
exactly as wide as its widest member row. `RunLayout()` only marks the diagram
dirty — the measure/layout/route work happens on the next frame, so you never
have to sequence it by hand.

## Relationships

Argument order follows the notation, and the kind alone decides how the edge is
drawn:

| Call | Line | Decoration |
|---|---|---|
| `AddAssociation(a, b)` | solid | none |
| `AddDirectedAssociation(a, b)` | solid | open arrow at `b` |
| `AddAggregation(whole, part)` | solid | hollow diamond at `whole` |
| `AddComposition(whole, part)` | solid | filled diamond at `whole` |
| `AddGeneralization(child, parent)` | solid | hollow closed triangle at `parent` |
| `AddRealization(impl, interface)` | dashed | hollow closed triangle at `interface` |
| `AddDependency(client, supplier, stereotype)` | dashed | open arrow at `supplier` |

Each returns a fluent handle, so an edge carrying all six UML labels stays
readable:

```cpp
diagram->AddAssociation("ServiceMetadata", "ContactInformation")
        .SetName("contactInformation", UMLReadingDirection::SourceToTarget)
        .SetMultiplicity("1", "0..1")
        .SetRoles("+service", "+contactInformation");
```

Lines are routed by the shared
[`UltraCanvasDiagramRouter`](UltraCanvasDiagramRouting.md): obstacle-aware
orthogonal paths with rounded elbows. When several relationships meet the same
face of a box, their anchors are spread along that face so parallel arrows do
not overlap.

## Themes and styling

`SetTheme` fills in `ClassDiagramStyle`, and every field of it is settable
afterwards:

| Theme | Look |
|---|---|
| `Default` | neutral grey headers on white |
| `Professional` | muted blue-grey, thin borders |
| `Blueprint` | yellow header band, navy border, graph paper |
| `Colorful` | a distinct hue per class |
| `DataType` | pale blue boxes for `«DataType»` models |
| `Minimal` | hairline borders, no fills |
| `Dark` | dark canvas and boxes |

Stereotype rules colour boxes by what they are — `«interface»` orange,
`«DataType»` blue, `«enumeration»` teal — and are matched before the theme:

```cpp
ClassStereotypeStyle rule;
rule.stereotype = "service";
rule.headerColor = Color(120, 200, 160, 255);
rule.hasHeaderColor = true;
diagram->AddStereotypeStyle(rule);
```

`ApplyPaletteByClassifier()` and `ApplyPaletteByPackage()` assign colours
automatically; `SetClassifierColors(name, header, body)` overrides one class.

## Rendering rules

- Abstract classes: italic name. Abstract (pure virtual) operations: italic row.
- Static members: underlined.
- Derived attributes: `/name`.
- Visibility: `+ - # ~`; a member with no glyph renders without one.
- An empty attribute or operation compartment is still drawn
  (`style.drawEmptyCompartments`, on by default) — reference UML diagrams do.
- Rows too wide for their box are truncated with an ellipsis on a UTF-8
  boundary.
- `SetDetailLevel` switches between `Full`, `Signatures` (no parameter lists)
  and `NameOnly`; `SetClassifierCollapsed` collapses a single class to a badge.

## Layout

| Kind | Use for |
|---|---|
| `Layered` | The default. Generalization edges define the ranking, so parents sit above children; ordering within a rank is barycentre-based to cut crossings |
| `Tree` | Pure hierarchies — layered plus centring each parent over its children |
| `Grid` | Models with little or no inheritance, where `Layered` would put everything in one rank |
| `Manual` | Positions you set yourself |

`ClassLayoutDirection` gives `TopToBottom`, `BottomToTop`, `LeftToRight` and
`RightToLeft`. `LayoutOptions()` exposes node/rank separation, margins,
crossing-reduction passes and grid columns.

Dragging a box pins it; a later `RunLayout()` places everything else around it.

> A model with no hierarchical edges has every class at rank 0, which `Layered`
> draws as one very wide row. Use `Grid` for those — the reverse-engineering
> scenario in the demo does exactly this.

## Interaction

Zoom (wheel, at the cursor), pan (drag empty canvas or middle-drag), drag a box
to move it, double-click to collapse or expand, click to select. Per-row hit
testing reports the exact member:

```cpp
diagram->onMemberClick = [&](const std::string& classifierId,
                             UMLMemberKind kind, size_t index) { ... };
diagram->onClassClick = ...;
diagram->onRelationshipClick = ...;
diagram->onSelectionChanged = ...;
diagram->onCanvasRightClick = ...;
```

`SetInteractive(false)` gives a presentation mode where zoom and pan still work
but nothing can be moved.

## Demo scenarios

Diagrams → Class Diagram, five buttons:

1. **Banking System** — generalization, aggregation and composition, per-end
   multiplicities, Blueprint theme, empty compartments drawn.
2. **Interfaces** — `«interface»` stereotypes, dashed realization and
   dependency, a collapsed name-only badge, an enumeration, a static operation.
3. **Service Metadata** — `«DataType»` boxes, member-level multiplicity, and an
   edge carrying an association name plus both role names and both
   multiplicities.
4. **Domain Model** — a colour per class, named associations, left-to-right
   layout.
5. **Reverse Engineered** — a model built at runtime by the C++ header scanner
   over four of the framework's own headers, coloured per source file.

## Known limitations

- End labels (multiplicities and role names) are drawn before the boxes, so on
  a dense diagram a label placed close to a box can be covered by it.
  Collision-aware placement is proposal item R11.
- Self-relationships (R12), packages and frames (K1–K4), notes (K7),
  association classes (M15) and n-ary associations (R13) are not drawn yet; the
  model carries them.
- Editing is limited to moving and collapsing. Creating and reconnecting edges,
  undo/redo and the clipboard are proposal items I5–I12.

## See also

- [`UltraCanvasClassDiagramProposal.md`](UltraCanvasClassDiagramProposal.md) — the full feature list and roadmap
- [`UltraCanvasUMLModel.md`](UltraCanvasUMLModel.md) — model, notation grammar, C++ reverse engineering, text export
- [`UltraCanvasDiagramRouting.md`](UltraCanvasDiagramRouting.md) — the shared orthogonal router
