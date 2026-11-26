// Tests/DocumentNavigatorElementTest.cpp
// UltraCanvasDocumentNavigator beside an UltraCanvasRichTextEdit in a real
// window: its rows read from the document (headings as a tree, tables,
// pictures, comments, notes), a click on a row going there, the current
// heading following the caret, the search box filtering the rows and listing
// the finds, the buttons promoting and moving a heading's section, a row
// dragged onto another moving its section, and the debounced refresh.
//
// The model side (the outline, the section moves and their undo) is covered
// without a display by RichDocumentOutlineTest.
//
// Runs headless under Xvfb. Skips - rather than fails - when there is no
// display, unless the run asked for one (ULTRACANVAS_TEST_DISPLAY_REQUIRED).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasDocumentNavigator.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTreeView.h"
#include "DisplayTestSupport.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
using Kind = UltraCanvasDocumentNavigator::EntryKind;

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
        if (UltraCanvas::DisplayTest::DisplayRequired()) {                    \
            std::cerr << "FAIL: a display was required, and " << reason       \
                      << std::endl;                                           \
            return 1;                                                         \
        }                                                                     \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

// A 1x1 PNG, for a picture that has to decode.
const std::vector<uint8_t> kDotPng = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
    0xDE,0x00,0x00,0x00,0x0C,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0xF8,0xCF,0xC0,0x00,
    0x00,0x03,0x01,0x01,0x00,0xC9,0xFE,0x92,0xEF,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,
    0x44,0xAE,0x42,0x60,0x82};

UCEvent Mouse(UCEventType type, int x, int y) {
    UCEvent event;
    event.type = type;
    event.button = UCMouseButton::Left;
    event.pointer = Point2Di(x, y);
    event.pointerWindow = Point2Di(x, y);
    event.pointerGlobal = event.pointer;
    return event;
}

UCEvent TextEvent(const std::string& text) {
    UCEvent event;
    event.type = UCEventType::KeyDown;
    event.virtualKey = UCKeys::Unknown;
    event.text = text;
    return event;
}

// The tree-local y of the middle of a row, -1 when the row is not on show.
int RowY(UltraCanvasTreeView& tree, const std::string& id) {
    for (int y = 0; y < static_cast<int>(tree.GetHeight()); y += 2) {
        TreeNode* node = tree.GetNodeAtY(y);
        if (!node || node->data.nodeId != id) continue;
        int bottom = y;
        while (bottom + 1 < static_cast<int>(tree.GetHeight()) && tree.GetNodeAtY(bottom + 1) == node) ++bottom;
        return (y + bottom) / 2;
    }
    return -1;
}

bool ClickRow(UltraCanvasTreeView& tree, const std::string& id) {
    const int y = RowY(tree, id);
    if (y < 0) return false;
    tree.OnEvent(Mouse(UCEventType::MouseDown, 80, y));
    tree.OnEvent(Mouse(UCEventType::MouseUp, 80, y));
    return true;
}

// The headings as the document has them, "level+words" joined by "|".
std::string Headings(const UltraCanvasRichTextEdit& edit) {
    std::string out;
    for (const RichDocBlock& block : edit.GetDocument()->blocks) {
        if (block.type != RichBlockType::Heading) continue;
        if (!out.empty()) out += "|";
        out += std::to_string(block.headingLevel) + RichOutlineText(block.runs);
    }
    return out;
}

int HeadingIndex(const UltraCanvasDocumentNavigator& navigator, const std::string& text) {
    const auto& headings = navigator.GetOutline().headings;
    for (size_t i = 0; i < headings.size(); ++i)
        if (headings[i].text == text) return static_cast<int>(i);
    return -1;
}

