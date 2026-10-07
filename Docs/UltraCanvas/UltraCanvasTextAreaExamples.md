# UltraCanvasTextArea Documentation

## Overview
The **UltraCanvasTextArea** is an advanced multi-line text editing control within the UltraCanvas framework, designed for sophisticated text manipulation with features including syntax highlighting, line numbers, selection management, and extensive customization options.

**Header:** `UltraCanvasTextArea.h`  
**Implementation:** `UltraCanvasTextArea.cpp`  
**Version:** 2.0.1  
**Last Modified:** 2026-10-07  
**Author:** UltraCanvas Framework  

<!-- doc-check: void SendChatText(); std::vector<std::pair<size_t, size_t>> FindNoSpellRanges(const std::string& text); bool RangesOverlap(const std::vector<std::pair<size_t, size_t>>& ranges, size_t startByte, size_t byteLength); std::vector<std::string> lines; std::vector<size_t> matchOffsets; std::string term; -->

## Class Hierarchy
```
UltraCanvasUIElement
    └── UltraCanvasTextArea
```

## Key Features
- Multi-line text editing with full Unicode support
- Syntax highlighting for multiple programming languages
- Line numbers display with customizable styling
- Text selection with keyboard and mouse
- Clipboard operations (copy, cut, paste)
- Horizontal and vertical scrolling
- Current line highlighting
- Word wrap support
- Read-only mode
- Customizable themes (Light/Dark)
- Undo/Redo functionality
- Tab size configuration
- Cursor animation and customization

## Constructor
```cpp
UltraCanvasTextArea(const std::string& name, float x, float y, float width, float height);
UltraCanvasTextArea(const std::string& name, float width, float height);   // position -1, -1
explicit UltraCanvasTextArea(const std::string& name);                     // size -1, -1 (layout decides);
```

### Parameters
- `name`: Unique identifier name for the text area
- `x`: X-coordinate position
- `y`: Y-coordinate position
- `width`: Width of the text area
- `height`: Height of the text area

## Core Properties

### TextAreaStyle Structure
The `TextAreaStyle` structure encapsulates all visual properties:

```cpp
struct TextAreaStyle {
    // Font properties
    FontStyle fontStyle;
    FontStyle fixedFontStyle;
    float lineHeight;
    Color fontColor;
    
    // Background and borders
    Color backgroundColor;
    Color borderColor;
    float textPadding;
    
    // Selection and cursor
    Color selectionColor;
    Color currentLineHighlightColor;
    Color cursorColor;

    // Placeholder hint shown when the document is empty
    Color placeholderColor = Color(150, 150, 150, 255);
    
    // Line numbers
    bool showLineNumbers;
    Color lineNumbersColor;
    Color lineNumbersBackgroundColor;
    
    // Current line highlighting
    Color currentLineColor;
    
    // Syntax highlighting
    bool highlightSyntax;
    
    // Scrollbars
    Color scrollbarTrackColor;
    Color scrollbarColor;
    int   scrollbarWidth = 15;          // track thickness in px
    float scrollbarCornerRadius = 0.0f; // 0 = square; 6 with width 12 matches ScrollbarStyle::Modern()
    int   scrollbarThumbInset = 2;      // gap between track edge and thumb (0 = thumb fills the track)
    
    // Token styles for syntax highlighting
    struct TokenStyles {
        TokenStyle keywordStyle;
        TokenStyle typeStyle;
        TokenStyle functionStyle;
        TokenStyle numberStyle;
        TokenStyle stringStyle;
        TokenStyle characterStyle;
        TokenStyle commentStyle;
        TokenStyle operatorStyle;
        TokenStyle punctuationStyle;
        TokenStyle preprocessorStyle;
        TokenStyle constantStyle;
        TokenStyle identifierStyle;
        TokenStyle builtinStyle;
        TokenStyle assemblyStyle;
        TokenStyle registerStyle;
        TokenStyle defaultStyle;
    } tokenStyles;
};
```

### TokenStyle Structure
```cpp
struct TokenStyle {
    Color color = Color(0, 0, 0);
    bool bold = false;
    bool italic = false;
    bool underline = false;

    TokenStyle() = default;
    TokenStyle(const Color &c, bool b = false, bool i = false, bool u = false);
};
```

