# UltraCanvasTextArea Documentation

## Overview
The **UltraCanvasTextArea** is an advanced multi-line text editing control within the UltraCanvas framework, designed for sophisticated text manipulation with features including syntax highlighting, line numbers, selection management, and extensive customization options.

**Header:** `UltraCanvasTextArea.h`  
**Implementation:** `UltraCanvasTextArea.cpp`  
**Version:** 2.0.0  
**Last Modified:** 2024-12-20  
**Author:** UltraCanvas Framework  

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
UltraCanvasTextArea(const std::string& name, int id, int x, int y, int width, int height)
```

### Parameters
- `name`: Unique identifier name for the text area
- `id`: Numeric identifier for the control
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
    int lineHeight;
    Color fontColor;
    
    // Background and borders
    Color backgroundColor;
    Color borderColor;
    int borderWidth;
    int padding;
    
    // Selection and cursor
    Color selectionColor;
    Color currentLineHighlightColor;
    Color cursorColor;
    
    // Line numbers
    bool showLineNumbers;
    int lineNumbersWidth;
    Color lineNumbersColor;
    Color lineNumbersBackgroundColor;
    
    // Current line highlighting
    Color currentLineColor;
    
    // Syntax highlighting
    bool highlightSyntax;
    
    // Scrollbars
    Color scrollbarTrackColor;
    Color scrollbarColor;
    
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
    Color color;
    bool bold;
    bool italic;
    bool underline;
};
```

## Public Methods

### Text Manipulation

#### SetText
```cpp
void SetText(const std::string& text)
```
Sets the entire content of the text area.

#### GetText
```cpp
std::string GetText() const
```
Returns the complete text content.

#### InsertText
```cpp
void InsertText(const std::string& text)
```
Inserts text at the current cursor position.

#### InsertCharacter
```cpp
void InsertCharacter(char ch)
```
Inserts a single character at cursor position.

#### InsertNewLine
```cpp
void InsertNewLine()
```
Inserts a line break at cursor position.

#### InsertTab
```cpp
void InsertTab()
```
Inserts a tab character or spaces based on tab settings.

#### DeleteCharacterBackward
```cpp
void DeleteCharacterBackward()
```
Deletes character before cursor (backspace behavior).

#### DeleteCharacterForward
```cpp
void DeleteCharacterForward()
```
Deletes character after cursor (delete key behavior).

#### DeleteSelection
```cpp
void DeleteSelection()
```
Removes currently selected text.

#### Clear
```cpp
void Clear()
```
Removes all text content.

### Cursor Movement

#### MoveCursorLeft
```cpp
void MoveCursorLeft(bool selecting = false)
```
Moves cursor one character left, optionally selecting text.

#### MoveCursorRight
```cpp
void MoveCursorRight(bool selecting = false)
```
Moves cursor one character right, optionally selecting text.

#### MoveCursorUp
```cpp
void MoveCursorUp(bool selecting = false)
```
Moves cursor one line up, maintaining column position.

#### MoveCursorDown
```cpp
void MoveCursorDown(bool selecting = false)
```
Moves cursor one line down, maintaining column position.

#### MoveCursorToLineStart
```cpp
void MoveCursorToLineStart(bool selecting = false)
```
Moves cursor to beginning of current line.

#### MoveCursorToLineEnd
```cpp
void MoveCursorToLineEnd(bool selecting = false)
```
Moves cursor to end of current line.

#### MoveCursorToStart
```cpp
void MoveCursorToStart(bool selecting = false)
```
Moves cursor to beginning of document.

#### MoveCursorToEnd
```cpp
void MoveCursorToEnd(bool selecting = false)
```
Moves cursor to end of document.

#### SetCursorPosition
```cpp
void SetCursorPosition(int position)
```
Sets cursor to specific character position.

#### GetCursorPosition
```cpp
int GetCursorPosition() const
```
Returns current cursor position as character index.

### Selection Management

#### SelectAll
```cpp
void SelectAll()
```
Selects entire text content.

#### SelectLine
```cpp
void SelectLine(int lineIndex)
```
Selects specified line by index.

#### SelectWord
```cpp
void SelectWord()
```
Selects word at cursor position.

#### SetSelection
```cpp
void SetSelection(int start, int end)
```
Sets selection range by character indices.

#### ClearSelection
```cpp
void ClearSelection()
```
Removes current selection.

#### HasSelection
```cpp
bool HasSelection() const
```
Returns true if text is selected.

#### GetSelectedText
```cpp
std::string GetSelectedText() const
```
Returns currently selected text.

### Clipboard Operations

#### CopySelection
```cpp
void CopySelection()
```
Copies selected text to clipboard.

#### CutSelection
```cpp
void CutSelection()
```
Cuts selected text to clipboard.

#### PasteClipboard
```cpp
void PasteClipboard()
```
Pastes clipboard content at cursor position.

### Syntax Highlighting

#### SetHighlightSyntax
```cpp
void SetHighlightSyntax(bool on)
```
Enables or disables syntax highlighting.

#### SetProgrammingLanguage
```cpp
void SetProgrammingLanguage(const std::string& language)
```
Sets language for syntax highlighting (e.g., "cpp", "python", "javascript").

#### SetProgrammingLanguageByExtension
```cpp
void SetProgrammingLanguageByExtension(const std::string& extension)
```
Auto-detects language from file extension (e.g., ".cpp", ".py", ".js").

### Property Setters

#### SetReadOnly
```cpp
void SetReadOnly(bool readOnly)
```
Enables or disables read-only mode.

