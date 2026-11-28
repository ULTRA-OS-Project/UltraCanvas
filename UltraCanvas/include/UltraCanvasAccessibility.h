// include/UltraCanvasAccessibility.h
// What assistive technology (screen readers, magnifiers, braille displays)
// needs from an element, independent of the platform's accessibility API:
// its role and name, for text its content, caret, selection, character
// positions and formatting, and a stream of events telling a listener what
// changed. The platform bridges sit on top of it: AT-SPI on Linux
// (OS/Linux/UltraCanvasLinuxAccessibility), UI Automation on Windows
// (OS/MSWindows/UltraCanvasWindowsAccessibility), NSAccessibility on macOS
// (OS/MacOS/UltraCanvasMacOSAccessibility).
//
//     if (IAccessibleText* text = element->GetAccessibleTextInterface()) {
//         int start = 0, end = 0;
//         std::string line = text->GetTextAtOffset(text->GetCaretOffset(),
//                                                  AccessibleTextBoundary::Line, start, end);
//     }
//     int id = UltraCanvasAccessibility::AddListener([](const AccessibilityEvent& e) { ... });
//
// Offsets count characters (Unicode code points), as the platform APIs do,
// not bytes.
//
// An element that draws its own pieces instead of holding child elements -
// a spreadsheet's cells, a slide editor's slides and shapes - describes them
// as parts (IAccessibleParts), each named by its path of indices from the
// element, and a grid of cells also as a table (IAccessibleTable):
//
//     if (IAccessibleTable* table = element->GetAccessibleTableInterface()) {
//         int row = 0, column = 0;
//         if (table->GetFocusedCell(row, column)) say(table->GetCell(row, column).name);   // "B3 42"
//     }
// Version: 1.3.0 - parts an element draws itself (IAccessibleParts, AccessiblePart), a grid of cells
//                  (IAccessibleTable); roles TableRow, TableCell and Canvas; AccessibilityEvent::part
// Version: 1.2.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <functional>
#include <vector>
#include <string>