## Public Methods

### Text Manipulation

#### SetText
```cpp
void SetText(const std::string& text, bool runNotifications = true);
```
Sets the entire content of the text area. Pass `runNotifications = false` to
skip the change callbacks.

#### GetText
```cpp
std::string GetText() const;
```
Returns the complete text content.

#### InsertText
```cpp
void InsertText(const std::string& text);
```
Inserts text at the current cursor position.

#### InsertCodepoint
```cpp
void InsertCodepoint(char32_t codepoint);
```
Inserts a single Unicode character at cursor position.

#### InsertNewLine
```cpp
void InsertNewLine();
```
Inserts a line break at cursor position.

#### InsertTab
```cpp
void InsertTab();
```
Inserts a tab character or spaces based on tab settings.

#### DeleteCharacterBackward
```cpp
void DeleteCharacterBackward();
```
Deletes character before cursor (backspace behavior).

#### DeleteCharacterForward
```cpp
void DeleteCharacterForward();
```
Deletes character after cursor (delete key behavior).

#### DeleteSelection
```cpp
void DeleteSelection();
```
Removes currently selected text.

#### Clear
```cpp
void Clear();
```
Removes all text content.

### Cursor Movement

#### MoveCursorLeft
```cpp
void MoveCursorLeft(bool selecting = false);
```
Moves cursor one character left, optionally selecting text.

#### MoveCursorRight
```cpp
void MoveCursorRight(bool selecting = false);
```
Moves cursor one character right, optionally selecting text.

#### MoveCursorUp
```cpp
void MoveCursorUp(bool selecting = false);
```
Moves cursor one line up, maintaining column position.

#### MoveCursorDown
```cpp
void MoveCursorDown(bool selecting = false);
```
Moves cursor one line down, maintaining column position.

#### MoveCursorToLineStart
```cpp
void MoveCursorToLineStart(bool selecting = false);
```
Moves cursor to beginning of current line.

#### MoveCursorToLineEnd
```cpp
void MoveCursorToLineEnd(bool selecting = false);
```
Moves cursor to end of current line.

#### MoveCursorToStart
```cpp
void MoveCursorToStart(bool selecting = false);
```
Moves cursor to beginning of document.

#### MoveCursorToEnd
```cpp
void MoveCursorToEnd(bool selecting = false);
```
Moves cursor to end of document.

#### SetCursorPosition
```cpp
void SetCursorPosition(const LineColumnIndex& pos, bool selecting = false);
```
Sets the cursor to a line and column (`pos.lineIndex`, `pos.columnIndex`, both
0-based; the column counts codepoints). With `selecting` set, the selection is
extended to `pos` from its anchor, as Shift+arrow does: the anchor is the start
of the current selection, or the cursor's previous place when nothing is
selected. Without it the selection is left unchanged; call `ClearSelection()`
to drop it.

```cpp
textArea->SetCursorPosition({0, 2});           // caret after "he" in "hello"
textArea->SetCursorPosition({0, 5}, true);     // selects "llo"
```

#### GetCursorPosition
```cpp
LineColumnIndex GetCursorPosition() const;
```
Returns the current cursor position as a line / column pair.

### Selection Management

#### SelectAll
```cpp
void SelectAll();
```
Selects entire text content.

#### SelectLine
```cpp
void SelectLine(int lineIndex);
```
Selects specified line by index.

#### SelectWord
```cpp
void SelectWord();
```
Selects word at cursor position.

#### SetSelection
```cpp
void SetSelection(int startGrapheme, int endGrapheme);
void SetSelection(const LineColumnIndex& start, const LineColumnIndex& end);
```
Sets the selection range by grapheme indices, or by line / column positions.

#### ClearSelection
```cpp
void ClearSelection();
```
Removes current selection.

#### HasSelection
```cpp
bool HasSelection() const;
```
Returns true if text is selected.

#### GetSelectedText
```cpp
std::string GetSelectedText() const;
```
Returns currently selected text.

### Clipboard Operations

#### CopySelection
```cpp
void CopySelection();
```
Copies selected text to clipboard.

#### CutSelection
```cpp
void CutSelection();
```
Cuts selected text to clipboard.

