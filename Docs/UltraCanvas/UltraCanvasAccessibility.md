# UltraCanvasAccessibility

What assistive technology — screen readers, magnifiers, braille displays —
needs to know about an element, in a form that does not depend on the
platform: its **role**, its **name**, for text a **text interface** (content,
caret, selection, character boxes, formatting, words, lines, sentences) and an
**event stream** saying what changed. A platform bridge (AT-SPI on Linux, UI
Automation on Windows, NSAccessibility on macOS) is the layer that hands this
to the operating system; the bridges are not written yet, so today the layer
serves tests, in-application readers (read aloud, a braille panel) and future
bridges.

Header: `UltraCanvasAccessibility.h` (included by `UltraCanvasUIElement.h`).

## Elements

```cpp
AccessibleRole role = element->GetAccessibleRole();   // Unknown by default
std::string name = element->GetAccessibleName();
IAccessibleText* text = element->GetAccessibleTextInterface();   // null without text
```

An element describes itself by overriding these three virtuals of
`UltraCanvasUIElement`. `UltraCanvasRichTextEdit` is the first to do so: role
`Document`, name the document's title (else the element's identifier), and a
text interface over the whole document.

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
text element announces edits, caret moves and selection changes. Elements build
events only while someone listens (`HasListeners()`), so the layer costs nothing
otherwise. Listeners are called on the thread that made the change (the UI
thread).

## Helpers

`CharacterCount`, `ByteOffsetOfCharacter` and `CharacterOffsetOfByte` convert
between the characters of the accessibility API and UTF-8 byte offsets.
