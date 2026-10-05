# UltraCanvasAccessibility

What assistive technology — screen readers, magnifiers, braille displays —
needs to know about an element, in a form that does not depend on the
platform: its **role**, its **name**, for text a **text interface** (content,
caret, selection, character boxes, formatting, words, lines, sentences) and an
**event stream** saying what changed. A platform bridge hands this to the
operating system, where screen readers find it: **AT-SPI** on Linux (Orca)
and **UI Automation** on Windows (Narrator, NVDA, JAWS) - see
[Platform bridges](#platform-bridges). There is no macOS bridge
(NSAccessibility) yet. The same layer serves tests and in-application readers
(read aloud, a braille panel).

Header: `UltraCanvasAccessibility.h` (included by `UltraCanvasUIElement.h`).

## Elements

```cpp
AccessibleRole role = element->GetAccessibleRole();   // Unknown by default
std::string name = element->GetAccessibleName();
IAccessibleText* text = element->GetAccessibleTextInterface();   // null without text
bool secret = element->IsAccessiblePassword();        // false by default
```

An element describes itself by overriding these virtuals of
`UltraCanvasUIElement`. `UltraCanvasRichTextEdit` does: role `Document`, name
the document's title (else the element's identifier), and a text interface
over the whole document. `UltraCanvasTextInput` (and what is built on it) is a
`TextField`, without a text interface yet.

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

**Testing.** `Tests/AtspiBridgeTest` runs a libatspi client - the library Orca
uses - against a test application on a private accessibility bus: the tree,
names, roles, states, text, words, sentences, attributes, extents, hit
testing, moving the caret, and the insert/delete events of typing. It is built
when the `atspi-2` development package is installed and skips itself without a
display or at-spi2-core. The UI Automation bridge has no automated test yet:
so far it is compile-checked (MinGW GCC and Clang) but has not been run
against Narrator, NVDA or JAWS.