#### PasteClipboard
```cpp
void PasteClipboard();
```
Pastes clipboard content at cursor position.

### Syntax Highlighting

#### SetHighlightSyntax
```cpp
void SetHighlightSyntax(bool on);
```
Enables or disables syntax highlighting.

#### SetProgrammingLanguage
```cpp
void SetProgrammingLanguage(const std::string& language);
```
Sets language for syntax highlighting (e.g., "cpp", "python", "javascript").

#### SetProgrammingLanguageByExtension
```cpp
bool SetProgrammingLanguageByExtension(const std::string& extension);
```
Auto-detects language from file extension (e.g., ".cpp", ".py", ".js").
Returns whether a language was found.

#### SetProgrammingLanguageForFile
```cpp
bool SetProgrammingLanguageForFile(const std::string& filename, const std::string& text);
```
Picks the language for a file being opened: a full-filename match first
(`pom.xml`), then the extension. An extension two languages share - `.cls`
(VBA class module or LaTeX class) and `.m` (MATLAB or Objective-C) - is
settled by the file's first lines through
`SyntaxTokenizer::LanguageFromContent(extension, text)`. Text in a language
the highlighter has no rules for (a LaTeX `.cls`, an Objective-C `.m`) is left
as plain text instead of being coloured as the other language. Returns
whether a language was set.

### Property Setters

#### SetReadOnly
```cpp
void SetReadOnly(bool readOnly);
```
Enables or disables read-only mode.

#### SetDisplayOnly
```cpp
void SetDisplayOnly(bool displayOnlyMode);
bool IsDisplayOnly() const;
```
Turns the area into a pure viewer. Display-only implies read-only and, on top of
that, takes the area out of the keyboard focus chain: `AcceptsFocus()` returns
false, no caret is drawn and no key event ever reaches it — so a hosting widget
keeps the arrow keys for its own navigation (this is how `UltraCanvasMediaViewer`
shows text files while Left/Right still browse the folder). Mouse wheel and
scrollbar scrolling plus mouse selection keep working; a host that wants
keyboard scrolling calls `ScrollUp()` / `ScrollDown()` itself.

```cpp
auto viewer = std::make_shared<UltraCanvasTextArea>("LogView", 0, 0, 600, 400);
viewer->SetDisplayOnly(true);          // read-only AND not focusable
viewer->SetText(logContents);
```

#### SetWordWrap
```cpp
void SetWordWrap(bool wrap);
```
Enables or disables word wrapping.

#### SetHighlightCurrentLine
```cpp
void SetHighlightCurrentLine(bool highlight);
```
Enables or disables current line highlighting.

#### SetShowLineNumbers
```cpp
void SetShowLineNumbers(bool show);
```
Shows or hides line numbers.

#### SetTabSize
```cpp
void SetTabSize(int size);
```
Sets number of spaces for tab character.

### Styling

#### SetStyle
```cpp
void SetStyle(const TextAreaStyle& newStyle);
```
Applies complete style configuration.

#### SetFont
```cpp
void SetFont(const std::string& family, float size);
```
Sets font family and size.

#### SetFontFamily
```cpp
void SetFontFamily(const std::string& family);
```
Sets font family name.

#### SetFontSize
```cpp
void SetFontSize(float size);
```
Sets font size in points.

#### Color Settings
```cpp
void SetTextColor(const Color& color);
void SetBackgroundColor(const Color& color);   // inherited from UltraCanvasUIElement
void SetSelectionColor(const Color& color);
void SetCursorColor(const Color& color);
```

### Theme Application

#### ApplyDarkTheme
```cpp
void ApplyDarkTheme();
```
Applies dark color scheme with syntax highlighting.

#### ApplyLightTheme
```cpp
void ApplyLightTheme();
```
Applies light color scheme with syntax highlighting.

### Scrolling

#### ScrollTo
```cpp
void ScrollTo(int line);
```
Scrolls to specific line number.

#### ScrollUp/ScrollDown
```cpp
void ScrollUp(int lines = 1);
void ScrollDown(int lines = 1);
```
Scrolls vertically by specified number of lines.

#### ScrollLeft/ScrollRight
```cpp
void ScrollLeft(int chars = 1);
void ScrollRight(int chars = 1);
```
Scrolls horizontally by specified number of characters.