namespace UltraCanvas {

class UltraCanvasUIElement;

enum class AccessibleRole {
    Unknown,
    Window,
    Button,
    CheckBox,
    Label,
    TextField,        // one-line text entry
    TextArea,         // multi-line plain text
    Document,         // rich text: paragraphs, headings, lists, tables
    List,
    ListItem,
    Table,
    Image,
    Link,
    Menu,
    MenuItem,
    // Version 1.2: the common widgets.
    RadioButton,
    Switch,           // on/off switch (a toggle button to assistive technology)
    ComboBox,         // drop-down list with one current item
    Slider,
    SpinButton,       // number field with up/down steps
    ProgressBar,
    Toolbar,
    TabList,          // tabbed container
    Tree,
    Group,            // panel, group box: holds and names other elements
    // Version 1.3: what an element draws itself (IAccessibleParts).
    TableRow,         // a row of a table's cells
    TableCell,        // one cell of a table
    Canvas            // a drawing surface whose shapes are its parts (a slide editor)
};

// A checkbox, radio button, switch or toggle button's state.
enum class AccessibleToggleState {
    NotToggleable,
    Off,
    On,
    Mixed             // a tri-state checkbox's indeterminate state
};

// A slider, spin button or progress bar's value and range.
struct AccessibleRange {
    double value = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;        // 0 = continuous
    bool readOnly = false;
};

enum class AccessibleTextBoundary { Character, Word, Line, Sentence, Paragraph };

// The formatting of a stretch of text, as a screen reader reports it.
struct AccessibleTextAttributes {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool superscript = false;
    bool subscript = false;
    std::string fontFamily;
    float fontSizePt = 0.0f;           // 0 = the element's default
    std::string color;                 // "#RRGGBB"; "" = default
    std::string backgroundColor;
    std::string link;                  // a link's target
    int headingLevel = 0;              // the paragraph is a heading of this level
    bool listItem = false;
    bool inserted = false;             // a tracked change
    bool deleted = false;
    bool commented = false;            // under a reviewer's comment
    bool misspelled = false;
};

// Text an element shows and lets the user move through.
class IAccessibleText {
public:
    virtual ~IAccessibleText() = default;
    virtual std::string GetAccessibleText() const = 0;          // UTF-8, all of it
    virtual int GetCharacterCount() const = 0;
    virtual int GetCaretOffset() const = 0;
    virtual bool SetCaretOffset(int offset) = 0;
    // False when nothing is selected (start == end == caret then).
    virtual bool GetSelection(int& start, int& end) const = 0;
    virtual bool SetSelection(int start, int end) = 0;
    // A character's box in window coordinates; empty when it is not laid out.
    virtual Rect2Df GetCharacterBounds(int offset) const = 0;
    // The character at a point in window coordinates, or -1.
    virtual int GetOffsetAtPoint(const Point2Df& windowPoint) const = 0;
    // The formatting at `offset`, and the stretch [runStart, runEnd) it holds for.
    virtual AccessibleTextAttributes GetAttributesAt(int offset, int& runStart, int& runEnd) const = 0;
    // The character, word, line, sentence or paragraph containing `offset`,
    // with its extent. Lines come from the element's layout; the other units
    // from the text (see UltraCanvasAccessibility::TextUnitAt).
    virtual std::string GetTextAtOffset(int offset, AccessibleTextBoundary boundary, int& start, int& end) const;
    // True when the user cannot change the text (a viewer, a locked field);
    // a screen reader then does not announce it as editable.
    virtual bool IsReadOnly() const { return false; }
};

enum class AccessibilityEventType {
    FocusChanged,       // `element` got the focus
    TextChanged,        // its text changed; offset/length give what is new, when known (else -1)
    CaretMoved,         // offset = the new caret offset
    SelectionChanged,
    NameChanged,
    ElementDestroyed,   // `element` is being destroyed; drop any reference to it
    ValueChanged,       // its range value or value text changed (slider, combo box, text field)
    StateChanged        // its toggle state changed (checked, pressed)
};

struct AccessibilityEvent {
    AccessibilityEventType type = AccessibilityEventType::TextChanged;
    UltraCanvasUIElement* element = nullptr;
    int offset = -1;
    int length = -1;
    // The part of `element` it concerns (IAccessibleParts): for a
    // SelectionChanged, the part that now has the focus. Empty: the element.
    std::vector<int> part;
};

// ===== PARTS AN ELEMENT DRAWS ITSELF =====
// A piece of an element that is drawn rather than held as a child element:
// a spreadsheet's row or cell, a slide in a slide editor's strip, a shape on
// the slide. A part is named by its path of indices from its element: {} is
// the element itself, {2} its third part, {2, 5} the sixth part of that.
struct AccessiblePart {
    AccessibleRole role = AccessibleRole::Unknown;
    std::string name;            // what a screen reader says for it ("B3 42", "Title: Sales")
    std::string description;     // read after the name; may be empty
    Rect2Df bounds;              // window coordinates; empty when it is not shown
    bool selected = false;
    bool focused = false;        // the one the keys act on (at most one per element)
    int row = -1, column = -1;   // a table cell's (or row's) place; -1: not in a table
};

class IAccessibleParts {
public:
    virtual ~IAccessibleParts() = default;
    // How many parts `parent` holds ({}: the element's own parts).
    virtual int GetPartCount(const std::vector<int>& parent) const = 0;
    // The part at `path`; role Unknown when there is none.
    virtual AccessiblePart GetPart(const std::vector<int>& path) const = 0;
    // The path of the part with the focus; empty for none.
    virtual std::vector<int> GetFocusedPart() const = 0;
    // The paths of the parts that are selected.
    virtual std::vector<std::vector<int>> GetSelectedParts() const = 0;
};

// A grid of cells (a spreadsheet): what a table interface of the platform
// asks. Its parts are its rows, each row's parts its cells.
class IAccessibleTable {
public:
    virtual ~IAccessibleTable() = default;
    virtual int GetRowCount() const = 0;
    virtual int GetColumnCount() const = 0;
    virtual std::string GetRowHeader(int row) const = 0;         // "3"
    virtual std::string GetColumnHeader(int column) const = 0;   // "C"
    // Cell (`row`, `column`), 0-based; role Unknown outside the table.
    virtual AccessiblePart GetCell(int row, int column) const = 0;
    // The cell the keys act on; false when there is none.
    virtual bool GetFocusedCell(int& row, int& column) const = 0;
    // The selected cells as one block; false when nothing is selected.
    virtual bool GetSelectedCells(int& firstRow, int& firstColumn, int& lastRow, int& lastColumn) const = 0;
};

class UltraCanvasAccessibility {
public:
    using Listener = std::function<void(const AccessibilityEvent&)>;
    // A bridge (or a test) listening to every element's events. Returns an
    // id for RemoveListener.
    static int AddListener(Listener listener);
    static void RemoveListener(int id);
    // Whether anyone listens: elements skip building events otherwise.
    static bool HasListeners();
    static void Notify(const AccessibilityEvent& event);

    // Character-offset helpers for implementations of IAccessibleText.
    static int CharacterCount(const std::string& utf8);
    static size_t ByteOffsetOfCharacter(const std::string& utf8, int character);
    static int CharacterOffsetOfByte(const std::string& utf8, size_t byte);
    // The word, sentence or paragraph (or character) of `text` containing
    // character `offset`: its extent in characters and its text.
    static std::string TextUnitAt(const std::string& text, int offset, AccessibleTextBoundary boundary,
                                  int& start, int& end);
};

} // namespace UltraCanvas
