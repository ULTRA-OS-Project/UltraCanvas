# Events and callbacks

An application reacts to the user in two ways:

1. **Callbacks** - named `std::function` members on each element
   (`onClick`, `onTextChanged`, `onSelectionChanged`, `onWindowClosing` ...).
   This is what almost all application code uses. Every element's callbacks
   are listed on its page under **Callbacks**; all of them together are in
   the [Callback index](Callback-Index.md).
2. **Events** - the raw `UCEvent` stream (mouse, keyboard, touch, drag and
   drop, window events), handled by overriding `OnEvent` in a custom element,
   by an element's generic `eventCallback`, or by a window's event filter.

## Callbacks

A callback is a public member you assign a lambda to:

```cpp
auto save = std::make_shared<UltraCanvasButton>("Save", Tr("Save"));
save->onClick = [this]() { SaveDocument(); };

auto zoom = std::make_shared<UltraCanvasSlider>("Zoom");
zoom->SetRange(10, 400);
zoom->onValueChanging = [this](float v) { PreviewZoom(v); };   // while dragging
zoom->onValueChanged  = [this](float v) { ApplyZoom(v); };     // when settled

auto name = std::make_shared<UltraCanvasTextInput>("Name");
name->onTextChanged  = [this](const std::string& text) { Validate(text); };
name->onEnterPressed = [this](const std::string& text) { Submit(text); return true; };
```

<!-- doc-check: void SaveDocument(); void PreviewZoom(float); void ApplyZoom(float); void Validate(const std::string&); void Submit(const std::string&); -->

Conventions, true across the framework:

- Names start with `on` and say what happened: `onItemClicked`,
  `onSelectionChanged`, `onValueChanging` (continuous) versus
  `onValueChanged` (final).
- A callback that returns **`bool`** lets you decide: `true` means *handled*
  or *allow* - `onEnterPressed` returning `true` consumes the key,
  `onWindowClosing` returning `false` keeps the window open,
  `onPopupAboutToClose` returning `false` keeps a popup open.
- An unset callback is simply not called; assigning `nullptr` removes one.
- Some elements also have a setter (`SetOnClick(...)`), a builder method
  (`ButtonBuilder::OnClick(...)`), or a *provider* callback that the element
  calls to ask for data rather than to report an event - `tooltipProvider`,
  `tickLabelFormatter`, `hoverText`. These are listed with the callbacks.
- Callbacks are always called on the UI thread.

### Callbacks every element has

Inherited from [`UltraCanvasUIElement`](Elements/UltraCanvasUIElement.md):

| Callback | Signature | Use |
|---|---|---|
| `eventCallback` / `SetEventCallback` | `bool(const UCEvent&)` | Sees every event the element receives before its default handling; return `true` to consume it |
| `onPopupOpened` | `void()` | The element was opened as a popup (`OpenPopup`) |
| `onPopupAboutToClose` | `bool(ClosePopupReason)` | The popup is about to close (click outside, Escape, manual ...); `false` keeps it |
| `onPopupClosed` | `void(ClosePopupReason)` | The popup has closed |