#### EnsureCursorVisible
```cpp
void EnsureCursorVisible();
```
Automatically scrolls to make cursor visible.

## Callbacks

The text area supports various event callbacks:

```cpp
// Text change notification
using TextChangedCallback = std::function<void(const std::string&)>;
void SetOnTextChanged(TextChangedCallback callback);

// Cursor position change
using CursorPositionChangedCallback = std::function<void(const LineColumnIndex& pos)>;
void SetOnCursorPositionChanged(CursorPositionChangedCallback callback);

// Selection change
using SelectionChangedCallback = std::function<void()>;
void SetOnSelectionChanged(SelectionChangedCallback callback);

// Right-click, before the built-in spell suggestion popup. Return true when
// the application opened a menu of its own. See Spell Checking below.
std::function<bool(const UCEvent&)> onContextMenu;

// Called with the exact text about to be spell checked, so content-dependent
// options can be rebuilt. See Spell Checking below.
std::function<void(SpellCheckOptions&, const std::string&)> onPrepareSpellCheck;

// Every KeyDown, before the area handles it (any editing mode, read-only too).
// Return true to consume the key; false lets the area edit as usual.
std::function<bool(const UCEvent&)> onBeforeKeyDown;
```

`onBeforeKeyDown` gives a key a meaning of its own without reimplementing the
editor. A chat box that sends on Enter and starts a new line on Shift+Enter:

```cpp
auto message = std::make_shared<UltraCanvasTextArea>("chat-message", 0, 0, 400, 72);
message->SetWordWrap(true);
message->onBeforeKeyDown = [](const UCEvent& e) {
    const bool enter = e.virtualKey == UCKeys::Return || e.virtualKey == UCKeys::NumPadEnter;
    if (!enter || e.shift) return false;   // Shift+Enter: the area inserts the line break
    SendChatText();                        // your own send function
    return true;                           // consumed: no line break
};
```

`IsPositionInsideSelection(const Point2Di&)` answers whether an element-local
point falls inside the current selection — what a context menu needs to decide
whether opening it should move the caret.

## Event Handling

The text area handles the following events:

### Keyboard Events
- **Text Input**: Character insertion
- **Arrow Keys**: Cursor navigation
- **Home/End**: Line navigation
- **Page Up/Down**: Page scrolling
- **Ctrl+A**: Select all
- **Ctrl+C**: Copy
- **Ctrl+V**: Paste
- **Ctrl+X**: Cut
- **Ctrl+Z**: Undo
- **Ctrl+Y**: Redo
- **Backspace/Delete**: Character deletion
- **Tab**: Tab insertion or indentation

### Mouse Events
- **Click**: Position cursor
- **Double-click**: Select word
- **Triple-click**: Select line
- **Drag**: Text selection
- **Wheel**: Vertical scrolling (3 lines per notch)

## Usage Example

```cpp
// Create text area
auto textArea = std::make_shared<UltraCanvasTextArea>(
    "codeEditor", 10, 10, 800, 600
);

// Configure for code editing
textArea->SetHighlightSyntax(true);
textArea->SetProgrammingLanguage("cpp");
textArea->SetShowLineNumbers(true);
textArea->SetTabSize(4);
textArea->ApplyDarkTheme();

// Set initial content
textArea->SetText("#include <iostream>\n\nint main() {\n    std::cerr << \"Hello World!\" << std::endl;\n    return 0;\n}");

// Add text change callback
textArea->SetOnTextChanged([](const std::string& text) {
    std::cerr << "Text changed, length: " << text.length() << std::endl;
});

// Add cursor position callback
textArea->SetOnCursorPositionChanged([](const LineColumnIndex& pos) {
    std::cerr << "Cursor at line " << pos.lineIndex << ", column " << pos.columnIndex << std::endl;
});

// Add to window
window->AddChild(textArea);
```

## Advanced Features

### Custom Syntax Highlighting
The text area uses a `SyntaxTokenizer` class for language-specific highlighting:

```cpp
// Custom token style
TokenStyle customKeywordStyle;
customKeywordStyle.color = Color(86, 156, 214, 255);
customKeywordStyle.bold = true;

// Apply to style
TextAreaStyle style = textArea->GetStyle();
style.tokenStyles.keywordStyle = customKeywordStyle;
textArea->SetStyle(style);
```

### CSS Highlighting
CSS (`.css`, language name `"CSS"`) is not highlighted from keyword lists: a
word's colour depends on where it sits, so `color` is a property before a `:`
inside a block and a value after one. The token types it produces, and the
`tokenStyles` slot each one is drawn with:

| Where | Examples | Token type / style |
|---|---|---|
| Selector | `div`, `li`, `from` | `Keyword` |
| Selector | `.nav-link`, `[type=search]` attribute | `Identifier` |
| Selector | `#main` | `Constant` |
| Selector | `:hover`, `::before`, `:not` | `Builtin` |
| Selector | `,` `>` `+` `~` `*` | `Operator` |
| Block | `margin`, `-webkit-appearance`, `--bs-gutter-x` (property) | `Keyword` |
| Value | `none`, `flex`, `border-box` | `Constant` |
| Value | `var(`, `calc(`, `rgba(`, `url(` | `Function` |
| Value | `--bs-border-width` inside `var()` | `Identifier` |
| Value | `1.5rem`, `-.25rem`, `50%`, `#d3d4d5` | `Number` |
| Value | `"..."`, `url(unquoted)` | `String` |
| Anywhere | `@media`, `@font-face`, `!important` | `Preprocessor` |
| Anywhere | `/* ... */` | `Comment` |

Each line continues from where the line above left the rules — inside a
selector list, a declaration block or an at-rule prelude, and how many blocks
are open — so a multi-line stylesheet, and a minified one the text area splits
into 8000-character segments, is classified the same as if it were scanned in
one go. (Called without a state, `TokenizeLine(line)` still infers the start
of a line from its own braces and semicolons.)

### Constructs Spanning Lines
The text area highlights one line at a time, carrying a `SyntaxLineState`
from each line to the next: a block comment still open (`/* */`, `(* *)`,
`<!-- -->`, … — whichever the language defines), and, across the segments of a
line split for length, a string or line comment cut at the split. Each cached
line layout remembers the state it was built from, so an edit that opens or
closes a comment recolours every line below it on the next frame.

```cpp
SyntaxTokenizer tk;
tk.SetLanguage("C++");
SyntaxLineState state;                       // the first line starts empty
for (const std::string& line : lines) {
    auto tokens = tk.TokenizeLine(line, state);  // updates state
    state = state.AtLineBreak();             // strings / line comments end here
}
```

Strings do not continue across a real line break, so C's `\`-continued
strings and Python's triple-quoted strings are still coloured on their first
line only. Fenced code blocks in Markdown mode are highlighted line by line
without state.

### Single Selection
The text area has one cursor and one selection; there is no multi-cursor API.

### Performance Optimization
The text area includes several optimizations:
- **Lazy rendering**: Only visible lines are drawn
- **Text caching**: Tokenization results are cached
- **Dirty region tracking**: Only changed areas are redrawn
- **Virtual scrolling**: Large documents handled efficiently

## Integration with Other Components

The text area can be integrated with other UltraCanvas components:

```cpp
// The text area draws its own scrollbars, so it needs no scroll container.
// Editor beside a preview, with a draggable divider:
auto splitView = CreateHorizontalSplitPane("EditorSplit", 0, 0, 1000, 600);
auto editorPane  = splitView->AddPane(1.0);
auto previewPane = splitView->AddPane(1.0);

editorPane->layout.SetFlexColumn();
editorPane->AddChild(textArea);
textArea->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
previewPane->AddChild(previewPanel);
```

## Spell Checking

Backed by the shared [UltraCanvasSpellChecker](UltraCanvasSpellChecker.md)
service. Checking runs on that service's worker thread; the element queues text
on every edit and drains the finished result while it draws, so typing is never
blocked by a dictionary lookup.

```cpp
UltraCanvasSpellChecker::Instance().Initialize();   // once, at startup
textArea->SetSpellCheckEnabled(true);
```

That is the whole integration. Misspellings get a red squiggle, and
right-clicking one opens a menu of suggestions plus **Add to Dictionary** and
**Ignore**.

