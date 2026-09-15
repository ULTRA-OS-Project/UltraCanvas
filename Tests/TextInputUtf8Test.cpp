// Tests/TextInputUtf8Test.cpp
// A text field must stay UTF-8 whatever the user does to it.
//
// UltraCanvasTextInput addresses its buffer by byte offset. Every offset it
// keeps - the caret, both selection ends, the result of a hit test - therefore
// has to land on a character boundary, because the moment one does not, the
// prefix the field measures (or the text it stores) ends inside a multi-byte
// character and is no longer UTF-8. Pango then refuses the whole string:
// "Invalid UTF-8 string passed to pango_layout_set_text()" in the debug log,
// and nothing drawn. Typing the name "Fröhling" into the account wizard was
// enough to hit it.
//
// Runs headless: the field is driven through its public API and key events
// without a window, so nothing here needs a display.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtilsUtf8.h"

#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

namespace {

// "Fröhling": 8 characters in 9 bytes, the "ö" (C3 B6) spanning bytes 2..3.
const std::string kName = "Fr\xC3\xB6hling";

bool IsValidUtf8(const std::string& s) {
    return g_utf8_validate(s.c_str(), static_cast<gssize>(s.size()), nullptr) != 0;
}

std::shared_ptr<UltraCanvasTextInput> MakeField() {
    return CreateTextInput("utf8Field", 0, 0, 200, 24);
}

UCEvent KeyEvent(UCKeys key) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    return e;
}

// A typed character arrives as UTF-8 text on the event, with no virtual key of
// its own - that is what every backend's input method delivers.
UCEvent TypeEvent(const std::string& utf8) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = UCKeys::Unknown;
    e.text = utf8;
    return e;
}

void TestBoundaryHelpers() {
    TEST("prev boundary steps over a whole character",
         utf8_prev_boundary(kName, 4) == 2);
    TEST("next boundary steps over a whole character",
         utf8_next_boundary(kName, 2) == 4);
    TEST("an offset inside a character aligns back to its start",
         utf8_align_boundary(kName, 3) == 2);
    TEST("an offset already on a boundary is left alone",
         utf8_align_boundary(kName, 4) == 4);
    TEST("boundaries stop at the ends",
         utf8_prev_boundary(kName, 0) == 0 &&
         utf8_next_boundary(kName, kName.size()) == kName.size());
    TEST("every character start is listed, plus the end",
         utf8_boundaries(kName) ==
             std::vector<size_t>({0, 1, 2, 4, 5, 6, 7, 8, 9}));
    TEST("a character limit is measured in bytes correctly",
         utf8_bytes_for_chars(kName, 3) == 4 &&
         utf8_bytes_for_chars(kName, 99) == kName.size());

    // Malformed input must not send the helpers off the end of the string.
    const std::string broken = "a\xC3";
    TEST("a truncated sequence still terminates",
         utf8_next_boundary(broken, 1) == broken.size() &&
         utf8_prev_boundary(broken, broken.size()) == 1);
    TEST("a lone Latin-1 byte is repaired, not kept",
         IsValidUtf8(utf8_make_valid("Fr\xF6hling")));
    TEST("valid text is returned unchanged", utf8_make_valid(kName) == kName);
}

void TestTyping() {
    auto field = MakeField();
    field->OnEvent(TypeEvent("F"));
    field->OnEvent(TypeEvent("r"));
    field->OnEvent(TypeEvent("\xC3\xB6"));
    field->OnEvent(TypeEvent("h"));
    TEST("a typed multi-byte character is stored whole",
         field->GetText() == "Fr\xC3\xB6h");
    TEST("the caret ends up past it", field->GetCaretPosition() == 5);
    TEST("the buffer is valid UTF-8", IsValidUtf8(field->GetText()));
}

void TestBackspace() {
    auto field = MakeField();
    field->SetText(kName);
    field->SetCaretPosition(kName.size());

    // Backspace the whole name away, one keystroke per character.
    for (int i = 0; i < 8; ++i) {
        field->OnEvent(KeyEvent(UCKeys::Backspace));
        if (!IsValidUtf8(field->GetText())) {
            TEST("backspace never leaves half a character behind", false);
            return;
        }
    }
    TEST("backspace never leaves half a character behind", true);
    TEST("eight keystrokes clear eight characters", field->GetText().empty());

    // And once, straight onto the multi-byte character itself.
    field->SetText(kName);
    field->SetCaretPosition(4);   // just after the "ö"
    field->OnEvent(KeyEvent(UCKeys::Backspace));
    TEST("backspace erases the whole character", field->GetText() == "Frhling");
    TEST("the caret follows it", field->GetCaretPosition() == 2);
}