Containers add `SetScrollChangedCallback`, `SetChildAddedCallback` and
`SetChildRemovedCallback` ([Container](Elements/UltraCanvasContainer.md#callbacks));
windows add `onWindowClosing`, `onWindowClosed`, `onWindowResize`,
`onWindowMove`, `onWindowFocus`, `onWindowBlur`, `onWindowMinimize`,
`onWindowMaximize`, `onWindowRestore`, `onWindowShow`, `onWindowHide`,
`onFrameRendered` ([WindowBase](Elements/UltraCanvasWindowBase.md#callbacks)).

## Ownership

**A callback must never capture a `shared_ptr` to the element it is installed
on, or to any container above it.** The tree owns its elements; a lambda that
owns one of them back closes a cycle, and the whole window is never freed.
Capture raw:

```cpp
class EditorWindow {
public:
    void Build() {
        auto status = std::make_shared<UltraCanvasLabel>("Status", "");
        auto clear  = std::make_shared<UltraCanvasButton>("Clear", Tr("Clear"));

        UltraCanvasLabel* statusRaw = status.get();      // raw: the window owns it
        clear->onClick = [this, statusRaw]() {           // `this` is the window class: fine
            statusRaw->SetText(Tr("Cleared"));
            ClearDocument();
        };
        // WRONG: clear->onClick = [status]() { ... };    a shared_ptr in the lambda
    }
    void ClearDocument();
};
```

`scripts/check_callback_cycles.py` finds the wrong form; CI runs it on every
pull request.

When a callback can outlive the element it touches (work posted from a
thread, a timer, a network reply), capture a `std::weak_ptr` and lock it, or
cancel the work when the element goes away.

## Events

### How an event travels

1. The platform layer turns native input into a `UCEvent` and queues it.
2. `UltraCanvasApplicationBase::DispatchEvent` runs the application-wide
   handlers, finds the target window, and blocks input to other windows while
   a modal dialog is open.
3. The window's **event filters** (`InstallEventFilter(id, filter, types)`)
   see the event; a filter returning `true` consumes it.
4. Mouse events go to the element that **captured** the pointer (a drag in
   progress) or otherwise to the element under the pointer; keyboard and text
   events go to the **focused** element.
5. The element's `OnEvent` runs - by default it calls `eventCallback`. If it
   returns `false`, the event **bubbles** up to the parent container, and so
   on up to the window.

### `UCEvent`

| Field | Meaning |
|---|---|
| `type` | the `UCEventType` (below) |
| `pointer`, `pointerWindow`, `pointerGlobal` | position in element, window and screen coordinates |
| `button` | `UCMouseButton::Left`, `Middle`, `Right` ... |
| `wheelDelta` | wheel or zoom delta |
| `virtualKey`, `nativeKeyCode`, `character`, `text` | the key: cross-platform `UCKeys`, the platform code, the character, the typed / IME text |
| `ctrl`, `shift`, `alt`, `meta` | modifier keys |
| `compositionCursor` | IME pre-edit caret (with `TextComposition`) |
| `pressure`, `pointerId`, `touchPointCount`, `scale`, `rotation` | touch and gestures |
| `dragData`, `dragMimeType`, `droppedFiles` | drag and drop payload |
| `width`, `height` | new size (window resize) |
| `timestamp`, `targetElement`, `targetWindow` | when and where |

Helpers: `IsMouseEvent()`, `IsMouseClickEvent()`, `IsKeyboardEvent()`,
`IsWindowEvent()`, `IsTouchEvent()`, `IsDragEvent()`.

### Event types (`UCEventType`)

| Family | Types |
|---|---|
| Mouse | `MouseDown`, `MouseUp`, `MouseMove`, `MouseEnter`, `MouseLeave`, `MouseWheel`, `MouseWheelHorizontal`, `MouseDoubleClick` |
| Keyboard | `KeyDown`, `KeyUp`, `TextInput`, `TextComposition` |
| Window | `WindowCloseRequest`, `WindowResize`, `WindowMove`, `WindowMinimize`, `WindowRestore`, `WindowFocus`, `WindowBlur`, `WindowRepaint` |
| Touch | `TouchStart`, `TouchMove`, `TouchEnd`, `Tap`, `PinchZoom` |
| Focus | `FocusGained`, `FocusLost` |
| Drag and drop | `DragStart`, `DragEnter`, `DragLeave`, `DragOver`, `Drop` |
| Other | `Clipboard`, `Selection`, `Timer`, `Custom`, `Redraw` |
| Commands | `ButtonClick`, `DropdownSelect`, `MenuClick` (between `CommandEventsStart` and `CommandEventsEnd`) |

### Handling raw events

On any element, without subclassing:

```cpp
canvas->SetEventCallback([](const UCEvent& e) {
    if (e.type == UCEventType::KeyDown && e.virtualKey == UCKeys::Escape) {
        // ...
        return true;                   // consumed
    }
    return false;                      // default handling, then bubbling
});
```

<!-- doc-check: std::shared_ptr<UltraCanvasContainer> canvas; -->

In a custom element, override `OnEvent` (and call the base class for what
you do not handle). See [Rendering](Rendering.md#a-custom-element).

For a whole window - keyboard shortcuts, for example:

```cpp
window->InstallEventFilter("Shortcuts", [](const UCEvent& e) {
    return e.type == UCEventType::KeyDown && e.ctrl && e.virtualKey == UCKeys::S;   // Ctrl+S
}, {UCEventType::KeyDown});
```

<!-- doc-check: std::shared_ptr<UltraCanvasWindow> window; -->

Most applications define commands once in `UltraCanvasCommandRegistry` and
bind them to menus, toolbars and keys rather than filtering keys by hand.

## Focus and the keyboard

`element->SetFocus(true)` (or `window->SetFocusedElement(element)`) moves the
keyboard focus; `onFocusGained` / `onFocusLost` (on inputs) and the
`FocusGained` / `FocusLost` events report it. Tab and Shift+Tab move between
elements that accept focus. Dialog keyboard conventions (Enter, Escape,
mnemonics) are described in
[`UltraCanvasDialogKeyboard.md`](../UltraCanvas/UltraCanvasDialogKeyboard.md).

## Timers and the main thread

```cpp
auto* app = UltraCanvasApplicationBase::GetCurrent();
TimerId tick = app->StartTimer(1000, true, [](TimerId) { /* every second */ });
app->StopTimer(tick);
app->PostToUIThread([]() { /* runs on the UI thread at the next turn of the loop */ });
```

Timers fire on the UI thread. `PostToUIThread` is the one call a worker thread
may make to reach the UI.

## Drag and drop

Elements receive `DragEnter` / `DragOver` / `DragLeave` / `Drop` with the
payload in `dragData` / `droppedFiles`. Dragging files *out* of the
application is `window->StartNativeFileDrag(paths, onFinished)`.

Next: [Styling and themes](Styling-And-Themes.md)
