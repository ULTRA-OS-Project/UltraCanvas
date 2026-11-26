# Core concepts

The handful of ideas every UltraCanvas program is built from.

## The application

Exactly one `UltraCanvasApplication` exists per process. It is constructed in
`main`, initialised with the application's name, and runs the event loop:

```cpp
UltraCanvasApplication app;
if (!app.Initialize("MyApp")) return 1;   // the name keys settings, catalogues, single instance
// ... create windows ...
app.Run();                                // returns after RequestExit() or the last window closes
```

Anywhere else, `UltraCanvasApplicationBase::GetCurrent()` returns it. It owns
the timers (`StartTimer`, `StopTimer`), the queue for work posted from other
threads (`PostToUIThread`), the clipboard and the list of windows; the
`onApplicationExit` callback runs as it shuts down. Full reference:
[ApplicationBase](Elements/UltraCanvasApplicationBase.md).

## Windows

A window is created from a `WindowConfig` - title, size, position, type,
resizability, modality, background, icon and more - with `CreateWindow`, and
shown with `Show()`:

```cpp
WindowConfig config;
config.title = "MyApp 1.0.0";       // applications show their version in the title
config.width = 1024;
config.height = 700;
config.minWidth = 640;
auto window = CreateWindow(config);
window->onWindowClosing = []() { return true; };   // false keeps the window open
window->Show();
```

`UltraCanvasWindowBase` is a **container**: you `AddChild` elements to it and
give it a `layout`. It also manages focus (`SetFocusedElement`), popups
(`OpenPopup` / `ClosePopup` for menus, dropdown lists, pickers), the busy
pointer, event filters and native drag and drop. Dialogs
(`UltraCanvasModalDialog`) are windows too. Full reference:
[WindowBase](Elements/UltraCanvasWindowBase.md) and the group
[Application, windows and dialogs](Groups/Application-Windows-And-Dialogs.md).

## Elements

Everything inside a window is an **element**: a class derived from
[`UltraCanvasUIElement`](Elements/UltraCanvasUIElement.md). Every element has

- an **identifier** (see below),
- a box in the layout: size, margin, padding, border, constraints
  (see [Layout](Layout.md)),
- state: visible, disabled, hovered, pressed, focused, selected
  (`SetVisible`, `SetDisabled`, `SetFocus`, `IsFocused` ...),
- a tooltip (`SetTooltip`, or rich `SetTooltipContent`),
- an accessible role, name and value for screen readers (`SetAccessibleName`),
- `Render(IRenderContext*, dirtyRect)` to draw itself and `OnEvent(const UCEvent&)`
  to handle input, both of which a custom element overrides,
- its own **callbacks** (`onClick`, `onTextChanged` ...) and a generic
  `eventCallback` - see [Events and callbacks](Events-and-Callbacks.md),
- a style struct and `ApplyTheme` - see [Styling and themes](Styling-And-Themes.md).

An element that holds other elements is an
[`UltraCanvasContainer`](Elements/UltraCanvasContainer.md): `AddChild`,
`RemoveChild`, `ClearChildren`, `FindChildById`, built-in scrollbars, and the
`layout` that places its children.

## Identifiers

**The first string an element is constructed with is its identifier, never its
text.** The text, where the element has one, comes second or from `SetText`:

```cpp
auto title  = std::make_shared<UltraCanvasLabel>("InfoTitle", Tr("About"));   // id, text
auto anon   = std::make_shared<UltraCanvasLabel>("", Tr("Version 1.0"));       // no id needed
auto row    = std::make_shared<UltraCanvasContainer>("Row");                   // id only
auto button = CreateButton("Ok", 0, 0, 104, 30, Tr("OK"));                     // id, x, y, w, h, text
```

There is no numeric id on any constructor or factory. Identifiers are what
`FindChildById` looks up, what the layout audit and debugging tools print, and
what tests address. `scripts/check_label_identifiers.py` refuses a label,
checkbox, radio or switch built from a translated text alone, because it would
compile and show nothing.

## Creating elements: the three constructor shapes

| Constructor | Example | Meaning for the layout |
|---|---|---|
| `(id, x, y, w, h, ...)` with a non-zero `x` or `y` | `UltraCanvasButton("b", 10, 10, 80, 30)` | pinned at (x, y), out of the flow |
| `(id, w, h, ...)` or `(id, 0, 0, w, h, ...)` | `UltraCanvasButton("b", 80, 30, "OK")` | in the flow, fixed size |
| `(id)` or `(id, text)` | `UltraCanvasButton("b", "OK")` | in the flow, sized by its content |

A `0` width or height means "automatic" on that axis. Most elements also have
a `Create<Name>(...)` factory returning a `std::shared_ptr`; both are listed
under **Creating it** on each element page.

## Ownership

Elements are `std::shared_ptr`-managed. A container holds its children; a
window holds its tree. Hold your own `shared_ptr` to an element only as long
as you need to call it - usually as a member of your window class.

Because the tree owns its elements, **a callback must never capture a
`shared_ptr` to the element it is installed on or to any container above it**:
that closes a cycle and nothing is ever freed. Capture a raw pointer (or
`this` of the owning window class). `scripts/check_callback_cycles.py`
enforces this in CI. Details: [Events and callbacks](Events-and-Callbacks.md#ownership).

## Placing a child inside a self-drawn view

A view that paints its own content (a list, a chart, a file manager) and needs
a real editable field or button over it adds that element as a child and
positions it every frame with `UltraCanvasContainer::PlaceChildAt(child, rect)`
- **not** `SetBounds()`, which the next layout pass undoes.

## Never hand-paint a control

If it takes input, shows a picture or presents a value, it is an element.
Painting one with `DrawText` / `FillRoundedRectangle` plus a private buffer,
caret or `hovered` flag loses caret and selection, clipboard, undo, IME,
keyboard focus, theming, DPI scaling and accessibility.
`scripts/check_ui_reuse.py` looks for exactly that. When the element you need
does not exist, add one: a class under `UltraCanvas/{include,core}`, a doc, and
a row in the catalogue - then it appears in this wiki too.

## Plug-in elements

Charts, diagrams, codes, document and vector views live in
`UltraCanvas/include/Plugins/` and `UltraCanvas/Plugins/`. They are built into
the same library and used exactly like the core elements. Some heavy formats
load on demand through the element plug-in system
(`UltraCanvasElementPlugins.h`,
[doc](../UltraCanvas/UltraCanvasElementPlugins.md)).

## Threads

The UI runs on one thread. Elements, windows and the render context are only
touched from it. Work on another thread hands its result back with
`PostToUIThread`:

```cpp
std::thread([label = label.get()]() {
    std::string result = ComputeSomethingSlow();
    if (auto* app = UltraCanvasApplicationBase::GetCurrent())
        app->PostToUIThread([label, result]() { label->SetText(result); });
}).detach();
```

<!-- doc-check: std::shared_ptr<UltraCanvasLabel> label; std::string ComputeSomethingSlow(); -->

(If the label can be destroyed before the result arrives, check that it still
exists - for example through a `weak_ptr` - before using it.)

Next: [Layout](Layout.md)
