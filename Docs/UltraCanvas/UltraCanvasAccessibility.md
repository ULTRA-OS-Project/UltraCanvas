# UltraCanvasAccessibility

What assistive technology — screen readers, magnifiers, braille displays —
needs to know about an element, in a form that does not depend on the
platform: its **role**, its **name**, for text a **text interface** (content,
caret, selection, character boxes, formatting, words, lines, sentences) and an
**event stream** saying what changed. A platform bridge hands this to the
operating system, where screen readers find it: **AT-SPI** on Linux (Orca),
**UI Automation** on Windows (Narrator, NVDA, JAWS) and **NSAccessibility** on
macOS (VoiceOver) - see [Platform bridges](#platform-bridges). The same layer
serves tests and in-application readers (read aloud, a braille panel).

Header: `UltraCanvasAccessibility.h` (included by `UltraCanvasUIElement.h`).

## Elements

```cpp
AccessibleRole role = element->GetAccessibleRole();   // Unknown by default
std::string name = element->GetAccessibleName();
std::string help = element->GetAccessibleDescription();          // the tooltip by default
IAccessibleText* text = element->GetAccessibleTextInterface();   // null without text
bool secret = element->IsAccessiblePassword();        // false by default
AccessibleToggleState state = element->GetAccessibleToggleState();   // NotToggleable, Off, On, Mixed
AccessibleRange range;                                // value, minimum, maximum, step, readOnly
bool hasValue = element->GetAccessibleRange(range);
std::string value = element->GetAccessibleValueText();          // a combo box's item, a field's text
std::string action = element->GetAccessibleActionName();        // "press", "toggle", "select" or ""
element->DoAccessibleAction();                        // as a click would
element->SetAccessibleValue(70);                      // a slider, a spin button
element->SetAccessibleValueText("Ada");               // a text field
```

An element describes itself by overriding these virtuals of
`UltraCanvasUIElement`. Whatever an element says, an application can name it
and describe it: `SetAccessibleName("Bold")` for an icon button, or for a field
whose label is a separate element; `SetAccessibleDescription(...)` for help
beyond the tooltip.

**What the widgets say.**

| Element | Role | Name | Also |
|---|---|---|---|
| `UltraCanvasButton` | `Button` | text, else tooltip (icon button) | action *press*; a toggle button: toggle state, action *toggle* |
| `UltraCanvasCheckbox` | `CheckBox` | label | toggle state (Mixed when indeterminate), action *toggle* |
| `UltraCanvasRadio` | `RadioButton` | label | toggle state, action *select* |
| `UltraCanvasSwitch` | `Switch` | label | toggle state, action *toggle* |
| `UltraCanvasLabel` | `Label` | text | |
| `UltraCanvasTextInput` | `TextField` | placeholder | value text = content (never in password mode), settable |
| `UltraCanvasDropdown` | `ComboBox` | `SetAccessibleName` | value text = the shown selection |
| `UltraCanvasSlider` | `Slider` | `SetAccessibleName` | range, settable (no single value in range mode) |
| `UltraCanvasSpinner` | `SpinButton` | `SetAccessibleName` | range, settable; value text = what it shows |
| `UltraCanvasBusyIndicator` | `ProgressBar` | `SetAccessibleName` | indeterminate (no range) |
| `UltraCanvasGroupBox` | `Group` | title | |
| `UltraCanvasTabbedContainer` | `TabList` | open tab's title | |
| `UltraCanvasToolbar` | `Toolbar` | `SetAccessibleName` | its buttons are its children |
| `UltraCanvasListView` / `UltraCanvasTreeView` | `List` / `Tree` | `SetAccessibleName` | rows and nodes are drawn, not elements: not yet reachable one by one |
| `UltraCanvasImageElement` | `Image` | `SetAccessibleName` (its alt text) | |
| `UltraCanvasMenu` | `Menu` | | items are drawn, not elements |
| `UltraCanvasRichTextEdit` | `Document` | the document's title | the full text interface |

A changed name, toggle state or value is announced (`NameChanged`,
`StateChanged`, `ValueChanged`), so a screen reader hears it without the
user moving.

**Password fields.** `IsAccessiblePassword()` is true for a field whose content
is a secret - `UltraCanvasTextInput` in password mode, whether or not its text
is revealed. The bridges report it as a password field (UI Automation's
`IsPassword`, AT-SPI's *password text* role), so a screen reader says
"password" and echoes stars instead of the characters typed, and other
assistive tools leave the content alone. An element that answers true must not
hand out its text through `GetAccessibleTextInterface()`.

## Text

Offsets count characters (Unicode code points), not bytes.

```cpp
std::string all = text->GetAccessibleText();
int caret = text->GetCaretOffset();
text->SetSelection(10, 20);
int start = 0, end = 0;
std::string word = text->GetTextAtOffset(caret, AccessibleTextBoundary::Word, start, end);
AccessibleTextAttributes look = text->GetAttributesAt(caret, start, end);   // bold, heading level, link, ...
Rect2Df box = text->GetCharacterBounds(caret);                              // window coordinates
int under = text->GetOffsetAtPoint(Point2Df(120, 80));
```

Words, sentences and paragraphs come from the text
(`UltraCanvasAccessibility::TextUnitAt`); an element can override
`GetTextAtOffset` for units only its layout knows — the rich text element does
so for lines. In the rich text element the text is the paragraphs one per line,
a table's cells separated by tabs and its rows by line breaks; attributes include
headings, list items, links, tracked insertions and deletions and comments.

## Events

```cpp
int id = UltraCanvasAccessibility::AddListener([](const AccessibilityEvent& e) {
    // e.type: FocusChanged, TextChanged, CaretMoved, SelectionChanged, NameChanged
    // e.element, e.offset (caret for CaretMoved), e.length
});
UltraCanvasAccessibility::RemoveListener(id);
```

The window announces `FocusChanged` when an element gains the focus; the rich
text element announces edits, caret moves and selection changes. Every element
announces `ElementDestroyed` from its destructor, so a bridge can drop the id
it gave it. Elements build events only while someone listens
(`HasListeners()`), so the layer costs nothing otherwise. Listeners are called
on the thread that made the change (the UI thread).

`IAccessibleText::IsReadOnly()` (default false) tells a screen reader not to
announce the text as editable; the rich text element answers its
`IsReadOnly()`.

## Helpers

`CharacterCount`, `ByteOffsetOfCharacter` and `CharacterOffsetOfByte` convert
between the characters of the accessibility API and UTF-8 byte offsets.

## Platform bridges

Nothing to call: the application starts the bridge for its platform, and an
element that describes itself (the three virtuals above) is reachable.

**The tree.** The application holds its windows; a window holds its elements
in container order. An element that has not described itself
(`AccessibleRole::Unknown`) still appears - as a panel when it holds others,
so a reader walks into it, else as a filler (AT-SPI) or custom control that is
neither control nor content (UI Automation). Names are `GetAccessibleName()`,
a window's title for a window. Positions are the element's place on the
screen: window coordinates scaled by the window's device scale, from the
window's content origin (`UltraCanvasWindowBase::GetContentScreenOrigin()`,
below the title bar on Windows).

**AT-SPI (Linux)** - `OS/Linux/UltraCanvasLinuxAccessibility`. The application
registers on the accessibility bus with the registry (`Socket.Embed`) and
answers there for the application (`Accessible`, `Application`), its windows
(frames) and its elements (`Accessible`, `Component`, and `Text` for elements
with text). Roles map to AT-SPI roles (`Document` → document text, `TextField`
→ entry, or password text for a password field, `Button` → push button, ...); states include enabled, visible,
showing, focusable, focused, editable or read-only, single/multi-line and, for
a window, active. Text attributes use the names ATK and Orca read: `weight`,
`style`, `underline`, `strikethrough`, `text-position`, `family-name`, `size`,
`fg-color`, `bg-color`, `invalid` (spelling), plus `heading-level`,
`list-item`, `link`, `revision` and `comment`. Events: focus
(`StateChanged:focused` and `Focus`), window activation, `TextCaretMoved`,
`TextSelectionChanged` and `TextChanged` as `delete`/`insert` with the text,
worked out from the text the bridge last sent.

It connects when the desktop has accessibility on (`org.a11y.Status`
`IsEnabled` or `ScreenReaderEnabled`, which GNOME sets when Orca starts), and
later when it is turned on; without a session bus it does nothing.
`NO_AT_BRIDGE=1` keeps it off, `UC_ACCESSIBILITY_ALWAYS_ON=1` connects
regardless. D-Bus is GIO's (already there through GTK); its calls are answered
on the UI thread between events, through an fd watch on the event loop, with
no thread of its own.

**UI Automation (Windows)** - `OS/MSWindows/UltraCanvasWindowsAccessibility`.
Each window answers `WM_GETOBJECT` with a fragment root; every element is a
fragment (control type, name, automation id = identifier, framework
"UltraCanvas", enabled, focusable, focused, offscreen, is-password, bounds, navigation,
`SetFocus`, hit testing). An element with text offers the **Text pattern**:
document, selection and point ranges that move and expand by character,
format run, word, line, paragraph and document, find text, select, report one
rectangle per line, and give font name and size, weight, italic, colours,
super/subscript, underline/strikethrough, heading style ids, read-only and
spelling/comment/insertion/deletion annotations (mixed where the runs of a
range differ). Focus, text and caret/selection changes raise the matching
events. `UIAutomationCore.dll` is loaded at run time, and the bridge does
nothing until a client sends the first `WM_GETOBJECT`.

**Widgets on AT-SPI and UI Automation.** Toggle states become AT-SPI's
checkable / checked / indeterminate (and pressed for a toggle button) and UI
Automation's Toggle pattern (SelectionItem for a radio button). A range is
AT-SPI's `Value` interface and UI Automation's RangeValue pattern, both
settable. A value text is UI Automation's Value pattern; on AT-SPI a text
field's or combo box's value is read through `Text`, the way Orca reads an
entry. The default action is AT-SPI's `Action` interface (one action, its
name) and UI Automation's Invoke pattern. Descriptions are AT-SPI's
`Description` and UI Automation's `HelpText`. Changes become
`StateChanged:checked`, `PropertyChange:accessible-value` and text
insert/delete signals (AT-SPI) and property-changed events for ToggleState,
IsSelected, RangeValue.Value, Value.Value and Name (UI Automation).