### Options

```cpp
SpellCheckOptions options;
options.skipUpperCaseWords = false;    // do check "HTTP"
options.fetchSuggestions   = true;     // pre-fill, instead of on right-click
textArea->SetSpellCheckOptions(options);
```

Markdown mode can keep code, links and math out of the check without the spell
module knowing any markdown. **The hook runs on the worker thread** — capture an
immutable snapshot rather than reading live element state:

```cpp
// An immutable snapshot, taken on the UI thread and shared with the worker
auto skip = std::make_shared<const std::vector<std::pair<size_t, size_t>>>(
        FindNoSpellRanges(textArea->GetText()));
options.shouldSkipRange = [skip](size_t startByte, size_t byteLength) {
    return RangesOverlap(*skip, startByte, byteLength);
};
```

`FindNoSpellRanges` and `RangesOverlap` stand for your own markdown scanner
(byte ranges of code, links and math) and an overlap test; UltraTexter's
scanner is in `Apps/Texter/UltraCanvasMarkdownSpellRanges.h`.

Those are byte ranges of the text being checked, so they go stale on the first
edit. `onPrepareSpellCheck` is called with the exact text about to be checked,
on the UI thread, and hands over a copy of the options used for that one check —
which is where a content-dependent hook belongs:

```cpp
textArea->onPrepareSpellCheck = [](SpellCheckOptions& options, const std::string& text) {
    auto skip = std::make_shared<const std::vector<std::pair<size_t, size_t>>>(FindNoSpellRanges(text));
    options.shouldSkipRange = [skip](size_t startByte, size_t byteLength) {
        return RangesOverlap(*skip, startByte, byteLength);
    };
};
```

### Supplying your own context menu

An application with its own editor context menu sets `onContextMenu` and
splices the suggestions into it, instead of getting two menus. It is called on
right-click before the built-in popup; return `true` when handled, `false` to
fall through to it:

```cpp
UltraCanvasTextArea* area = textArea.get();   // the area owns the hook: no shared_ptr cycle
textArea->onContextMenu = [area](const UCEvent& event) -> bool {
    const SpellError* hit = area->GetSpellErrorAtPosition(event.pointer.x,
                                                          event.pointer.y);
    // ... build one menu: UltraCanvasSpellChecker::BuildSuggestionMenuItems(*hit, ...)
    //     first when hit is non-null, then Cut / Copy / Paste ...
    return true;
};
```

The caret moves to the click once the hook returns `true` — not before, or
`GetSpellErrorAtPosition` would hit-test against the previous caret line's
layout — so **Paste** acts where the user clicked. A click inside the selection
keeps it, so **Cut** and **Copy** still act on what is highlighted.

### Reading and applying results

```cpp
for (const SpellError& error : textArea->GetSpellErrors()) {
    debugOutput << error.word << " at byte " << error.startByte << std::endl;
}

textArea->RunSpellCheck();                     // queue a check now
const SpellError* hit = textArea->GetSpellErrorAtPosition(mouseX, mouseY);
if (hit) textArea->ApplySpellSuggestion(*hit, "correction");
```

Errors carry offsets into the text as it was when the check ran. After an edit,
any whose span no longer holds the word it was raised for is dropped
immediately, so a mark is never painted over the wrong text while the next
result is on its way.

### API

```cpp
void SetSpellCheckEnabled(bool enabled);
bool IsSpellCheckEnabled() const;
void SetSpellCheckOptions(const SpellCheckOptions& options);
const SpellCheckOptions& GetSpellCheckOptions() const;
void RunSpellCheck();
const std::vector<SpellError>& GetSpellErrors() const;
const SpellError* GetSpellErrorAtPosition(int x, int y);
bool ApplySpellSuggestion(const SpellError& error, const std::string& replacement);
bool ShowSpellSuggestionMenu(const UCEvent& event);

// Hooks for a host application
std::function<bool(const UCEvent&)> onContextMenu;
std::function<void(SpellCheckOptions&, const std::string&)> onPrepareSpellCheck;
```

## Character Range Geometry

Maps a byte range of the document to where it actually is on screen, accounting
for soft wrap, both scroll offsets, the line-number gutter and markdown mode
(where rendered runs do not correspond one-to-one with source bytes).

