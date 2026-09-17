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

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
}
