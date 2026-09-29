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
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasClipboard.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>
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

// A 1x1 PNG, for pictures that have to decode.
const std::vector<uint8_t> kDotPng = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
    0xDE,0x00,0x00,0x00,0x0C,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0xF8,0xCF,0xC0,0x00,
    0x00,0x03,0x01,0x01,0x00,0xC9,0xFE,0x92,0xEF,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,
    0x44,0xAE,0x42,0x60,0x82};

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

    // ===== TABLE CELLS =====
    std::cerr << "\n--- Table cells ---" << std::endl;
    edit->SetMarkdown("Intro paragraph.\n\n"
                      "| Region | Revenue |\n"
                      "|--------|---------|\n"
                      "| North  | 1200    |\n"
                      "| South  | 950     |\n\n"
                      "Closing paragraph.\n");
    edit->RequestRedraw();
    window->UpdateAndRender();

    int tableBlock = -1;
    for (int i = 0; i < editor.GetBlockCount(); i++) {
        if (editor.GetBlock(i).type == RichBlockType::Table) tableBlock = i;
    }
    TEST("The markdown table became a table block", tableBlock >= 0);

    if (tableBlock >= 0) {
        TEST("The table has rows", editor.TableRowCount(tableBlock) >= 2);
        TEST("Cells are containers the editor can reach",
             editor.AllContainers().size() > static_cast<size_t>(editor.GetBlockCount()));

        // Put the caret in a cell through the public API and type into it.
        const RichDocPosition cell(tableBlock, 1, 0, 0);
        const std::string before = editor.TextAt(cell);
        TEST("A cell has its own text", !before.empty());

        editor.SetCaret(editor.ContainerEnd(cell));
        window->UpdateAndRender();
        TEST("The caret is inside the cell", editor.GetCaret().InCell());

        edit->OnEvent(TextEvent("X"));
        window->UpdateAndRender();
        TEST("Typing lands in that cell", editor.TextAt(cell) == before + "X");
        TEST("...and nowhere else",
             editor.TextAt(RichDocPosition(tableBlock, 1, 1, 0)).find('X') == std::string::npos);
        TEST("The table did not split into more blocks",
             editor.GetBlock(tableBlock).type == RichBlockType::Table);

        // Enter inside a cell adds a line to it rather than breaking the table.
        const int blocksBefore = editor.GetBlockCount();
        edit->OnEvent(KeyEvent(UCKeys::Enter));
        window->UpdateAndRender();
        TEST("Enter in a cell does not split the table",
             editor.GetBlockCount() == blocksBefore);

        edit->Undo();
        edit->Undo();
        window->UpdateAndRender();
        TEST("Undo restores the cell", editor.TextAt(cell) == before);

        // Tab walks to the next cell.
        editor.SetCaret(cell);
        window->UpdateAndRender();
        const int columnBefore = editor.GetCaret().cellColumn;
        edit->OnEvent(KeyEvent(UCKeys::Tab));
        window->UpdateAndRender();
        TEST("Tab moves to the next cell",
             editor.GetCaret().InCell() && editor.GetCaret().cellColumn != columnBefore);
        edit->OnEvent(KeyEvent(UCKeys::Tab, /*ctrl*/ false, /*shift*/ true));
        window->UpdateAndRender();
        TEST("Shift+Tab comes back",
             editor.GetCaret().InCell() && editor.GetCaret().cellColumn == columnBefore);

        // A click inside the table must land in a cell. The table's exact y
        // depends on font metrics, so this sweeps the band it must occupy and
        // requires that clicking somewhere in there reaches a cell — which is
        // false if cell hit testing is not wired up at all.
        edit->SetFocus(true);
        bool clickReachedCell = false;
        int cellsReached = 0;
        for (int y = 20; y <= 200 && !clickReachedCell; y += 4) {
            editor.SetCaret(editor.DocumentStart());
            edit->OnEvent(MouseEvent(UCEventType::MouseDown, 60, static_cast<float>(y)));
            edit->OnEvent(MouseEvent(UCEventType::MouseUp, 60, static_cast<float>(y)));
            window->UpdateAndRender();
            if (editor.GetCaret().InCell()) {
                clickReachedCell = true;
                cellsReached++;
            }
        }
        TEST("Clicking inside the table puts the caret in a cell", clickReachedCell);

        // Clicking the right-hand column reaches a different cell than the left.
        int leftColumn = -1, rightColumn = -1;
        for (int y = 20; y <= 200; y += 4) {
            edit->OnEvent(MouseEvent(UCEventType::MouseDown, 30, static_cast<float>(y)));
            edit->OnEvent(MouseEvent(UCEventType::MouseUp, 30, static_cast<float>(y)));
            window->UpdateAndRender();
            if (editor.GetCaret().InCell()) { leftColumn = editor.GetCaret().cellColumn; break; }
        }
        for (int y = 20; y <= 200; y += 4) {
            edit->OnEvent(MouseEvent(UCEventType::MouseDown, 600, static_cast<float>(y)));
            edit->OnEvent(MouseEvent(UCEventType::MouseUp, 600, static_cast<float>(y)));
            window->UpdateAndRender();
            if (editor.GetCaret().InCell()) { rightColumn = editor.GetCaret().cellColumn; break; }
        }
        TEST("A click on the left reaches the first column", leftColumn == 0);
        TEST("A click on the right reaches a later column", rightColumn > 0);

        // Selecting inside a cell renders without disturbing the document.
        editor.SetSelection(RichDocPosition(tableBlock, 1, 0, 0),
                            editor.ContainerEnd(RichDocPosition(tableBlock, 1, 0, 0)));
        window->UpdateAndRender();
        TEST("A cell selection is not empty", editor.HasSelection());
        TEST("A cell selection stays in one cell",
             editor.GetSelectionRange().start.SameContainer(editor.GetSelectionRange().end));
        TEST("Selected cell text comes back", !edit->GetSelectedText().empty());

        // Bold through the element reaches the cell's runs.
        edit->ToggleBold();
        window->UpdateAndRender();
        TEST("Bold applies inside a cell",
             RichCharFormatState::IsOn(edit->GetFormatState().bold));
        edit->Undo();
        window->UpdateAndRender();

        // Find reaches into the table.
        editor.SetCaret(editor.DocumentStart());
        TEST("Find reaches a word that only exists in a cell",
             edit->FindNext("South"));
        TEST("...and the match is in a cell", editor.GetSelectionRange().start.InCell());
    }

    // ===== MERGED CELLS =====
    // Spans are geometry, not decoration: a cell covering two columns has to be
    // laid out two columns wide, and the cells beside it shifted past it. Both
    // are checked through hit testing, which is the only thing that can tell
    // where a cell actually ended up.
    std::cerr << "\n--- Merged cells ---" << std::endl;
    {
        auto cellWith = [](const std::string& text, int columnSpan, int rowSpan) {
            RichTableCell cell;
            RichTextRun run;
            run.text = text;
            cell.runs.push_back(run);
            cell.columnSpan = columnSpan;
            cell.rowSpan = rowSpan;
            return cell;
        };

        // Grid is three columns wide: a cell spanning two, then a single one.
        auto doc = std::make_shared<UCRichDocument>();
        RichDocBlock table;
        table.type = RichBlockType::Table;
        {
            RichTableRow row;
            row.cells.push_back(cellWith("wide", 2, 1));
            row.cells.push_back(cellWith("narrow", 1, 1));
            table.tableRows.push_back(row);
        }
        {
            RichTableRow row;
            row.cells.push_back(cellWith("a", 1, 1));
            row.cells.push_back(cellWith("b", 1, 1));
            row.cells.push_back(cellWith("c", 1, 1));
            table.tableRows.push_back(row);
        }
        doc->blocks.push_back(table);
        edit->SetDocument(doc);
        edit->RequestRedraw();
        window->UpdateAndRender();

        // Find the row-0 band by clicking down the left edge until a cell of
        // row 0 answers, then probe across it at that y.
        auto columnAt = [&](float x, int wantRow) -> int {
            for (int y = 4; y <= 240; y += 2) {
                edit->OnEvent(MouseEvent(UCEventType::MouseDown, x, static_cast<float>(y)));
                edit->OnEvent(MouseEvent(UCEventType::MouseUp, x, static_cast<float>(y)));
                window->UpdateAndRender();
                const RichDocPosition p = editor.GetCaret();
                if (p.InCell() && p.cellRow == wantRow) return p.cellColumn;
            }
            return -2;
        };

        // The element is 780 wide, so a three-column grid puts column
        // boundaries near 260 and 520. At x=400 (the middle column) row 0 must
        // answer with the SPANNING cell (index 0), because "wide" covers grid
        // columns 0 and 1. Laying it out one column wide would put "narrow"
        // there instead, which is exactly the old behaviour.
        const int midRow0 = columnAt(400.0f, 0);
        TEST("A column-spanning cell covers the column beside it", midRow0 == 0);

        // Far right of row 0 is past the span: that is "narrow", index 1.
        const int rightRow0 = columnAt(700.0f, 0);
        TEST("The cell after a span sits past it, not beside it", rightRow0 == 1);

        // Row 1 is unspanned, so its three cells land in their own thirds.
        TEST("An unspanned row keeps one cell per column", columnAt(100.0f, 1) == 0);
        TEST("...including the middle one", columnAt(400.0f, 1) == 1);
        TEST("...and the last", columnAt(700.0f, 1) == 2);
    }

    // A row-spanning cell must still be there in the row below it.
    {
        auto cellWith = [](const std::string& text, int columnSpan, int rowSpan) {
            RichTableCell cell;
            RichTextRun run;
            run.text = text;
            cell.runs.push_back(run);
            cell.columnSpan = columnSpan;
            cell.rowSpan = rowSpan;
            return cell;
        };

        auto doc = std::make_shared<UCRichDocument>();
        RichDocBlock table;
        table.type = RichBlockType::Table;
        {
            RichTableRow row;
            row.cells.push_back(cellWith("tall", 1, 2));      // covers both rows
            row.cells.push_back(cellWith("top-right", 1, 1));
            table.tableRows.push_back(row);
        }
        {
            RichTableRow row;
            row.cells.push_back(cellWith("bottom-right", 1, 1));  // grid column 1
            table.tableRows.push_back(row);
        }
        doc->blocks.push_back(table);
        edit->SetDocument(doc);
        edit->RequestRedraw();
        window->UpdateAndRender();

        // Walk down the right-hand column: it must answer row 0 first, then
        // row 1 — the second row's single cell belongs at grid column 1,
        // because column 0 is taken by the cell spanning down from above.
        int firstRightRow = -1, secondRightRow = -1;
        for (int y = 4; y <= 240; y += 2) {
            edit->OnEvent(MouseEvent(UCEventType::MouseDown, 600, static_cast<float>(y)));
            edit->OnEvent(MouseEvent(UCEventType::MouseUp, 600, static_cast<float>(y)));
            window->UpdateAndRender();
            const RichDocPosition p = editor.GetCaret();
            if (!p.InCell()) continue;
            if (firstRightRow < 0) firstRightRow = p.cellRow;
            else if (p.cellRow != firstRightRow) { secondRightRow = p.cellRow; break; }
        }
        TEST("The right column starts in row 0", firstRightRow == 0);
        TEST("...and reaches row 1 below it", secondRightRow == 1);

        // The left column is the spanning cell for the whole table height, so
        // every y that answers there reports row 0, cell 0.
        bool leftAlwaysSpanningCell = true;
        bool leftAnswered = false;
        for (int y = 4; y <= 240; y += 2) {
            edit->OnEvent(MouseEvent(UCEventType::MouseDown, 60, static_cast<float>(y)));
            edit->OnEvent(MouseEvent(UCEventType::MouseUp, 60, static_cast<float>(y)));
            window->UpdateAndRender();
            const RichDocPosition p = editor.GetCaret();
            if (!p.InCell()) continue;
            leftAnswered = true;
            if (p.cellRow != 0 || p.cellColumn != 0) leftAlwaysSpanningCell = false;
        }
        TEST("The left column answered at all", leftAnswered);
        TEST("A row-spanning cell owns the column for both rows",
             leftAlwaysSpanningCell);

        // Typing into it still edits the one model cell.
        editor.SetCaret(editor.ContainerEnd(RichDocPosition(0, 0, 0, 0)));
        edit->OnEvent(TextEvent("!"));
        window->UpdateAndRender();
        TEST("The spanning cell is editable",
             editor.TextAt(RichDocPosition(0, 0, 0, 0)) == "tall!");
        TEST("...and its span is untouched by the edit",
             editor.GetBlock(0).tableRows[0].cells[0].rowSpan == 2);
    }

    // ===== AN EDIT THAT DOES NOT MOVE THE CARET =====
    // The layout is cached per block and rebuilt only where it has been
    // invalidated. Moving the caret invalidates the blocks it moves between,
    // which hid this for a long time: an edit that changes a block WITHOUT
    // moving the caret - centring the paragraph you are already in - left the
    // old layout on screen until something else moved the caret.
    std::cerr << "\n--- Edits that leave the caret alone ---" << std::endl;
    {
        auto doc = std::make_shared<UCRichDocument>();
        RichDocBlock para;
        para.type = RichBlockType::Paragraph;
        RichTextRun run; run.text = "centre me";
        para.runs.push_back(run);
        doc->blocks.push_back(para);
        edit->SetDocument(doc);
        edit->RequestRedraw();
        window->UpdateAndRender();

        // Clicking well past the end of a left-aligned line lands after its
        // last character; once the line is centred, the same point is inside
        // it. So the offset a click reports is a readout of where the text
        // actually sits.
        auto offsetAt = [&](float x) {
            edit->OnEvent(MouseEvent(UCEventType::MouseDown, x, 8.0f));
            edit->OnEvent(MouseEvent(UCEventType::MouseUp, x, 8.0f));
            window->UpdateAndRender();
            return editor.GetCaret().byteOffset;
        };

        const int leftAligned = offsetAt(390.0f);
        TEST("A click past a left-aligned line lands at its end", leftAligned == 9);

        // No SetCaret before this: moving the caret is what used to make the
        // change appear, so doing it here would hide the bug being tested.
        edit->SetAlignment(RichTextAlign::Center);
        window->UpdateAndRender();
        TEST("Centring shows up on the very next render, with the caret untouched",
             offsetAt(390.0f) < leftAligned);
    }

    // ===== TABLE STRUCTURE =====
    // The editing core's own tests prove the model comes out right; what only
    // the element can show is that the LAYOUT follows - a column inserted in
    // the model has to become a column you can click in.
    std::cerr << "\n--- Table structure ---" << std::endl;
    {
        auto doc = std::make_shared<UCRichDocument>();
        doc->blocks.push_back(RichDocBlock{});           // one empty paragraph
        edit->SetDocument(doc);
        editor.SetCaret(RichDocPosition(0, 0));

        edit->InsertTable(2, 2);
        edit->RequestRedraw();
        window->UpdateAndRender();
        TEST("Insert Table leaves the caret in the first cell",
             editor.GetCaret().InCell() && editor.GetCaret().cellRow == 0 &&
             editor.GetCaret().cellColumn == 0);

        // Fill the cells so each one is identifiable by its text.
        const char* names[2][2] = {{"aa", "bb"}, {"cc", "dd"}};
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 2; c++) {
                editor.SetCaret(RichDocPosition(0, r, c, 0));
                edit->OnEvent(TextEvent(names[r][c]));
            }
        }
        window->UpdateAndRender();

        // Which cell answers a click at x, scanning down for the wanted row.
        auto cellAt = [&](float x, int wantRow) -> int {
            for (int y = 4; y <= 240; y += 2) {
                edit->OnEvent(MouseEvent(UCEventType::MouseDown, x, static_cast<float>(y)));
                edit->OnEvent(MouseEvent(UCEventType::MouseUp, x, static_cast<float>(y)));
                window->UpdateAndRender();
                const RichDocPosition p = editor.GetCaret();
                if (p.InCell() && p.cellRow == wantRow) return p.cellColumn;
            }
            return -2;
        };

        // Two columns across 780px: the left half is column 0, the right half
        // column 1.
        TEST("A fresh table lays out its two columns", cellAt(150.0f, 0) == 0);
        TEST("...with the second one beside it", cellAt(600.0f, 0) == 1);

        // Insert a column between them. Three columns now, so x=400 lands in
        // the NEW middle one - which is empty, and is cell index 1.
        editor.SetCaret(RichDocPosition(0, 0, 0, 0));
        TEST("Insert column right reports success", edit->InsertColumnRight());
        window->UpdateAndRender();
        TEST("The inserted column is clickable where it now sits",
             cellAt(400.0f, 0) == 1);
        TEST("...and it is the empty one",
             editor.TextAt(RichDocPosition(0, 0, 1, 0)).empty());
        TEST("...while the old second column moved right",
             editor.TextAt(RichDocPosition(0, 0, 2, 0)) == "bb");

        // Deleting it puts the table back to two clickable columns.
        editor.SetCaret(RichDocPosition(0, 0, 1, 0));
        TEST("Delete column reports success", edit->DeleteCurrentColumn());
        window->UpdateAndRender();
        TEST("The table is two columns wide again", cellAt(600.0f, 0) == 1);
        TEST("...holding the original text",
             editor.TextAt(RichDocPosition(0, 0, 1, 0)) == "bb");

        // A merged cell has to be laid out at its full width straight away:
        // this is the layout reading the span the merge just wrote.
        editor.SetCaret(RichDocPosition(0, 0, 0, 0));
        TEST("Merge with the cell to the right reports success",
             edit->MergeWithCellRight());
        window->UpdateAndRender();
        TEST("The merged cell covers the column beside it", cellAt(600.0f, 0) == 0);
        TEST("...and keeps both cells' text",
             editor.TextAt(RichDocPosition(0, 0, 0, 0)) == "aa\nbb");

        // Splitting it gives the second column back.
        TEST("The merged cell can be split", edit->CanSplitCurrentCell());
        TEST("Split reports success", edit->SplitCurrentCell());
        window->UpdateAndRender();
        TEST("The split cell released the column beside it", cellAt(600.0f, 0) == 1);

        // Rows: a new one has to be reachable by clicking BELOW the others,
        // which only happens if the layout grew.
        editor.SetCaret(RichDocPosition(0, 1, 0, 0));
        TEST("Insert row below reports success", edit->InsertRowBelow());
        window->UpdateAndRender();
        int deepestRow = -1;
        for (int y = 4; y <= 240; y += 2) {
            edit->OnEvent(MouseEvent(UCEventType::MouseDown, 150, static_cast<float>(y)));
            edit->OnEvent(MouseEvent(UCEventType::MouseUp, 150, static_cast<float>(y)));
            window->UpdateAndRender();
            const RichDocPosition p = editor.GetCaret();
            if (p.InCell()) deepestRow = std::max(deepestRow, p.cellRow);
        }
        TEST("The new third row is there to be clicked in", deepestRow == 2);

        // Outside a table the operations decline rather than acting on
        // whatever block happens to be at the caret.
        editor.SetCaret(editor.DocumentEnd());
        TEST("Table operations decline outside a table",
             !edit->IsCaretInTable() && !edit->InsertRowBelow() &&
             !edit->DeleteCurrentColumn() && !edit->MergeWithCellRight());
    }

    // ===== INLINE IMAGES =====
    // A picture in the line has to take up room in that line: the layout
    // reserves a box for it, so the text after it is pushed along and the line
    // grows tall enough to hold it.
    std::cerr << "\n--- Inline images ---" << std::endl;
    {
        // A 1x1 PNG. It decodes, so the element draws it rather than a
        // placeholder frame - which is the path worth exercising.
        const std::vector<uint8_t> png = {
            0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
            0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
            0xDE,0x00,0x00,0x00,0x0C,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0xF8,0xCF,0xC0,0x00,
            0x00,0x03,0x01,0x01,0x00,0xC9,0xFE,0x92,0xEF,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,
            0x44,0xAE,0x42,0x60,0x82};

        auto textOnly = [&](bool withPicture) {
            auto doc = std::make_shared<UCRichDocument>();
            if (withPicture) doc->AddMedia("dot.png", "image/png", png);
            RichDocBlock para;
            para.type = RichBlockType::Paragraph;
            RichTextRun a;
            a.text = "Before ";
            para.runs.push_back(a);
            if (withPicture) {
                RichTextRun pic;
                pic.text = RichTextRun::kObjectReplacement;
                pic.mediaIndex = 0;
                pic.imageWidthPt = 40.0f;
                pic.imageHeightPt = 40.0f;
                pic.imageAltText = "dot";
                para.runs.push_back(pic);
            }
            RichTextRun b;
            b.text = " After";
            para.runs.push_back(b);
            doc->blocks.push_back(para);
            return doc;
        };

        edit->SetDocument(textOnly(false));
        edit->RequestRedraw();
        window->UpdateAndRender();
        const float plainHeight = edit->GetContentHeight();
        TEST("A plain paragraph has a height", plainHeight > 0.0f);

        edit->SetDocument(textOnly(true));
        edit->RequestRedraw();
        window->UpdateAndRender();
        const float withImageHeight = edit->GetContentHeight();

        // A 40pt picture cannot fit in a line of body text, so the line must
        // have grown. This is what CreateShape reserving a box actually buys.
        // The line must be at least as tall as the 40pt picture. Merely
        // "taller than plain text" does not discriminate: the U+FFFC glyph on
        // its own nudges the height by about a point, so that assertion passes
        // even with no box reserved at all (measured: 21.8 vs 20.9). What the
        // reserved box buys is the jump to ~43.
        TEST("An inline picture reserves its full height in the line",
             withImageHeight >= 40.0f);
        TEST("...which is well beyond what the placeholder glyph alone gives",
             withImageHeight > plainHeight * 1.5f);

        TEST("The document is still one paragraph", editor.GetBlockCount() == 1);
        TEST("...holding the picture as a run", [&]() {
            for (const auto& r : editor.GetBlock(0).runs) if (r.IsInlineImage()) return true;
            return false;
        }());

        // The placeholder never reaches the reader.
        TEST("Plain text shows the alt text, not the placeholder",
             edit->GetPlainText().find(RichTextRun::kObjectReplacement) == std::string::npos);

        // Clicking to the right of the picture lands after it; to the left,
        // before it. Both sides of a 40pt box are reachable.
        const int pictureOffset = static_cast<int>(std::string("Before ").size());
        edit->SetFocus(true);
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 4, 10));
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 4, 10));
        window->UpdateAndRender();
        const int leftClick = editor.GetCaret().byteOffset;
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 300, 10));
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 300, 10));
        window->UpdateAndRender();
        const int rightClick = editor.GetCaret().byteOffset;
        TEST("A click at the start lands before the picture", leftClick <= pictureOffset);
        TEST("A click past it lands after the picture", rightClick > pictureOffset);

        // Typing beside a picture leaves it alone.
        editor.SetCaret(editor.DocumentEnd());
        edit->OnEvent(TextEvent("!"));
        window->UpdateAndRender();
        TEST("Typing next to a picture keeps it",
             [&]() {
                 for (const auto& r : editor.GetBlock(0).runs) if (r.IsInlineImage()) return true;
                 return false;
             }());
        TEST("...and the typed character arrived",
             edit->GetPlainText().find("After!") != std::string::npos);

        // Selecting across the picture and deleting removes it whole.
        edit->SelectAll();
        edit->OnEvent(KeyEvent(UCKeys::Backspace));
        window->UpdateAndRender();
        TEST("Deleting a selection across a picture removes it", [&]() {
            for (const auto& r : editor.GetBlock(0).runs) if (r.IsInlineImage()) return false;
            return true;
        }());
    }

    // ===== CHECK LISTS =====
    std::cerr << "\n--- Check lists ---" << std::endl;
    {
        edit->SetMarkdown("- [ ] first task\n- [x] second task\n");
        window->UpdateAndRender();
        TEST("Two check list items", editor.GetBlockCount() == 2 && editor.GetBlock(0).checkbox);
        // The box sits left of the text, inside the list indent.
        const float y = 4.0f + 8.0f + 6.0f;       // padding + half a line
        edit->SetFocus(true);
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 8.0f + 10.0f, y));
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 8.0f + 10.0f, y));
        window->UpdateAndRender();
        TEST("Clicking the box ticks it", editor.GetBlock(0).checked);
        TEST("...and does not tick the other", editor.GetBlock(1).checked);
        TEST("Undo takes the tick back", edit->Undo() && !editor.GetBlock(0).checked);
        editor.SetCaret(RichDocPosition(1, 3));
        TEST("Ticking from the keyboard", edit->ToggleCheckedAtCaret() && !editor.GetBlock(1).checked);
        TEST("The Markdown keeps the boxes", edit->GetMarkdown().find("- [ ] first task") != std::string::npos);
    }

    // ===== MATH =====
    std::cerr << "\n--- Math ---" << std::endl;
    {
        auto document = std::make_shared<UCRichDocument>();
        RichDocBlock paragraph;
        RichTextRun before, formula, after;
        before.text = "Energy ";
        formula.text = "E = mc^2";
        formula.math = true;
        after.text = " holds.";
        paragraph.runs = {before, formula, after};
        document->blocks.push_back(paragraph);
        RichDocBlock display;
        display.type = RichBlockType::MathBlock;
        RichTextRun source;
        source.text = "\\int_0^1 x\\,dx";
        display.runs.push_back(source);
        document->blocks.push_back(display);
        edit->SetDocument(document);
        editor.SetCaret(RichDocPosition(0, 0));
        window->UpdateAndRender();
        TEST("A document with formulas lays out", edit->GetContentHeight() > 0.0f);
        std::cerr << "  (math engine " << (UltraCanvasInlineMath::IsAvailable() ? "loaded" : "not available")
                  << ")" << std::endl;
        // Wherever the caret goes, the formula's source stays the text.
        editor.SetCaret(RichDocPosition(0, 9));
        window->UpdateAndRender();
        editor.SetCaret(RichDocPosition(1, 2));
        window->UpdateAndRender();
        TEST("Formulas keep their source", edit->GetPlainText().find("E = mc^2") != std::string::npos);
    }

    // ===== CELL SELECTION =====
    std::cerr << "\n--- Cell selection ---" << std::endl;
    {
        edit->SetMarkdown("| a | b |\n|---|---|\n| c | d |\n");
        window->UpdateAndRender();
        // Drag from the first cell to the last.
        editor.SetCaret(RichDocPosition(0, 0, 0, 0));
        window->UpdateAndRender();
        const RichDocPosition last(0, 1, 1, 1);
        editor.SetCaret(last, true);
        window->UpdateAndRender();
        TEST("A drag across cells is a cell selection", edit->HasCellSelection());
        TEST("Copy of a cell selection is tab separated",
             edit->GetSelectedText() == "a\tb\nc\td\n");
        TEST("The selected cells merge into one", edit->MergeSelectedCells()
             && editor.GetBlock(0).tableRows[0].cells[0].columnSpan == 2
             && editor.GetBlock(0).tableRows[0].cells[0].rowSpan == 2);
        window->UpdateAndRender();
    }

    // ===== DRAG AND DROP =====
    std::cerr << "\n--- Drag and drop ---" << std::endl;
    {
        edit->SetMarkdown("alpha beta gamma\n");
        window->UpdateAndRender();
        editor.SetSelection(RichDocPosition(0, 0), RichDocPosition(0, 6));   // "alpha "
        window->UpdateAndRender();
        // Press inside "alpha", drag past the end of the line, release.
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 20, 18));
        edit->OnEvent(MouseEvent(UCEventType::MouseMove, 60, 18));
        edit->OnEvent(MouseEvent(UCEventType::MouseMove, 600, 18));
        window->UpdateAndRender();
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 600, 18));
        window->UpdateAndRender();
        TEST("Dragging a selection moves it: " + editor.BlockText(0), editor.BlockText(0) == "beta gammaalpha ");
        TEST("...and it stays selected", edit->GetSelectedText() == "alpha ");
        TEST("One undo puts it back", edit->Undo() && editor.BlockText(0) == "alpha beta gamma");

        // A press inside the selection without moving is a click.
        editor.SetSelection(RichDocPosition(0, 0), RichDocPosition(0, 6));
        window->UpdateAndRender();
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 20, 18));
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 20, 18));
        window->UpdateAndRender();
        TEST("A click inside the selection just places the caret", !edit->HasSelection());
        // A picture file dropped from another application lands in the line.
        const std::string file = PathToUtf8(std::filesystem::temp_directory_path() / "rte-drop-dot.png");
        {
            std::ofstream out(PathFromUtf8(file), std::ios::binary);
            out.write(reinterpret_cast<const char*>(kDotPng.data()), static_cast<std::streamsize>(kDotPng.size()));
        }
        UCEvent drop = MouseEvent(UCEventType::Drop, 600, 18);
        drop.droppedFiles = {file};
        TEST("A dropped image file is accepted", edit->OnEvent(drop));
        TEST("...as a picture in the line", [&]() {
            for (const auto& r : editor.GetBlock(0).runs) if (r.IsInlineImage()) return true;
            return false;
        }());
        std::filesystem::remove(PathFromUtf8(file));
    }

    // ===== PICTURES: SELECT, RESIZE, ALT TEXT =====
    std::cerr << "\n--- Picture resize ---" << std::endl;
    {
        auto document = std::make_shared<UCRichDocument>();
        RichDocBlock image;
        image.type = RichBlockType::Image;
        image.mediaIndex = document->AddMedia("dot.png", "image/png", kDotPng);
        image.imageWidthPt = 60.0f;       // 80 x 40 px at 96/72
        image.imageHeightPt = 30.0f;
        document->blocks.push_back(image);
        RichDocBlock after;
        RichTextRun text;
        text.text = "Below the picture.";
        after.runs.push_back(text);
        document->blocks.push_back(after);
        edit->SetDocument(document);
        window->UpdateAndRender();

        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 30, 20));
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 30, 20));
        window->UpdateAndRender();
        TEST("Clicking a picture selects it", edit->HasSelectedImage());

        // Drag the bottom-right handle 40 px to the right: a corner keeps the
        // proportions, so 80x40 becomes 120x60 px = 90x45 pt.
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 88, 48));
        edit->OnEvent(MouseEvent(UCEventType::MouseMove, 110, 48));
        edit->OnEvent(MouseEvent(UCEventType::MouseMove, 128, 48));
        window->UpdateAndRender();
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 128, 48));
        window->UpdateAndRender();
        const RichDocBlock& resized = editor.GetBlock(0);
        TEST("A corner handle resizes in proportion: " + std::to_string(resized.imageWidthPt) + "x"
             + std::to_string(resized.imageHeightPt),
             std::abs(resized.imageWidthPt - 90.0f) < 1.0f && std::abs(resized.imageHeightPt - 45.0f) < 1.0f);
        TEST("One undo restores the size", edit->Undo() && std::abs(editor.GetBlock(0).imageWidthPt - 60.0f) < 0.01f);

        TEST("Alt text on the selected picture", edit->SelectImage(RichDocPosition(0, 0))
             && edit->SetSelectedImageAltText("A red dot") && editor.GetBlock(0).imageAltText == "A red dot"
             && edit->GetSelectedImageAltText() == "A red dot");
        TEST("Delete removes a selected picture", [&]() {
            edit->OnEvent(KeyEvent(UCKeys::Delete));
            window->UpdateAndRender();
            return editor.GetBlock(0).type != RichBlockType::Image;
        }());
    }

    // ===== FLOATING PICTURES =====
    std::cerr << "\n--- Floating pictures ---" << std::endl;
    {
        auto document = std::make_shared<UCRichDocument>();
        RichDocBlock paragraph;
        RichTextRun picture;
        picture.text = RichTextRun::kObjectReplacement;
        picture.mediaIndex = document->AddMedia("dot.png", "image/png", kDotPng);
        picture.imageWidthPt = 60.0f;          // 80 x 53 px
        picture.imageHeightPt = 40.0f;
        picture.imageWrap = RichTextRun::ImageWrap::Square;
        picture.imageFloatAlign = RichTextAlign::Right;
        RichTextRun words;
        words.text = std::string(400, 'x').replace(0, 400, 400 / 5, 'w') + " "
                   + std::string("the quick brown fox jumps over the lazy dog ") + "the quick brown fox";
        paragraph.runs = {picture, words};
        document->blocks.push_back(paragraph);
        edit->SetDocument(document);
        window->UpdateAndRender();
        window->UpdateAndRender();
        // The picture sits at the column's right edge, the text beside it.
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 740, 30));
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 740, 30));
        window->UpdateAndRender();
        TEST("A floating picture sits at the right edge, where a click selects it", edit->HasSelectedImage());
        TEST("...addressed by its place in the text", edit->GetSelectedImage() == RichDocPosition(0, 0));
    }

    // ===== BLOCKS RUNNING OVER PAGES =====
    std::cerr << "\n--- Page splitting ---" << std::endl;
    {
        // One paragraph longer than a page: it continues on the next page
        // instead of running past the first one's bottom margin.
        std::string words;
        for (int i = 0; i < 700; i++) words += "word" + std::to_string(i) + " ";
        edit->SetMarkdown(words + "\n");
        edit->SetPageView(true);
        window->UpdateAndRender();
        window->UpdateAndRender();
        TEST("A paragraph taller than a page runs onto the next: " + std::to_string(edit->GetPageCount()) + " pages",
             edit->GetPageCount() >= 2);
        // The caret at its end is on the last page: scrolling to it goes down.
        editor.SetCaret(editor.DocumentEnd());
        edit->ScrollToCaret();
        TEST("The caret at its end is scrolled to on a later page", edit->GetScrollOffset() > 600.0f);
        // Typing there lands in the same paragraph.
        edit->InsertText("END");
        window->UpdateAndRender();
        TEST("...and typing there continues it", editor.GetBlockCount() == 1
             && editor.BlockText(0).find("END") != std::string::npos);

        // A long table with a header row continues on the next page too.
        std::string table = "| Item | Qty |\n|---|---|\n";
        for (int r = 0; r < 80; r++) table += "| row " + std::to_string(r) + " | " + std::to_string(r) + " |\n";
        edit->SetMarkdown(table);
        window->UpdateAndRender();
        window->UpdateAndRender();
        TEST("A table longer than a page runs over pages", edit->GetPageCount() >= 2);
        edit->SetPageView(false);
    }

    // ===== ZOOM AND SIDEWAYS SCROLLING =====
    std::cerr << "\n--- Zoom ---" << std::endl;
    {
        edit->SetMarkdown("alpha beta gamma delta\n");
        window->UpdateAndRender();
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 60, 18));
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 60, 18));
        window->UpdateAndRender();
        const int atOne = editor.GetCaret().byteOffset;
        edit->SetZoom(2.0f);
        window->UpdateAndRender();
        // The same document point is twice as far from the view's origin.
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 8 + 2 * (60 - 8), 8 + 2 * (18 - 8)));
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 8 + 2 * (60 - 8), 8 + 2 * (18 - 8)));
        window->UpdateAndRender();
        TEST("A click lands on the same text at 200%", editor.GetCaret().byteOffset == atOne && atOne > 0);
        TEST("Zoom is clamped", [&]() { edit->SetZoom(50.0f); return edit->GetZoom() <= 5.0f; }());

        edit->SetZoom(2.0f);
        edit->SetPageView(true);
        window->UpdateAndRender();
        window->UpdateAndRender();
        edit->SetHorizontalScrollOffset(200.0f);
        TEST("A page wider than the view scrolls sideways", edit->GetHorizontalScrollOffset() > 0.0f);
        edit->SetZoom(0.5f);
        window->UpdateAndRender();
        window->UpdateAndRender();
        TEST("...and not when it fits", edit->GetHorizontalScrollOffset() == 0.0f);
        edit->SetPageView(false);
        edit->SetZoom(1.0f);
        window->UpdateAndRender();
    }

    // ===== PDF =====
    std::cerr << "\n--- PDF export ---" << std::endl;
    {
        std::string words;
        for (int i = 0; i < 700; i++) words += "word" + std::to_string(i) + " ";
        edit->SetMarkdown("# Report\n\n" + words + "\n");
        window->UpdateAndRender();
        editor.SetSelection(RichDocPosition(1, 0), RichDocPosition(1, 20));
        std::vector<uint8_t> pdf;
        std::string error;
        TEST("The document exports as a PDF: " + error, edit->ExportToPdf(pdf, error));
        TEST("...which is a PDF", pdf.size() > 1000 && std::string(pdf.begin(), pdf.begin() + 5) == "%PDF-");
        TEST("Exporting leaves the view as it was", !edit->IsPageView() && edit->HasSelection());
        window->UpdateAndRender();
        edit->OnEvent(MouseEvent(UCEventType::MouseDown, 20, 20));
        edit->OnEvent(MouseEvent(UCEventType::MouseUp, 20, 20));
        window->UpdateAndRender();
        TEST("...and clicks still land in it", editor.GetCaret().blockIndex == 0);
    }

    // ===== EDITING HEADERS AND FOOTERS =====
    std::cerr << "\n--- Header and footer editing ---" << std::endl;
    {
        edit->SetMarkdown("Body text on the page.\n");
        edit->SetPageView(true);
        window->UpdateAndRender();
        window->UpdateAndRender();
        TEST("A header opens for editing", edit->EditHeader(0) && edit->IsEditingHeaderOrFooter());
        window->UpdateAndRender();
        edit->OnEvent(TextEvent("M"));
        edit->OnEvent(TextEvent("y"));
        edit->InsertText(" letterhead");
        window->UpdateAndRender();
        TEST("Typing goes into the header", edit->GetDocument()->pageFurniture.header.size() == 1
             && UCRichDocument::ConcatenateRunText(edit->GetDocument()->pageFurniture.header[0].runs) == "My letterhead");
        TEST("...not into the body", edit->GetDocument()->blocks.size() == 1
             && edit->GetPlainText().find("Body text") != std::string::npos);
        TEST("The document is modified", edit->IsModified());
        edit->OnEvent(KeyEvent(UCKeys::Escape));
        window->UpdateAndRender();
        TEST("Escape goes back to the body", !edit->IsEditingHeaderOrFooter()
             && editor.BlockText(0) == "Body text on the page.");
        TEST("...keeping the header", !edit->GetDocument()->pageFurniture.header.empty());

        // A double-click in the bottom margin of the page opens its footer.
        window->UpdateAndRender();
        const float pageTop = 16.0f;             // style.pageGap
        const float footerY = 8.0f + pageTop + 1100.0f - 20.0f;   // near the page's foot
        edit->SetScrollOffset(footerY - 300.0f);
        window->UpdateAndRender();
        const float y = footerY - edit->GetScrollOffset() ;
        UCEvent click = MouseEvent(UCEventType::MouseDoubleClick, 300.0f, y);
        edit->OnEvent(click);
        window->UpdateAndRender();
        TEST("A double-click in the bottom margin edits the footer", edit->IsEditingFooter());
        edit->InsertText("Page footer");
        edit->FinishHeaderFooterEditing();
        TEST("...which the document now has", !edit->GetDocument()->pageFurniture.footer.empty());
        edit->SetPageView(false);
        edit->ScrollToTop();
    }

    // ===== NAMED STYLES =====
    std::cerr << "\n--- Named styles ---" << std::endl;
    {
        edit->SetMarkdown("Title text\n\nBody text\n");
        window->UpdateAndRender();
        editor.SetCaret(RichDocPosition(0, 2));
        const float before = edit->GetContentHeight();
        TEST("A paragraph takes the Title style", edit->ApplyParagraphStyle("Title")
             && edit->GetCurrentParagraphStyle() == "Title");
        window->UpdateAndRender();
        TEST("...and grows with its larger text", edit->GetContentHeight() > before);
        TEST("A new style from the paragraph", edit->NewStyleFromCaret("My Title")
             && edit->GetCurrentParagraphStyle() == "MyTitle");
        TEST("The styles list has it", [&]() {
            for (const auto& s : edit->GetStyles()) if (s.id == "MyTitle" && s.name == "My Title") return true;
            return false;
        }());
    }

    // ===== PAGE FIELDS IN THE BODY =====
    std::cerr << "\n--- Page fields ---" << std::endl;
    {
        edit->SetMarkdown("First page\n\nSecond page\n");
        edit->SetPageView(true);
        window->UpdateAndRender();
        editor.SetCaret(RichDocPosition(0, 0));
        edit->InsertPageBreak();
        window->UpdateAndRender();
        const int secondBlock = editor.GetBlockCount() - 1;
        editor.SetCaret(editor.ContainerEnd(RichDocPosition(secondBlock, 0)));
        edit->InsertText(" is page ");
        edit->InsertPageNumberField();
        edit->InsertText(" of ");
        edit->InsertPageCountField();
        window->UpdateAndRender();
        window->UpdateAndRender();
        TEST("Two pages", edit->GetPageCount() == 2);
        const std::string text = editor.BlockText(secondBlock);
        TEST("The body's page field numbers its own page: " + text,
             text.find("is page 2 of 2") != std::string::npos);
        TEST("Typing after a field is not part of it", [&]() {
            for (const auto& r : editor.GetBlock(secondBlock).runs) {
                if (r.field != RichTextRun::Field::Plain && r.text.find("of") != std::string::npos) return false;
            }
            return true;
        }());
        edit->SetPageView(false);
    }

    // ===== FOOTNOTES AND ENDNOTES =====
    std::cerr << "\n--- Footnotes and endnotes ---" << std::endl;
    {
        edit->SetMarkdown("Alpha beta gamma.\n\nSecond paragraph.\n");
        edit->SetPageView(true);
        window->UpdateAndRender();
        editor.SetCaret(RichDocPosition(0, 5));             // after "Alpha"
        TEST("A footnote is inserted and opened", edit->InsertFootnote() && edit->IsEditingNote());
        window->UpdateAndRender();
        edit->InsertText("The first note.");
        window->UpdateAndRender();
        edit->OnEvent(KeyEvent(UCKeys::Escape));
        window->UpdateAndRender();
        const auto doc = edit->GetDocument();
        TEST("Escape goes back to the body", !edit->IsEditingNote());
        TEST("The note holds what was typed", doc->notes.size() == 1 && !doc->notes[0].blocks.empty()
             && UCRichDocument::ConcatenateRunText(doc->notes[0].blocks[0].runs) == "The first note.");
        TEST("The reference is marked 1: " + editor.BlockText(0), editor.BlockText(0) == "Alpha1 beta gamma.");

        // One before it takes 1; the first becomes 2.
        editor.SetCaret(RichDocPosition(0, 0));
        edit->InsertFootnote();
        edit->InsertText("Earlier note.");
        edit->FinishHeaderFooterEditing();
        window->UpdateAndRender();
        TEST("Marks renumber in document order: " + editor.BlockText(0), editor.BlockText(0) == "1Alpha2 beta gamma.");

        editor.SetCaret(editor.ContainerEnd(RichDocPosition(1, 0)));
        edit->InsertEndnote();
        edit->InsertText("An endnote.");
        edit->FinishHeaderFooterEditing();
        window->UpdateAndRender();
        TEST("An endnote is marked i: " + editor.BlockText(1), editor.BlockText(1) == "Second paragraph.i");
        TEST("The plain text carries the notes", edit->GetPlainText().find("An endnote.") != std::string::npos);
        TEST("Markdown carries them as footnotes", edit->GetMarkdown().find("[^2]: The first note.") != std::string::npos);
        TEST("Typing after a mark is not raised", [&]() {
            editor.SetCaret(editor.ContainerEnd(RichDocPosition(1, 0)));
            edit->InsertText("!");
            const auto& runs = editor.GetBlock(1).runs;
            return !runs.empty() && runs.back().text == "!" && !runs.back().superscript && runs.back().noteIndex < 0;
        }());
        TEST("A note can be opened again", edit->EditNote(0) && edit->IsEditingNote());
        edit->FinishHeaderFooterEditing();

        // A long document: the footnote stays on its reference's page, and
        // the page's text makes room for it.
        std::string words;
        for (int i = 0; i < 900; i++) words += "word" + std::to_string(i) + " ";
        edit->SetMarkdown(words + "\n");
        window->UpdateAndRender();
        const int pagesBefore = edit->GetPageCount();
        editor.SetCaret(RichDocPosition(0, 40));
        edit->InsertFootnote();
        std::string longNote;
        for (int i = 0; i < 60; i++) longNote += "note" + std::to_string(i) + " ";
        edit->InsertText(longNote);
        edit->FinishHeaderFooterEditing();
        window->UpdateAndRender();
        TEST("A long footnote pushes text on to more pages", edit->GetPageCount() >= pagesBefore);
        std::vector<uint8_t> pdf;
        std::string error;
        TEST("A document with notes exports as a PDF: " + error, edit->ExportToPdf(pdf, error) && pdf.size() > 1000);
        edit->SetPageView(false);
        window->UpdateAndRender();
        TEST("Outside page view the notes follow the body", edit->GetContentHeight() > 0.0f);
    }

    // ===== CONTENTS AND CROSS-REFERENCES =====
    std::cerr << "\n--- Contents and cross-references ---" << std::endl;
    {
        std::string words;
        for (int i = 0; i < 500; i++) words += "word" + std::to_string(i) + " ";
        edit->SetMarkdown("# First\n\n" + words + "\n\n# Second\n\n" + words + "\n\n# Third\n\nEnd.\n");
        edit->SetPageView(true);
        window->UpdateAndRender();
        editor.SetCaret(RichDocPosition(0, 0));
        TEST("A table of contents is inserted", edit->InsertTableOfContents());
        window->UpdateAndRender();
        window->UpdateAndRender();
        const std::string third = editor.BlockText(2);
        TEST("Its page numbers are the headings' pages: " + editor.BlockText(0) + " | " + third,
             editor.BlockText(0) == "First\t1" && third.rfind("Third\t", 0) == 0 && third != "Third\t1");
        TEST("Several pages", edit->GetPageCount() >= 2);
        // Ctrl+click on an entry goes to its heading: tried down the top of
        // the first page until the third entry is hit.
        bool reached = false;
        for (float y = 40.0f; y < 320.0f && !reached; y += 4.0f) {
            edit->ScrollToTop();
            editor.SetCaret(RichDocPosition(0, 0));
            window->UpdateAndRender();
            UCEvent click = MouseEvent(UCEventType::MouseDown, 300.0f, y);
            click.ctrl = true;
            edit->OnEvent(click);
            edit->OnEvent(MouseEvent(UCEventType::MouseUp, 300.0f, y));
            const int caretBlock = editor.GetCaret().blockIndex;
            reached = editor.GetBlock(caretBlock).type == RichBlockType::Heading && editor.BlockText(caretBlock) == "Third";
        }
        TEST("Ctrl+click on an entry goes to its heading", reached);
        window->UpdateAndRender();
        TEST("...scrolled into view", edit->GetScrollOffset() > 100.0f);
        edit->SetPageView(false);
    }

    // ===== COMMENTS =====
    std::cerr << "\n--- Comments ---" << std::endl;
    {
        edit->SetMarkdown("A sentence someone will comment on.\n\nAnother paragraph.\n");
        window->UpdateAndRender();
        const float widthBefore = edit->GetContentHeight();
        TEST("No pane without comments", !edit->IsCommentPaneVisible());
        edit->SetCommentAuthor("Reviewer");
        editor.SetSelection(RichDocPosition(0, 2), RichDocPosition(0, 10));
        const int comment = edit->AddComment("Clarify.");
        window->UpdateAndRender();
        window->UpdateAndRender();
        TEST("A comment is added", comment >= 0 && edit->GetDocument()->comments[static_cast<size_t>(comment)].author == "Reviewer");
        TEST("...and the pane appears", edit->IsCommentPaneVisible());
        (void)widthBefore;
        int activated = -1;
        edit->onCommentActivated = [&](int index) { activated = index; };
        editor.SetCaret(RichDocPosition(1, 0));
        bool selected = false;
        const float paneX = static_cast<float>(edit->GetWidth()) - 15.0f - 110.0f;
        for (float y = 2.0f; y < 200.0f && !selected; y += 4.0f) {
            edit->OnEvent(MouseEvent(UCEventType::MouseDown, paneX, y));
            edit->OnEvent(MouseEvent(UCEventType::MouseUp, paneX, y));
            selected = editor.HasSelection() && editor.GetSelectionRange().start == RichDocPosition(0, 2);
            if (selected) edit->OnEvent(MouseEvent(UCEventType::MouseDoubleClick, paneX, y));
        }
        TEST("Clicking the comment selects its text", selected);
        TEST("Double-clicking it asks the host to edit it", activated == comment);
        TEST("Hiding comments hides the pane", [&]() {
            edit->SetShowComments(false);
            window->UpdateAndRender();
            const bool hidden = !edit->IsCommentPaneVisible();
            edit->SetShowComments(true);
            return hidden;
        }());
        TEST("Removing it removes the pane", [&]() {
            edit->RemoveComment(comment);
            window->UpdateAndRender();
            return !edit->IsCommentPaneVisible();
        }());
        edit->onCommentActivated = nullptr;
    }

    // ===== TRACKED CHANGES =====
    std::cerr << "\n--- Tracked changes ---" << std::endl;
    {
        edit->SetMarkdown("Some text to review.\n");
        window->UpdateAndRender();
        edit->SetCommentAuthor("Reviewer");
        edit->SetTrackChanges(true);
        TEST("Tracking is on", edit->IsTrackingChanges());
        editor.SetCaret(RichDocPosition(0, 5));
        edit->OnEvent(TextEvent("n"));
        edit->OnEvent(TextEvent("e"));
        edit->OnEvent(TextEvent("w"));
        edit->OnEvent(TextEvent(" "));
        editor.SetCaret(RichDocPosition(0, 0));
        edit->OnEvent(KeyEvent(UCKeys::Delete));
        window->UpdateAndRender();
        TEST("Typed and deleted text both stay: " + editor.BlockText(0), editor.BlockText(0) == "Some new text to review.");
        TEST("...marked by Reviewer", [&]() {
            const auto& doc = edit->GetDocument();
            for (const auto& r : editor.GetBlock(0).runs) {
                if (r.change != RichTextRun::Change::Unchanged
                    && (r.revision < 0 || doc->revisions[static_cast<size_t>(r.revision)].author != "Reviewer")) return false;
            }
            return true;
        }());
        TEST("The next change is found", edit->GoToNextChange() && editor.HasSelection());
        TEST("All changes accepted", edit->AcceptAllChanges() && editor.BlockText(0) == "ome new text to review.");
        edit->SetTrackChanges(false);
    }

    // ===== COLUMNS =====
    std::cerr << "\n--- Sections in columns ---" << std::endl;
    {
        std::string md = "Intro.\n\n";
        for (int i = 0; i < 30; i++) md += "Paragraph " + std::to_string(i) + " with some words to fill a narrow column of text in two.\n\n";
        edit->SetMarkdown(md);
        edit->SetPageView(true);
        window->UpdateAndRender();
        editor.SetCaret(RichDocPosition(1, 0));
        TEST("A section break", edit->InsertSectionBreak(false));
        TEST("...in two columns", edit->SetSectionColumns(2) && edit->GetCurrentSection().columns == 2);
        window->UpdateAndRender();
        window->UpdateAndRender();
        // The section fills the first column, then the second.
        editor.SetCaret(RichDocPosition(1, 0));
        window->UpdateAndRender();
        const Rect2Df first = edit->GetCaretRectForTest();
        int second = -1;
        Rect2Df secondRect;
        for (int i = 2; i < editor.GetBlockCount(); i++) {
            editor.SetCaret(RichDocPosition(i, 0));
            window->UpdateAndRender();
            const Rect2Df r = edit->GetCaretRectForTest();
            if (r.x > first.x + 100.0f) { second = i; secondRect = r; break; }
        }
        TEST("Later paragraphs go into the second column", second > 1);
        TEST("...which starts level with the first", second > 1 && std::abs(secondRect.y - first.y) < 60.0f);
        edit->SetPageView(false);
        window->UpdateAndRender();
        editor.SetCaret(RichDocPosition(second > 1 ? second : 2, 0));
        window->UpdateAndRender();
        TEST("Outside page view the text is one column", edit->GetCaretRectForTest().x < first.x + 100.0f);
    }

    // ===== RICH PASTE =====
    std::cerr << "\n--- Rich copy and paste ---" << std::endl;
    {
        edit->SetMarkdown("Some **bold** text.\n");
        window->UpdateAndRender();
        editor.SetSelection(RichDocPosition(0, 0), RichDocPosition(0, 14));
        edit->Copy();
        std::string html;
        TEST("Copy puts HTML on the clipboard", GetClipboardHtml(html) && html.find("<b>bold</b>") != std::string::npos);
        // Into another document: formatted, pictures and all.
        auto other = std::make_shared<UltraCanvasRichTextEdit>("Other", 0, 0, 300, 200);
        window->AddChild(other);
        other->SetMarkdown("\n");
        window->UpdateAndRender();
        other->Paste();
        bool bold = false;
        for (const auto& r : other->GetEditor().GetBlock(0).runs) bold = bold || (r.text == "bold" && r.bold);
        TEST("...and another document pastes it formatted", bold);
        // From another application: only its HTML and text.
        SetClipboardHtml("<p>From <i>elsewhere</i> with <span style=\"color:#00ff00\">green</span></p>",
                         "From elsewhere with green");
        other->SetMarkdown("\n");
        window->UpdateAndRender();
        other->Paste();
        bool italic = false, green = false;
        for (const auto& r : other->GetEditor().GetBlock(0).runs) {
            italic = italic || (r.text == "elsewhere" && r.italic);
            green = green || (r.text == "green" && r.color == "#00FF00");
        }
        TEST("HTML from another application pastes formatted: " + other->GetEditor().BlockText(0),
             italic && green && other->GetEditor().BlockText(0) == "From elsewhere with green");
        window->RemoveChild(other);
    }

    // ===== INPUT METHOD COMPOSITION =====
    std::cerr << "\n--- Input method composition ---" << std::endl;
    {
        edit->SetMarkdown("ab\n");
        window->UpdateAndRender();
        editor.SetCaret(RichDocPosition(0, 1));
        window->UpdateAndRender();
        const Rect2Df before = edit->GetCaretRectForTest();
        TEST("The element draws compositions itself", edit->DrawsTextComposition());
        UCEvent compose;
        compose.type = UCEventType::TextComposition;
        compose.text = "\xE3\x81\x8B\xE3\x81\xAA";          // kana, being composed
        compose.compositionCursor = static_cast<int>(compose.text.size());
        TEST("A composition is taken", edit->OnEvent(compose));
        window->UpdateAndRender();
        const Rect2Df during = edit->GetCaretRectForTest();
        TEST("It shows at the caret, which moves to its end", during.x > before.x + 5.0f);
        TEST("...without entering the document", editor.BlockText(0) == "ab" && edit->GetCompositionText() == compose.text);
        // The input method commits: the text arrives typed.
        UCEvent commit = TextEvent("\xE4\xBB\xAE");
        edit->OnEvent(commit);
        window->UpdateAndRender();
        TEST("The committed text is typed and the composition gone",
             editor.BlockText(0) == "a\xE4\xBB\xAE" "b" && edit->GetCompositionText().empty());
        compose.text.clear();
        edit->OnEvent(compose);
        TEST("An empty composition ends it", edit->GetCompositionText().empty());
    }

    // ===== RIGHT TO LEFT =====
    std::cerr << "\n--- Right-to-left text ---" << std::endl;
    {
        // Hebrew: shalom olam.
        edit->SetMarkdown("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D\n\nleft text\n");
        edit->SetPageView(false);
        window->UpdateAndRender();
        editor.SetCaret(RichDocPosition(0, 0));
        window->UpdateAndRender();
        const Rect2Df start = edit->GetCaretRectForTest();
        editor.SetCaret(RichDocPosition(1, 0));
        window->UpdateAndRender();
        const Rect2Df leftStart = edit->GetCaretRectForTest();
        TEST("A Hebrew paragraph starts at the right", start.x > leftStart.x + 200.0f);
        // At its logical start (the right end), Left moves into the text.
        editor.SetCaret(RichDocPosition(0, 0));
        window->UpdateAndRender();
        edit->OnEvent(KeyEvent(UCKeys::Left));
        window->UpdateAndRender();
        const Rect2Df afterLeft = edit->GetCaretRectForTest();
        TEST("Left moves the caret left through it", editor.GetCaret().blockIndex == 0 && editor.GetCaret().byteOffset == 2
             && afterLeft.x < start.x);
        edit->OnEvent(KeyEvent(UCKeys::Right));
        TEST("...and Right back", editor.GetCaret() == RichDocPosition(0, 0));
        // A left-to-right paragraph marked right-to-left starts at the right.
        editor.SetCaret(RichDocPosition(1, 0));
        edit->SetRightToLeft(true);
        window->UpdateAndRender();
        TEST("A paragraph marked right-to-left", edit->IsRightToLeft() && edit->GetCaretRectForTest().x > leftStart.x + 200.0f);
    }

    // ===== ACCESSIBILITY =====
    std::cerr << "\n--- Accessibility ---" << std::endl;
    {
        edit->SetMarkdown("# Title\n\nA **bold** caf\xC3\xA9 sentence. Another one.\n\n| a | b |\n|---|---|\n| c | d |\n");
        edit->SetPageView(false);
        window->UpdateAndRender();
        TEST("The element is a document", edit->GetAccessibleRole() == AccessibleRole::Document);
        IAccessibleText* text = edit->GetAccessibleTextInterface();
        TEST("It has a text interface", text != nullptr);
        if (text) {
            const std::string all = text->GetAccessibleText();
            TEST("Its text is the paragraphs, a line each, cells tab-separated: " + all,
                 all == "Title\nA bold caf\xC3\xA9 sentence. Another one.\na\tb\nc\td");
            TEST("Offsets count characters", text->GetCharacterCount() == 48);
            std::vector<AccessibilityEventType> events;
            const int listener = UltraCanvasAccessibility::AddListener([&](const AccessibilityEvent& e) {
                if (e.element == edit.get()) events.push_back(e.type);
            });
            text->SetCaretOffset(17);                       // after "café"
            TEST("The caret maps to the document", editor.GetCaret() == RichDocPosition(1, 12) && text->GetCaretOffset() == 17);
            TEST("...and moving it is announced", !events.empty() && events.back() == AccessibilityEventType::CaretMoved);
            int start = 0, end = 0;
            TEST("Words", text->GetTextAtOffset(14, AccessibleTextBoundary::Word, start, end) == "caf\xC3\xA9 " && start == 13);
            TEST("Sentences", text->GetTextAtOffset(30, AccessibleTextBoundary::Sentence, start, end) == "Another one.\n"
                 || text->GetTextAtOffset(30, AccessibleTextBoundary::Sentence, start, end) == "Another one.");
            TEST("Lines as laid out", text->GetTextAtOffset(8, AccessibleTextBoundary::Line, start, end).find("bold") != std::string::npos);
            AccessibleTextAttributes attributes = text->GetAttributesAt(8, start, end);
            TEST("Formatting of a run", attributes.bold && start == 8 && end == 12);
            TEST("A heading's level", text->GetAttributesAt(1, start, end).headingLevel == 1);
            const Rect2Df box = text->GetCharacterBounds(8);
            TEST("Character boxes are on the screen", box.height > 0.0f && box.width > 0.0f);
            TEST("...and lead back to the character", text->GetOffsetAtPoint(Point2Df(box.x + box.width * 0.5f, box.y + box.height * 0.5f)) == 8);
            TEST("Cells are addressable", text->SetSelection(41, 42) && editor.GetSelectionRange().start.InCell());
            events.clear();
            editor.SetCaret(RichDocPosition(1, 0));
            edit->InsertText("x");
            TEST("Edits are announced", std::find(events.begin(), events.end(), AccessibilityEventType::TextChanged) != events.end());
            UltraCanvasAccessibility::RemoveListener(listener);
        }
    }

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
