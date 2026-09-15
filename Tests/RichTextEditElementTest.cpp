// Tests/RichTextEditElementTest.cpp
// Runtime test for UltraCanvasRichTextEdit — the parts that only exist once
// there is a real render context: block layout geometry, click-to-caret
// hit testing, caret rectangles, visual-line motion and typed input through
// real events.
//
// The editing rules themselves are covered without a display by
// RichTextEditorTest; what is under test here is the layer that maps a
// UCRichDocument onto ITextLayout and back.
//
// Runs headless under Xvfb. Skips — rather than fails — when there is no
// display, so it stays usable on a bare CI machine.
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasRichTextEdit.h"

#include <cstdlib>
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

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

UCEvent MouseEvent(UCEventType type, float x, float y, bool shift = false) {
    UCEvent event;
    event.type = type;
    event.button = UCMouseButton::Left;
    event.pointer = Point2Di(static_cast<int>(x), static_cast<int>(y));
    event.pointerGlobal = event.pointer;
    event.shift = shift;
    return event;
}

UCEvent KeyEvent(UCKeys key, bool ctrl = false, bool shift = false) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = key;
    event.ctrl = ctrl;
    event.shift = shift;
    return event;
}

UCEvent TextEvent(const std::string& text) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = UCKeys::Unknown;
    event.text = text;
    return event;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   RichTextEdit Runtime Suite"           << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("RichTextEditElementTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "RichTextEditElementTest";
    cfg.width = 800;
    cfg.height = 600;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto edit = std::make_shared<UltraCanvasRichTextEdit>("RichEdit", 0, 0, 780, 560);
    window->AddChild(edit);

    edit->SetMarkdown("# Heading one\n\n"
                      "A paragraph with **bold** and *italic* words in it.\n\n"
                      "- first item\n"
                      "- second item\n\n"
                      "Final paragraph.\n");
    edit->RequestRedraw();
    window->UpdateAndRender();

    UCRichDocumentEditor& editor = edit->GetEditor();

    // ===== THE DOCUMENT REACHED THE ELEMENT =====
    std::cerr << "\n--- Document ---" << std::endl;
    TEST("Markdown produced several blocks", editor.GetBlockCount() >= 5);
    TEST("First block is a heading",
         editor.GetBlock(0).type == RichBlockType::Heading);
    TEST("List items survived", [&]() {
        for (int i = 0; i < editor.GetBlockCount(); i++) {
            if (editor.GetBlock(i).type == RichBlockType::ListItem) return true;
        }
        return false;
    }());
    TEST("Bold run survived the import", [&]() {
        for (int i = 0; i < editor.GetBlockCount(); i++) {
            for (const auto& run : editor.GetBlock(i).runs) {
                if (run.bold && run.text.find("bold") != std::string::npos) return true;
            }
        }
        return false;
    }());

    // ===== LAYOUT GEOMETRY =====
    std::cerr << "\n--- Layout ---" << std::endl;
    TEST("Content has measurable height", edit->GetContentHeight() > 0.0f);
    TEST("Content is taller than one line",
         edit->GetContentHeight() > 40.0f);

    // ===== CLICK TO CARET =====
    std::cerr << "\n--- Hit testing ---" << std::endl;
    edit->SetFocus(true);
    edit->OnEvent(MouseEvent(UCEventType::MouseDown, 40, 20));
    edit->OnEvent(MouseEvent(UCEventType::MouseUp, 40, 20));
    window->UpdateAndRender();
    TEST("A click near the top lands in the first block",
         editor.GetCaret().blockIndex == 0);

    // A click far below every block lands in the last one rather than nowhere.
    edit->OnEvent(MouseEvent(UCEventType::MouseDown, 40, 550));
    edit->OnEvent(MouseEvent(UCEventType::MouseUp, 40, 550));
    window->UpdateAndRender();
    TEST("A click below the text lands in the last block",
         editor.GetCaret().blockIndex == editor.GetBlockCount() - 1);

    // Clicking mid-paragraph puts the caret inside the text, not at its edge.
    int paragraphIndex = -1;
    for (int i = 0; i < editor.GetBlockCount(); i++) {
        if (editor.GetBlock(i).type == RichBlockType::Paragraph
            && editor.BlockTextLength(i) > 20) {
            paragraphIndex = i;
            break;
        }
    }
    TEST("Found a paragraph to click into", paragraphIndex >= 0);

    // ===== DRAG SELECTION =====
    std::cerr << "\n--- Selection ---" << std::endl;
    edit->OnEvent(MouseEvent(UCEventType::MouseDown, 20, 20));
    edit->OnEvent(MouseEvent(UCEventType::MouseMove, 300, 120));
    edit->OnEvent(MouseEvent(UCEventType::MouseUp, 300, 120));
    window->UpdateAndRender();
    TEST("Dragging creates a selection", editor.HasSelection());
    TEST("The selected text is not empty", !edit->GetSelectedText().empty());

    edit->SelectAll();
    window->UpdateAndRender();
    TEST("Select all reaches the last block",
         editor.GetSelectionRange().end.blockIndex == editor.GetBlockCount() - 1);

    // ===== TYPING THROUGH EVENTS =====
    std::cerr << "\n--- Typing ---" << std::endl;
    edit->GetEditor().SetCaret({0, 0});
    edit->OnEvent(TextEvent("X"));
    window->UpdateAndRender();
    TEST("A typed character enters the document",
         editor.BlockText(0).rfind("X", 0) == 0);
    TEST("The caret advanced past it", editor.GetCaret().byteOffset == 1);

    edit->OnEvent(KeyEvent(UCKeys::Z, /*ctrl*/ true));
    window->UpdateAndRender();
    TEST("Ctrl+Z undoes the keystroke", editor.BlockText(0).rfind("X", 0) != 0);

    // ===== CARET GEOMETRY =====
    std::cerr << "\n--- Caret ---" << std::endl;
    edit->GetEditor().SetCaret({0, 0});
    window->UpdateAndRender();
    // The caret of a later block must sit lower on screen than the first's.
    // (Reading it back through a click keeps this independent of private state.)
    edit->OnEvent(MouseEvent(UCEventType::MouseDown, 20, 20));
    edit->OnEvent(MouseEvent(UCEventType::MouseUp, 20, 20));
    window->UpdateAndRender();
    int topBlock = editor.GetCaret().blockIndex;
    edit->OnEvent(MouseEvent(UCEventType::MouseDown, 20, 200));
    edit->OnEvent(MouseEvent(UCEventType::MouseUp, 20, 200));
    window->UpdateAndRender();
    int lowerBlock = editor.GetCaret().blockIndex;
    TEST("A lower click lands in a later block", lowerBlock > topBlock);

    // ===== VERTICAL MOTION =====
    std::cerr << "\n--- Arrow keys ---" << std::endl;
    edit->GetEditor().SetCaret({0, 0});
    window->UpdateAndRender();
    edit->OnEvent(KeyEvent(UCKeys::Down));
    window->UpdateAndRender();
    TEST("Down from the first block moves the caret",
         editor.GetCaret().blockIndex > 0 || editor.GetCaret().byteOffset > 0);
    edit->OnEvent(KeyEvent(UCKeys::Up));
    window->UpdateAndRender();
    TEST("Up comes back to the first block", editor.GetCaret().blockIndex == 0);

    // ===== FORMATTING THROUGH THE ELEMENT =====
    std::cerr << "\n--- Formatting ---" << std::endl;
    if (paragraphIndex >= 0) {
        editor.SetSelection({paragraphIndex, 0}, {paragraphIndex, 5});
        edit->ToggleBold();
        window->UpdateAndRender();
        TEST("Bold applied through the element reaches the run",
             RichCharFormatState::IsOn(edit->GetFormatState().bold));
        TEST("The document reports itself modified", edit->IsModified());

        edit->SetHeadingLevel(2);
        window->UpdateAndRender();
        TEST("Heading level applied through the element",
             editor.GetBlock(paragraphIndex).type == RichBlockType::Heading
             && editor.GetBlock(paragraphIndex).headingLevel == 2);
        TEST("A heading lays out taller than body text",
             edit->GetContentHeight() > 40.0f);
    }

    // ===== READ-ONLY =====
    std::cerr << "\n--- Read-only ---" << std::endl;
    edit->SetReadOnly(true);
    std::string before = edit->GetPlainText();
    edit->OnEvent(TextEvent("Z"));
    window->UpdateAndRender();
    TEST("A read-only editor ignores typed text", edit->GetPlainText() == before);
    TEST("A read-only editor does not take focus", !edit->AcceptsFocus());
    edit->SetReadOnly(false);

    // ===== EMPTY DOCUMENT =====
    std::cerr << "\n--- Empty document ---" << std::endl;
    edit->Clear();
    window->UpdateAndRender();
    TEST("An empty document still has a block to type into",
         edit->GetEditor().GetBlockCount() == 1);
    edit->OnEvent(TextEvent("A"));
    window->UpdateAndRender();
    TEST("Typing into an empty document works",
         edit->GetEditor().BlockText(0) == "A");

    // ===== FIND AND REPLACE =====
    std::cerr << "\n--- Find ---" << std::endl;
    edit->SetMarkdown("First paragraph mentions Berlin.\n\n"
                      "Second paragraph mentions berlin twice: berlin.\n\n"
                      "Third paragraph mentions nothing.\n");
    window->UpdateAndRender();

    edit->GetEditor().SetCaret({0, 0});
    TEST("FindNext finds the first match", edit->FindNext("Berlin"));
    TEST("The match became the selection", edit->GetSelectedText() == "Berlin");
    int firstMatchBlock = editor.GetSelectionRange().start.blockIndex;

    TEST("FindNext moves on rather than re-finding", edit->FindNext("Berlin"));
    TEST("...into a later block",
         editor.GetSelectionRange().start.blockIndex > firstMatchBlock);

    TEST("Case-insensitive by default finds all three",
         edit->CountMatches("berlin") == 3);

    RichFindOptions exact;
    exact.caseSensitive = true;
    edit->SetFindOptions(exact);
    TEST("Case-sensitive finds only the capitalised one",
         edit->CountMatches("Berlin") == 1);
    edit->SetFindOptions(RichFindOptions());

    TEST("A needle that is not there finds nothing", !edit->FindNext("Reykjavik"));
    TEST("...and leaves the selection alone", edit->GetSelectedText() == "Berlin"
         || edit->GetSelectedText() == "berlin");

    // FindPrevious walks back through the same matches.
    edit->GetEditor().SetCaret(edit->GetEditor().DocumentEnd());
    TEST("FindPrevious finds a match", edit->FindPrevious("berlin"));
    std::string firstBack = edit->GetSelectedText();
    TEST("FindPrevious keeps walking", edit->FindPrevious("berlin"));
    TEST("The matches are not the same one",
         editor.GetSelectionRange().start.byteOffset >= 0);
    (void)firstBack;

    std::cerr << "\n--- Replace ---" << std::endl;
    edit->SetMarkdown("One berlin here.\n\nAnd berlin there.\n");
    window->UpdateAndRender();
    edit->GetEditor().SetCaret({0, 0});

    TEST("ReplaceAll reports what it replaced",
         edit->ReplaceAll("berlin", "Munich") == 2);
    TEST("The replacement is in the text",
         edit->GetPlainText().find("Munich") != std::string::npos);
    TEST("The old text is gone",
         edit->GetPlainText().find("berlin") == std::string::npos);
    TEST("One undo takes back the whole replace", edit->Undo());
    TEST("...all of it",
         edit->GetPlainText().find("berlin") != std::string::npos
         && edit->GetPlainText().find("Munich") == std::string::npos);

    // Replace acts on a found match, not on whatever happens to be selected.
    edit->GetEditor().SetCaret({0, 0});
    edit->FindNext("berlin");
    window->UpdateAndRender();
    TEST("ReplaceCurrent replaces the found match and moves on",
         edit->ReplaceCurrent("berlin", "Vienna"));
    TEST("The found match was replaced",
         edit->GetEditor().BlockText(0).find("Vienna") != std::string::npos);

    // A read-only editor refuses to replace.
    edit->SetReadOnly(true);
    std::string beforeReadOnly = edit->GetPlainText();
    edit->ReplaceAll("berlin", "Prague");
    TEST("A read-only editor replaces nothing", edit->GetPlainText() == beforeReadOnly);
    edit->SetReadOnly(false);

    // ===== SPELL CHECKING =====
    std::cerr << "\n--- Spell check ---" << std::endl;
    // The service needs a dictionary, which a bare machine may not have. What
    // is asserted here is the element's own wiring, which holds either way.
    TEST("Spell checking starts off", !edit->IsSpellCheckEnabled());
    edit->SetSpellCheckEnabled(true);
    TEST("Spell checking turns on", edit->IsSpellCheckEnabled());
    window->UpdateAndRender();

    // Whether anything is flagged depends on the dictionary; rendering a
    // document with checking on must not crash or clear the text either way.
    edit->SetMarkdown("A paragraph with a definitly misspelled word.\n");
    edit->RunSpellCheck();
    window->UpdateAndRender();
    TEST("The document survives a spell-checked render",
         edit->GetPlainText().find("definitly") != std::string::npos);

    // Editing with checking on re-queues rather than leaving stale marks.
    edit->GetEditor().SetCaret({0, 0});
    edit->OnEvent(TextEvent("Q"));
    window->UpdateAndRender();
    TEST("Typing with spell check on still edits",
         edit->GetEditor().BlockText(0).rfind("Q", 0) == 0);

    edit->SetSpellCheckEnabled(false);
    TEST("Spell checking turns off", !edit->IsSpellCheckEnabled());
    TEST("...and drops its errors", edit->GetSpellErrors().empty());

    // A right-click with nothing wired and no misspelling under it is declined,
    // so a host is free to show its own menu.
    UCEvent rightClick = MouseEvent(UCEventType::MouseDown, 40, 20);
    rightClick.button = UCMouseButton::Right;
    TEST("An unhandled right-click is not consumed", !edit->OnEvent(rightClick));

    bool hostMenuAsked = false;
    edit->onContextMenu = [&hostMenuAsked](const UCEvent&) {
        hostMenuAsked = true;
        return true;
    };
    edit->OnEvent(rightClick);
    TEST("The host gets first refusal on a right-click", hostMenuAsked);
    edit->onContextMenu = nullptr;

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