void TestDelete() {
    auto field = MakeField();
    field->SetText(kName);
    field->SetCaretPosition(2);   // just before the "ö"
    field->OnEvent(KeyEvent(UCKeys::Delete));
    TEST("delete erases the whole character", field->GetText() == "Frhling");
    TEST("the caret stays put", field->GetCaretPosition() == 2);
}

void TestCaretMovement() {
    auto field = MakeField();
    field->SetText(kName);
    field->SetCaretPosition(kName.size());

    for (int i = 0; i < 5; ++i) field->OnEvent(KeyEvent(UCKeys::Left));
    TEST("left steps one character at a time", field->GetCaretPosition() == 4);
    field->OnEvent(KeyEvent(UCKeys::Left));
    TEST("left steps over a two-byte character in one go",
         field->GetCaretPosition() == 2);
    field->OnEvent(KeyEvent(UCKeys::Right));
    TEST("right steps back over it in one go", field->GetCaretPosition() == 4);

    // A caller (or a restored setting) may hand over an offset that is not on a
    // boundary; the field must not keep it.
    field->SetCaretPosition(3);
    TEST("a caret set inside a character snaps to its start",
         field->GetCaretPosition() == 2);
}

void TestSelection() {
    auto field = MakeField();
    field->SetText(kName);
    field->SetSelection(1, 3);   // ends inside the "ö"
    TEST("a selection end inside a character snaps to a boundary",
         IsValidUtf8(field->GetSelectedText()) &&
         field->GetSelectedText() == "r");

    field->SelectAll();
    TEST("select-all takes the whole string", field->GetSelectedText() == kName);
}

void TestLengthLimit() {
    auto field = MakeField();
    field->SetMaxLength(8);
    field->SetText(kName);
    TEST("a limit of 8 characters keeps all 8 of them (in 9 bytes)",
         field->GetText() == kName);

    field->SetCaretPosition(kName.size());
    field->OnEvent(TypeEvent("s"));
    TEST("a ninth character is refused", field->GetText() == kName);

    // Shrinking the limit truncates on a character boundary, never mid-character.
    field->SetMaxLength(3);
    TEST("truncation cuts between characters",
         field->GetText() == "Fr\xC3\xB6" && IsValidUtf8(field->GetText()));
}

void TestMasking() {
    // "übung": 5 characters, 6 bytes. Split so that the 'b' is not swallowed as
    // a third hex digit of the escape.
    const std::string uebung = "\xC3\xBC" "bung";

    auto field = CreatePasswordInput("utf8Pass", 0, 0, 200, 24);
    field->SetText(uebung);
    TEST("a mask shows one star per character, not per byte",
         field->GetRenderText() == "*****");
    TEST("the text itself is untouched", field->GetText() == uebung);

    field->SetPasswordRevealed(true);
    TEST("revealing shows the real text", field->GetRenderText() == uebung);
}

void TestTextFromOutside() {
    auto field = MakeField();
    // Latin-1, as a keyboard backend without an input method would deliver it.
    field->SetText("Fr\xF6hling");
    TEST("text that is not UTF-8 is repaired on the way in",
         IsValidUtf8(field->GetText()));

    auto typed = MakeField();
    typed->OnEvent(TypeEvent("Fr\xF6hling"));
    TEST("and so is text that arrives as a key event",
         IsValidUtf8(typed->GetText()));
}

} // namespace

int main() {
    std::cerr << "=== UltraCanvasTextInput UTF-8 test ===" << std::endl;

    TestBoundaryHelpers();
    TestTyping();
    TestBackspace();
    TestDelete();
    TestCaretMovement();
    TestSelection();
    TestLengthLimit();
    TestMasking();
    TestTextFromOutside();

    std::cerr << "\n" << (testCount - failCount) << "/" << testCount
              << " checks passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