#### SetWordWrap
```cpp
void SetWordWrap(bool wrap)
```
Enables or disables word wrapping.

#### SetHighlightCurrentLine
```cpp
void SetHighlightCurrentLine(bool highlight)
```
Enables or disables current line highlighting.

#### SetShowLineNumbers
```cpp
void SetShowLineNumbers(bool show)
```
Shows or hides line numbers.

#### SetTabSize
```cpp
void SetTabSize(int size)
```
Sets number of spaces for tab character.

### Styling

#### SetStyle
```cpp
void SetStyle(const TextAreaStyle& newStyle)
```
Applies complete style configuration.

#### SetFont
```cpp
void SetFont(const std::string& family, float size)
```
Sets font family and size.

#### SetFontFamily
```cpp
void SetFontFamily(const std::string& family)
```
Sets font family name.

#### SetFontSize
```cpp
void SetFontSize(float size)
```
Sets font size in points.

#### Color Settings
```cpp
void SetTextColor(const Color& color)
void SetBackgroundColor(const Color& color)
void SetSelectionColor(const Color& color)
void SetCursorColor(const Color& color)
```

### Theme Application

#### ApplyDarkTheme
```cpp
void ApplyDarkTheme()
```
Applies dark color scheme with syntax highlighting.

#### ApplyLightTheme
```cpp
void ApplyLightTheme()
```
Applies light color scheme with syntax highlighting.

### Scrolling

#### ScrollTo
```cpp
void ScrollTo(int line)
```
Scrolls to specific line number.

#### ScrollUp/ScrollDown
```cpp
void ScrollUp(int lines = 1)
void ScrollDown(int lines = 1)
```
Scrolls vertically by specified number of lines.

#### ScrollLeft/ScrollRight
```cpp
void ScrollLeft(int chars = 1)
void ScrollRight(int chars = 1)
```
Scrolls horizontally by specified number of characters.

#### EnsureCursorVisible
```cpp
void EnsureCursorVisible()
```
Automatically scrolls to make cursor visible.

## Callbacks

The text area supports various event callbacks:

```cpp
// Text change notification
using TextChangedCallback = std::function<void(const std::string&)>;
void SetOnTextChanged(TextChangedCallback callback);

// Cursor position change
using CursorPositionChangedCallback = std::function<void(int line, int column)>;
void SetOnCursorPositionChanged(CursorPositionChangedCallback callback);

// Selection change
using SelectionChangedCallback = std::function<void()>;
void SetOnSelectionChanged(SelectionChangedCallback callback);
```

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
- **Wheel**: Vertical scrolling
- **Shift+Wheel**: Horizontal scrolling

## Usage Example

```cpp
// Create text area
auto textArea = std::make_shared<UltraCanvasTextArea>(
    "codeEditor", 1001, 10, 10, 800, 600
);

// Configure for code editing
textArea->SetHighlightSyntax(true);
textArea->SetProgrammingLanguage("cpp");
textArea->SetShowLineNumbers(true);
textArea->SetTabSize(4);
textArea->ApplyDarkTheme();

// Set initial content
textArea->SetText("#include <iostream>\n\nint main() {\n    std::cout << \"Hello World!\" << std::endl;\n    return 0;\n}");

// Add text change callback
textArea->SetOnTextChanged([](const std::string& text) {
    std::cout << "Text changed, length: " << text.length() << std::endl;
});

// Add cursor position callback
textArea->SetOnCursorPositionChanged([](int line, int column) {
    std::cout << "Cursor at line " << line << ", column " << column << std::endl;
});

// Add to window
window->AddElement(textArea);
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

### Multiple Selection Support
While the current implementation supports single selection, the architecture allows for future multi-cursor editing:

```cpp
// Future API (planned)
textArea->AddCursor(position);
textArea->AddSelection(start, end);
```

### Performance Optimization
The text area includes several optimizations:
- **Lazy rendering**: Only visible lines are drawn
- **Text caching**: Tokenization results are cached
- **Dirty region tracking**: Only changed areas are redrawn
- **Virtual scrolling**: Large documents handled efficiently

## Integration with Other Components

The text area can be integrated with other UltraCanvas components:

```cpp
// Create with scrollbar
auto scrollContainer = std::make_shared<UltraCanvasScrollContainer>();
scrollContainer->SetContent(textArea);

// Add to split view
auto splitView = std::make_shared<UltraCanvasSplitContainer>();
splitView->SetLeftPane(textArea);
splitView->SetRightPane(previewPanel);
```

## Notes and Best Practices

1. **Memory Management**: Use smart pointers for component lifecycle management
2. **Event Handling**: Return true from event handlers to prevent propagation
3. **Performance**: Enable syntax highlighting only when needed
4. **Accessibility**: Ensure proper focus management and keyboard navigation
5. **Theming**: Use consistent color schemes across the application
6. **Input Validation**: Implement max length restrictions for large documents

## Related Components
- **UltraCanvasTextInput**: Single-line text input
- **UltraCanvasStyledText**: Rich text with formatting
- **UltraCanvasCodeEditor**: Enhanced code editing features
- **UltraCanvasFormulaEditor**: Mathematical formula editing

## Platform-Specific Notes
- **Linux**: Uses X11/Wayland clipboard integration
- **Windows**: Native Win32 clipboard support
- **macOS**: Cocoa clipboard integration

## Version History
- **2.0.0** (2024-12-20): Added syntax highlighting and themes
- **1.5.0**: Added line numbers and word wrap
- **1.0.0**: Initial implementation with basic editing
