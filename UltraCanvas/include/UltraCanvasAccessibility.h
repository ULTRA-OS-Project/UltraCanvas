// include/UltraCanvasAccessibility.h
// What assistive technology (screen readers, magnifiers, braille displays)
// needs from an element, independent of the platform's accessibility API:
// its role and name, for text its content, caret, selection, character
// positions and formatting, and a stream of events telling a listener what
// changed. The platform bridges sit on top of it: AT-SPI on Linux
// (OS/Linux/UltraCanvasLinuxAccessibility), UI Automation on Windows
// (OS/MSWindows/UltraCanvasWindowsAccessibility); none yet for macOS.
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
// Version: 1.1.0
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
    MenuItem
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
    ElementDestroyed    // `element` is being destroyed; drop any reference to it
};

struct AccessibilityEvent {
    AccessibilityEventType type = AccessibilityEventType::TextChanged;
    UltraCanvasUIElement* element = nullptr;
    int offset = -1;
    int length = -1;
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