**NSAccessibility (macOS)** - `OS/MacOS/UltraCanvasMacOSAccessibility`. A
window's content view (`UltraCanvasView`) is the way in: its accessibility
children are the window's elements, it hit-tests and reports the focused
element, and every element is a `UCAccessibilityElement`
(`NSAccessibilityElement`) kept for its lifetime. Roles map to AppKit roles
(`Button` → AXButton, `CheckBox` → AXCheckBox, `Switch` → AXCheckBox/AXSwitch,
`TextField` → AXTextField or AXSecureTextField for a password, `Document` →
AXTextArea, `ComboBox` → AXPopUpButton, `Slider` → AXSlider, `SpinButton` →
AXIncrementor, `ProgressBar` → AXProgressIndicator or AXBusyIndicator, ...).
Elements that never described themselves are left out and their children
lifted into their place. Label, help, value (toggle state as 0/1/2, range
value, or the text), min/max, settable value, AXPress, increment/decrement,
focus, frames in screen coordinates, and for text the parameterised
attributes VoiceOver uses - character count, selected range and text,
insertion-point line, string/line/range/style range/frame for a range or
position - with NSString (UTF-16) ranges converted to the code points of
`IAccessibleText`. Changes post the matching notifications (focused element,
value, selected text, title, element destroyed). The bridge starts listening
when VoiceOver first asks a window for its children.

**Testing.** `Tests/AtspiBridgeTest` runs a libatspi client - the library Orca
uses - against a test application on a private accessibility bus: the tree,
names, roles, states, text, words, sentences, attributes, extents, hit
testing, moving the caret, the insert/delete events of typing, and the common
widgets - pressing a button, ticking a checkbox, reading and setting a
slider, reading a text field. It is built when the `atspi-2` development
package is installed and skips itself without a display or at-spi2-core.
`Tests/WidgetAccessibilityTest` checks every widget's answers headless. The
UI Automation and NSAccessibility bridges have no automated test yet: they
are compile-checked (UI Automation with MinGW GCC and Clang, NSAccessibility
by the macOS CI build) but have not been run against Narrator, NVDA, JAWS or
VoiceOver.