std::string ParentId(UltraCanvasTreeView& tree, const std::string& id) {
    TreeNode* node = tree.FindNode(id);
    return node && node->parent ? node->parent->data.nodeId : "";
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Document Navigator Element Suite"     << std::endl;
    std::cerr << "========================================" << std::endl;

    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    UltraCanvasApplication app;
    if (!app.Initialize("DocumentNavigatorElementTest")) SKIP_ALL("application would not initialise");

    WindowConfig cfg;
    cfg.title = "DocumentNavigatorElementTest";
    cfg.width = 1000;
    cfg.height = 700;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    auto navigator = std::make_shared<UltraCanvasDocumentNavigator>("Navigator", 0, 0, 280, 680);
    auto edit = std::make_shared<UltraCanvasRichTextEdit>("RichEdit", 290, 0, 700, 680);
    window->AddChild(navigator);
    window->AddChild(edit);
    edit->SetCommentAuthor("Erika");
    edit->SetMarkdown("# Alpha\n\n"
                      "Alpha text about the budget.\n\n"
                      "## Alpha one\n\n"
                      "One text.\n\n"
                      "| Quarter | Sales |\n|---|---|\n| Q1 | 10 |\n\n"
                      "## Alpha two\n\n"
                      "### Alpha two a\n\n"
                      "Two a text and the budget again.\n\n"
                      "# Beta\n\n"
                      "Beta text.\n");
    UCRichDocumentEditor& editor = edit->GetEditor();
    // A picture after Beta's text, a comment on "Beta", a footnote.
    editor.SetCaret(editor.DocumentEnd());
    edit->InsertImageFromMemory("dot.png", "image/png", kDotPng, "A dot");
    int beta = -1, oneText = -1;
    for (int b = 0; b < editor.GetBlockCount(); ++b) {
        const std::string text = editor.BlockText(b);
        if (text == "Beta text.") beta = b;
        if (text == "One text.") oneText = b;
    }
    editor.SetSelection(RichDocPosition(beta, 0), RichDocPosition(beta, 4));
    edit->AddComment("Say more here");
    editor.SetCaret(editor.BlockEnd(RichDocPosition(oneText, 0)));
    edit->InsertNote(RichNote::Kind::Footnote, "From the ledger.");
    editor.SetCaret(RichDocPosition(0, 0));

    navigator->SetRefreshDelay(0);
    navigator->SetEdit(edit);
    DisplayTest::Frame(window, {navigator, edit});
    UltraCanvasTreeView& tree = *navigator->GetTree();

    // ===== THE ROWS =====
    std::cerr << "\n--- The rows ---" << std::endl;
    const std::string headingsRow = UltraCanvasDocumentNavigator::EntryId(Kind::Category, static_cast<int>(Kind::Heading));
    TEST("five headings in the outline", navigator->GetOutline().headings.size() == 5);
    TEST("a Headings row", tree.FindNode(headingsRow) != nullptr);
    TEST("a top heading hangs from the Headings row",
         ParentId(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, 0)) == headingsRow);
    TEST("a tree by level: Alpha two a under Alpha two",
         ParentId(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, 3))
             == UltraCanvasDocumentNavigator::EntryId(Kind::Heading, 2));
    TEST("a table row", navigator->IsEntryShown(Kind::Table, 0));
    TEST("a picture row, by its alt text", navigator->IsEntryShown(Kind::Picture, 0)
                                               && tree.FindNode(UltraCanvasDocumentNavigator::EntryId(Kind::Picture, 0))->data.text == "A dot");
    TEST("a comment row, with its author", navigator->IsEntryShown(Kind::Comment, 0)
                                               && tree.FindNode(UltraCanvasDocumentNavigator::EntryId(Kind::Comment, 0))->data.text.rfind("Erika: Say more", 0) == 0);
    TEST("a footnote row", navigator->IsEntryShown(Kind::Note, 0)
                               && tree.FindNode(UltraCanvasDocumentNavigator::EntryId(Kind::Note, 0))->data.text.find("ledger") != std::string::npos);
    TEST("the caret's heading is the selected row", navigator->GetCurrentHeading() == 0
                                                        && tree.GetFirstSelectedNode()
                                                        && tree.GetFirstSelectedNode()->data.nodeId == UltraCanvasDocumentNavigator::EntryId(Kind::Heading, 0));

    // ===== A CLICK GOES THERE =====
    std::cerr << "\n--- A click goes there ---" << std::endl;
    const int alphaTwoA = HeadingIndex(*navigator, "Alpha two a");
    TEST("a click on a heading row", ClickRow(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, alphaTwoA)));
    TEST("...puts the caret on that heading",
         editor.GetCaret() == RichDocPosition(navigator->GetOutline().headings[static_cast<size_t>(alphaTwoA)].blockIndex, 0));
    TEST("...which is now the current heading", navigator->GetCurrentHeading() == alphaTwoA);
    DisplayTest::Frame(window, {navigator, edit});
    TEST("a click on the table row", ClickRow(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Table, 0)));
    TEST("...puts the caret in its first cell", editor.GetCaret().InCell()
                                                    && editor.GetCaret().blockIndex == navigator->GetOutline().tables[0].blockIndex);
    TEST("...and keeps the table row selected", tree.GetFirstSelectedNode()
                                                    && tree.GetFirstSelectedNode()->data.nodeId == UltraCanvasDocumentNavigator::EntryId(Kind::Table, 0));
    TEST("...under Alpha one", navigator->GetCurrentHeading() == HeadingIndex(*navigator, "Alpha one"));
    TEST("the picture row selects the picture", navigator->GoToEntry(Kind::Picture, 0) && edit->HasSelectedImage());
    TEST("the comment row selects the commented word", navigator->GoToEntry(Kind::Comment, 0)
                                                           && edit->GetSelectedText() == "Beta");
    int activated = -1;
    navigator->onCommentActivated = [&activated](int index) { activated = index; };
    TEST("a double-click on a comment hands it to the host", navigator->ActivateEntry(Kind::Comment, 0) && activated == 0);
    TEST("activating the footnote opens it for editing", navigator->ActivateEntry(Kind::Note, 0) && edit->IsEditingNote());
    edit->FinishHeaderFooterEditing();

    // ===== THE CURRENT HEADING FOLLOWS THE CARET =====
    std::cerr << "\n--- The caret ---" << std::endl;
    editor.SetCaret(RichDocPosition(beta, 2));
    TEST("the caret in Beta's text selects Beta's row", navigator->GetCurrentHeading() == HeadingIndex(*navigator, "Beta")
                                                            && tree.GetFirstSelectedNode()
                                                            && tree.GetFirstSelectedNode()->data.nodeId
                                                                   == UltraCanvasDocumentNavigator::EntryId(Kind::Heading, HeadingIndex(*navigator, "Beta")));
    editor.SetCaret(RichDocPosition(oneText, 0));
    TEST("...and Alpha one's text Alpha one's", navigator->GetCurrentHeading() == HeadingIndex(*navigator, "Alpha one"));

    // ===== SEARCH =====
    std::cerr << "\n--- Search ---" << std::endl;
    UltraCanvasTextInput& search = *navigator->GetSearchBox();
    search.SetFocus(true);
    for (const char* letter : {"t", "w", "o"}) search.OnEvent(TextEvent(letter));
    TEST("typing in the search box filters", navigator->GetFilterText() == "two");
    TEST("...the matching headings stay", navigator->IsEntryShown(Kind::Heading, HeadingIndex(*navigator, "Alpha two"))
                                              && navigator->IsEntryShown(Kind::Heading, alphaTwoA));
    TEST("...with the heading above them", navigator->IsEntryShown(Kind::Heading, 0));
    TEST("...the others go", !navigator->IsEntryShown(Kind::Heading, HeadingIndex(*navigator, "Beta"))
                                 && !navigator->IsEntryShown(Kind::Heading, HeadingIndex(*navigator, "Alpha one")));
    TEST("...and so do the lists without a match", !navigator->IsEntryShown(Kind::Table, 0)
                                                       && !navigator->IsEntryShown(Kind::Picture, 0));
    navigator->SetFilterText("budget");
    TEST("the finds are listed", navigator->GetFoundRanges().size() == 2 && navigator->IsEntryShown(Kind::Found, 1));
    TEST("a find goes there and is selected", navigator->GoToEntry(Kind::Found, 1)
                                                  && edit->GetSelectedText() == "budget"
                                                  && editor.GetCaret().blockIndex
                                                         > navigator->GetOutline().headings[static_cast<size_t>(alphaTwoA)].blockIndex);
    DisplayTest::Frame(window, {navigator, edit});
    navigator->SetFilterText("");
    TEST("an empty search brings every row back", navigator->IsEntryShown(Kind::Table, 0)
                                                      && navigator->IsEntryShown(Kind::Heading, HeadingIndex(*navigator, "Beta"))
                                                      && navigator->GetFoundRanges().empty());

    // ===== PROMOTE, DEMOTE, MOVE =====
    std::cerr << "\n--- The buttons ---" << std::endl;
    const std::string original = Headings(*edit);
    TEST("the headings to start with", original == "1Alpha|2Alpha one|2Alpha two|3Alpha two a|1Beta");
    ClickRow(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, HeadingIndex(*navigator, "Alpha two")));
    TEST("promote the selected heading", navigator->PromoteHeading());
    TEST("...it and its subheading go up", Headings(*edit) == "1Alpha|2Alpha one|1Alpha two|2Alpha two a|1Beta");
    TEST("...and the tree follows at once", ParentId(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, HeadingIndex(*navigator, "Alpha two")))
                                                == headingsRow);
    TEST("undo takes it back", edit->Undo() && Headings(*edit) == original);
    TEST("...and the tree follows the undo",
         ParentId(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, HeadingIndex(*navigator, "Alpha two")))
             == UltraCanvasDocumentNavigator::EntryId(Kind::Heading, 0));
    ClickRow(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, HeadingIndex(*navigator, "Alpha two")));
    TEST("move the section up", navigator->MoveHeadingUp());
    TEST("...past Alpha one", Headings(*edit) == "1Alpha|2Alpha two|3Alpha two a|2Alpha one|1Beta");
    TEST("...the moved heading stays the current one",
         navigator->GetCurrentHeading() == HeadingIndex(*navigator, "Alpha two"));
    edit->Undo();
    ClickRow(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, 0));
    TEST("promote is refused at level 1", !navigator->PromoteHeading() && Headings(*edit) == original);
    TEST("move down the whole chapter", navigator->MoveHeadingDown()
                                            && Headings(*edit) == "1Beta|1Alpha|2Alpha one|2Alpha two|3Alpha two a");
    edit->Undo();
    TEST("undone", Headings(*edit) == original);

    // ===== DRAGGING A ROW =====
    std::cerr << "\n--- Dragging a heading ---" << std::endl;
    DisplayTest::Frame(window, {navigator, edit});
    const int fromY = RowY(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, HeadingIndex(*navigator, "Beta")));
    const int toY = RowY(tree, UltraCanvasDocumentNavigator::EntryId(Kind::Heading, HeadingIndex(*navigator, "Alpha one")));
    TEST("both rows on show", fromY >= 0 && toY >= 0);
    tree.OnEvent(Mouse(UCEventType::MouseDown, 80, fromY));
    tree.OnEvent(Mouse(UCEventType::MouseMove, 80, fromY - 10));
    tree.OnEvent(Mouse(UCEventType::MouseMove, 80, toY));
    TEST("a drag is under way", tree.IsDraggingNode());
    tree.OnEvent(Mouse(UCEventType::MouseUp, 80, toY));
    TEST("dropping Beta on Alpha one moves Beta's section before it",
         Headings(*edit) == "1Alpha|1Beta|2Alpha one|2Alpha two|3Alpha two a");
    TEST("one undo takes the drop back", edit->Undo() && Headings(*edit) == original);
    TEST("a heading cannot be dropped into its own section",
         !navigator->MoveHeadingBefore(0, HeadingIndex(*navigator, "Alpha two")) && Headings(*edit) == original);
    TEST("dropped on the Headings row, a section goes to the end",
         navigator->MoveHeadingBefore(HeadingIndex(*navigator, "Alpha one"), -1)
             && Headings(*edit) == "1Alpha|2Alpha two|3Alpha two a|1Beta|2Alpha one");
    edit->Undo();

    // ===== DEBOUNCED =====
    std::cerr << "\n--- Debounced refresh ---" << std::endl;
    navigator->SetRefreshDelay(400);
    editor.SetCaret(editor.BlockEnd(RichDocPosition(0, 0)));
    edit->InsertText(" first");
    TEST("an edit waits for the pause", navigator->IsRefreshPending()
                                            && navigator->GetOutline().headings[0].text == "Alpha");
    editor.SetCaret(editor.BlockEnd(RichDocPosition(0, 0)));
    edit->InsertText(" part");
    TEST("...and another one puts it off again", navigator->IsRefreshPending());
    navigator->Refresh();
    TEST("once read, the renamed heading is listed", !navigator->IsRefreshPending()
                                                         && navigator->GetOutline().headings[0].text == "Alpha first part"
                                                         && tree.FindNode(UltraCanvasDocumentNavigator::EntryId(Kind::Heading, 0))->data.text
                                                                == "Alpha first part");
    DisplayTest::Frame(window, {navigator, edit});

    // ===== A NAVIGATOR THAT GOES FIRST =====
    std::cerr << "\n--- Lifetime ---" << std::endl;
    {
        auto second = std::make_shared<UltraCanvasDocumentNavigator>("Second", 0, 0, 200, 300);
        second->SetRefreshDelay(0);
        second->SetEdit(edit);
        TEST("a second navigator lists the same", second->GetOutline().headings.size() == 5);
    }
    edit->InsertText("!");
    TEST("an edit after a navigator went is safe", Headings(*edit).find("part!") != std::string::npos);

    std::cerr << "\n" << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