```cpp
std::vector<Rect2Df> GetCharacterRangeBounds(size_t startByte, size_t byteLength);
```

One rectangle per visual line — a range crossing a soft wrap yields two. A
range scrolled out of view yields none, which is normal rather than an error.

This is what draws spell marks, but it is not spell-specific: search-result
highlighting, inline diff marks, comment anchors and collaborative-editing
cursors all need the same mapping.

```cpp
// Highlight every match of a search term
for (size_t offset : matchOffsets) {
    for (const Rect2Df& box : textArea->GetCharacterRangeBounds(offset, term.size())) {
        ctx->SetFillPaint(Color(255, 235, 59, 90));
        ctx->FillRectangle(box);
    }
}
```

Its counterpart replaces a byte range, going through the selection and undo
machinery so the edit is undoable and raises `onTextChanged` like a typed one:

```cpp
bool ReplaceTextRange(size_t startByte, size_t byteLength, const std::string& replacement);
```

## Math in Markdown mode

In `MarkdownHybrid` editing mode a formula between dollar signs is typeset
by the framework's LaTeX engine and set into the line like a word:

```markdown
Einstein wrote $E = mc^2$; the roots are $x = \frac{-b \pm \sqrt{b^2-4ac}}{2a}$.

$$
\int_0^1 x^2 \, dx = \frac{1}{3}
$$
```

- `$...$` is text-style math (fractions and operators sized for a line of
  text); `$$...$$` on one line is display-style math inline.
- `$$` alone on a line opens a display block; the next `$$` line closes it.
  The block is rendered centred at the closing fence, and the source lines
  between show only while the caret is on them.
- A pair of dollar signs counts as math only when it looks like one: no space
  right after the opener or before the closer, and no digit after the closer,
  so `costs $5 and $10` stays text.
- Formulas take the Markdown style's `mathTextColor` and the area's font size
  (in points; the engine works in pixels at 96 dpi). They work inside table
  cells, list items, headings and blockquotes.
- The rendering is baseline-aligned: the engine reports the formula's width,
  ascent and descent, the text layout reserves that box with a shape
  attribute on a U+FFFC placeholder (`TextAttributeFactory::CreateShape`),
  and the formula is drawn after the text at `IndexToPos()` /
  `IndexToBaseline()` of the placeholder. `UltraCanvasInlineMath.h` is the
  handle any other text-laying element can use the same way.
- Without the LaTeX module (not built, or its math font missing) the old
  behaviour remains: the commands are substituted with Unicode
  (`\alpha` becomes α) and the run is shown in italics.

Word and ODT documents keep their equations this way: the importers turn
OMML / MathML into `$latex$` runs, the Markdown serializer leaves them
unescaped, and the document view typesets them.

## Notes and Best Practices

1. **Memory Management**: Use smart pointers for component lifecycle management
2. **Event Handling**: Return true from event handlers to prevent propagation
3. **Performance**: Enable syntax highlighting only when needed
4. **Accessibility**: Ensure proper focus management and keyboard navigation
5. **Theming**: Use consistent color schemes across the application
6. **Input Validation**: Implement max length restrictions for large documents

## Related Components
- **UltraCanvasTextInput**: Single-line text input
- **UltraCanvasMarkdownDisplay**: Read-only markdown rendering (`Plugins/Text/UltraCanvasMarkdown.h`)
- **SyntaxTokenizer**: The highlighter behind the text area (`UltraCanvasSyntaxTokenizer.h`)
- **UltraCanvasSpellChecker**: The shared spell-check service

## Platform-Specific Notes
- **Linux**: Uses X11/Wayland clipboard integration
- **Windows**: Native Win32 clipboard support
- **macOS**: Cocoa clipboard integration

## Version History
- **3.9.0** (2026-08-28): `onContextMenu`, `onPrepareSpellCheck`, `IsPositionInsideSelection()`
- **3.8.0** (2026-08-24): Spell checking, `GetCharacterRangeBounds()`, `ReplaceTextRange()`
- **2.0.0** (2024-12-20): Added syntax highlighting and themes
- **1.5.0**: Added line numbers and word wrap
- **1.0.0**: Initial implementation with basic editing
