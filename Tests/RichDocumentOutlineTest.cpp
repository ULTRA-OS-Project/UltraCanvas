// Tests/RichDocumentOutlineTest.cpp
// The model side of the document navigator, without a display: the outline
// read from a UCRichDocument (BuildRichDocumentOutline - headings by level,
// tables, pictures, charts, comments, bookmarks, notes, tracked changes), the
// outline following the edits made to the document, and the heading
// sections the editing core moves, promotes and demotes - each one undo step.
//
// The element that shows it (UltraCanvasDocumentNavigator) is covered under
// Xvfb by DocumentNavigatorElementTest.
#include "UltraCanvasRichDocument.h"
#include "UltraCanvasRichDocumentEditor.h"
#include "UltraCanvasRichDocumentOutline.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

// The headings' words, in order, joined by "|", each prefixed with its level.
std::string HeadingList(const UCRichDocument& document) {
    std::string out;
    for (const RichOutlineHeading& heading : BuildRichDocumentOutline(document).headings) {
        if (!out.empty()) out += "|";
        out += std::to_string(heading.level) + heading.text;
    }
    return out;
}

int HeadingBlock(const UCRichDocument& document, const std::string& text) {
    for (const RichOutlineHeading& heading : BuildRichDocumentOutline(document).headings)
        if (heading.text == text) return heading.blockIndex;
    return -1;
}

// What the body says, block by block, for a check that nothing was lost.
std::string BodyText(const UCRichDocument& document) {
    std::string out;
    for (const RichDocBlock& block : document.blocks) {
        if (!out.empty()) out += "/";
        out += RichOutlineText(block.runs);
    }
    return out;
}

const char* kOutlineMarkdown =
    "# Alpha\n\n"
    "Alpha text.\n\n"
    "## Alpha one\n\n"
    "One text.\n\n"
    "## Alpha two\n\n"
    "### Alpha two a\n\n"
    "Two a text.\n\n"
    "# Beta\n\n"
    "Beta text.\n";

void TestHeadings() {
    std::cerr << "\n--- Headings ---" << std::endl;
    UCRichDocument document = UCRichDocument::FromMarkdown(kOutlineMarkdown);
    const RichDocumentOutline outline = BuildRichDocumentOutline(document);
    TEST("five headings", outline.headings.size() == 5);
    if (outline.headings.size() != 5) return;
    TEST("levels by the document", outline.headings[0].level == 1 && outline.headings[1].level == 2
                                       && outline.headings[2].level == 2 && outline.headings[3].level == 3
                                       && outline.headings[4].level == 1);
    TEST("words of a heading", outline.headings[3].text == "Alpha two a");
    TEST("a tree by level: parents", outline.headings[0].parent == -1 && outline.headings[1].parent == 0
                                         && outline.headings[2].parent == 0 && outline.headings[3].parent == 2
                                         && outline.headings[4].parent == -1);
    TEST("Alpha's section runs to Beta", outline.headings[0].sectionEnd == outline.headings[4].blockIndex);
    TEST("Alpha two's section holds its subheading",
         outline.headings[2].sectionEnd == outline.headings[4].blockIndex);
    TEST("the last section runs to the end", outline.headings[4].sectionEnd == static_cast<int>(document.blocks.size()));
    TEST("the caret's heading: a text block is under the heading above it",
         outline.HeadingAt(outline.headings[3].blockIndex + 1) == 3);
    TEST("the caret's heading: a heading is its own", outline.HeadingAt(outline.headings[1].blockIndex) == 1);
    TEST("no heading above the first", BuildRichDocumentOutline(UCRichDocument::FromMarkdown("Text\n\n# H\n")).HeadingAt(0) == -1);
    TEST("siblings: Alpha two after Alpha one",
         UCRichDocumentEditor::SiblingHeading(document.blocks, outline.headings[1].blockIndex, true)
             == outline.headings[2].blockIndex);
    TEST("siblings: none before the first under its parent",
         UCRichDocumentEditor::SiblingHeading(document.blocks, outline.headings[1].blockIndex, false) == -1);
    TEST("siblings: Beta before... Alpha",
         UCRichDocumentEditor::SiblingHeading(document.blocks, outline.headings[4].blockIndex, false)
             == outline.headings[0].blockIndex);
    TEST("siblings: a paragraph has none", UCRichDocumentEditor::SiblingHeading(document.blocks, 1, true) == -1);
    TEST("a paragraph has no section", UCRichDocumentEditor::HeadingSectionEnd(document.blocks, 1) == -1);
}

void TestOtherLists() {
    std::cerr << "\n--- Tables, pictures, charts, comments, bookmarks, notes, changes ---" << std::endl;
    auto document = std::make_shared<UCRichDocument>(UCRichDocument::FromMarkdown("# Report\n\nIntro text here.\n\n## Data\n\nEnd.\n"));
    UCRichDocumentEditor editor(document);

    // A table after "Data", with a caption, and a word in its first cell.
    const int data = HeadingBlock(*document, "Data");
    editor.SetCaret(RichDocPosition(data + 1, 0));
    const int table = editor.InsertTable(2, 3, true);
    TEST("a table was inserted", table >= 0);
    editor.SetCaret(RichDocPosition(table, 0, 0, 0));
    editor.InsertText("Quarter");
    editor.SetCaret(RichDocPosition(table, 0, 0, 0));
    const std::string captionMark = editor.InsertCaption("Table", "Sales by quarter");
    TEST("a caption was inserted", !captionMark.empty());

    // A picture of its own paragraph, one in the line, and one in a cell.
    // Three different pictures (the same bytes would be one media entry).
    auto png = [](uint8_t which) { return std::vector<uint8_t>{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, which}; };
    editor.SetCaret(RichDocPosition(1, 0));
    const int pictureBlock = editor.InsertImage("logo.png", "image/png", png(1), "The logo");
    TEST("a picture block was inserted", pictureBlock >= 0);
    editor.SetCaret(editor.BlockEnd(RichDocPosition(pictureBlock + 1, 0)));
    TEST("a picture in the line was inserted", editor.InsertInlineImage("icon.png", "image/png", png(2), "") >= 0);
    const int tableNow = BuildRichDocumentOutline(*document).tables.front().blockIndex;
    editor.SetCaret(RichDocPosition(tableNow, 1, 1, 0));
    TEST("a picture in a cell was inserted", editor.InsertInlineImage("cell.png", "image/png", png(3), "Cell picture") >= 0);

    // A chart in the line at the end.
    RichChart chart;
    chart.kind = RichChart::Kind::Column;
    chart.title = "Revenue";
    chart.categories = {"Q1", "Q2"};
    RichChartSeries series;
    series.name = "2026";
    series.values = {3, 5};
    chart.series.push_back(series);
    editor.SetCaret(editor.DocumentEnd());
    TEST("a chart was inserted", editor.InsertChart(chart, 200, 120) >= 0);

    // A comment on "Intro", a bookmark, a footnote, a tracked insertion.
    int intro = -1;
    for (int b = 0; b < editor.GetBlockCount(); ++b)
        if (editor.BlockText(b).rfind("Intro", 0) == 0) intro = b;
    TEST("the intro paragraph is there", intro >= 0);
    editor.SetSelection(RichDocPosition(intro, 0), RichDocPosition(intro, 5));
    const int comment = editor.AddComment("Shorter, please", "Erika", "2026-11-25T10:00:00Z");
    TEST("a comment was added", comment >= 0);
    // A comment no text is under any more is not listed.
    document->comments.push_back(RichComment{"Nobody", "N", "", "orphan", false});
    editor.SetCaret(RichDocPosition(intro, 0));
    TEST("a bookmark was added", editor.AddBookmark("intro"));
    editor.SetCaret(editor.BlockEnd(RichDocPosition(intro, 0)));
    const int note = editor.InsertNote(RichNote::Kind::Footnote);
    TEST("a footnote was added", note >= 0);
    if (note >= 0) {
        RichTextRun words;
        words.text = "Source: the annual report.";
        document->notes[static_cast<size_t>(note)].blocks.front().runs = {words};
    }
    editor.SetRevisionAuthor("Max", "2026-11-25T11:00:00Z");
    editor.SetTrackChanges(true);
    editor.SetCaret(editor.BlockEnd(RichDocPosition(intro, 0)));
    editor.InsertText(" Added");
    editor.SetTrackChanges(false);

    const RichDocumentOutline outline = BuildRichDocumentOutline(*document);
    TEST("one table", outline.tables.size() == 1);
    if (!outline.tables.empty()) {
        TEST("its size", outline.tables[0].rows == 2 && outline.tables[0].columns == 3);
        TEST("its caption", outline.tables[0].caption.find("Sales by quarter") != std::string::npos);
        TEST("its first cell", outline.tables[0].firstCellText == "Quarter");
    }
    TEST("three pictures", outline.pictures.size() == 3);
    if (outline.pictures.size() == 3) {
        TEST("pictures in document order: the paragraph's first", outline.pictures[0].position.blockIndex == pictureBlock
                                                                      && outline.pictures[0].altText == "The logo");
        TEST("... then the one in the line", outline.pictures[1].name == "icon.png"
                                                 && !outline.pictures[1].position.InCell());
        TEST("... then the one in the cell", outline.pictures[2].position.InCell()
                                                 && outline.pictures[2].altText == "Cell picture");
    }
    TEST("one chart, with its title", outline.charts.size() == 1 && outline.charts[0].title == "Revenue"
                                          && outline.charts[0].kind == RichChart::Kind::Column);
    TEST("one comment listed (the orphan is not)", outline.comments.size() == 1);
    if (!outline.comments.empty()) {
        TEST("the comment's author and words", outline.comments[0].author == "Erika"
                                                   && outline.comments[0].text == "Shorter, please");
        TEST("the text it is on", outline.comments[0].commentedText == "Intro"
                                      && outline.comments[0].range.start == RichDocPosition(intro, 0)
                                      && outline.comments[0].range.end == RichDocPosition(intro, 5));
    }
    bool bookmark = false, contentsMark = false;
    for (const RichOutlineBookmark& b : outline.bookmarks) {
        if (b.name == "intro" && b.blockIndex == intro && !b.contentsMark) bookmark = true;
        if (b.contentsMark) contentsMark = true;
    }
    TEST("the bookmark, on its paragraph", bookmark);
    TEST("no contents marks yet", !contentsMark);
    TEST("one footnote", outline.notes.size() == 1);
    if (!outline.notes.empty()) {
        TEST("its mark and words", outline.notes[0].mark == "1" && outline.notes[0].text == "Source: the annual report.");
        TEST("its reference's place", outline.notes[0].position.blockIndex == intro
                                          && editor.NoteAt(outline.notes[0].position) == outline.notes[0].noteIndex);
    }
    TEST("one tracked change", outline.changes.size() == 1);
    if (!outline.changes.empty()) {
        TEST("an insertion by Max", outline.changes[0].change == RichTextRun::Change::Inserted
                                        && outline.changes[0].author == "Max");
        TEST("its words", outline.changes[0].text == "Added");
        TEST("its range covers them", editor.RangeToPlainText(outline.changes[0].range) == " Added");
    }

    // A table of contents gives the headings "_Toc" marks: flagged, so a
    // navigator can leave them out of its bookmarks.
    editor.SetCaret(RichDocPosition(0, 0));
    editor.InsertTableOfContents(3);
    bool flagged = false;
    for (const RichOutlineBookmark& b : BuildRichDocumentOutline(*document).bookmarks) flagged = flagged || b.contentsMark;
    TEST("the contents' marks are flagged", flagged);
}

void TestFollowsEdits() {
    std::cerr << "\n--- The outline follows the edits ---" << std::endl;
    auto document = std::make_shared<UCRichDocument>(UCRichDocument::FromMarkdown(kOutlineMarkdown));
    UCRichDocumentEditor editor(document);
    const RichDocumentOutline before = BuildRichDocumentOutline(*document);
    TEST("an outline equals itself", before == BuildRichDocumentOutline(*document));

    // Typing in a heading changes its entry.
    const int beta = HeadingBlock(*document, "Beta");
    editor.SetCaret(editor.BlockEnd(RichDocPosition(beta, 0)));
    editor.InsertText(" gamma");
    TEST("typing in a heading renames it", HeadingBlock(*document, "Beta gamma") == beta);
    TEST("...and the outline differs", !(before == BuildRichDocumentOutline(*document)));

    // A paragraph made a heading joins the tree under the one above it.
    editor.SetCaret(RichDocPosition(1, 0));     // "Alpha text."
    editor.SetHeadingLevel(2);
    RichDocumentOutline outline = BuildRichDocumentOutline(*document);
    TEST("a new heading", outline.headings.size() == 6 && outline.headings[1].text == "Alpha text."
                              && outline.headings[1].parent == 0);
    editor.Undo();
    TEST("undone, it is text again", BuildRichDocumentOutline(*document).headings.size() == 5);

    // Deleting a heading's paragraph takes it out.
    editor.Undo();   // the typing in Beta
    TEST("undo brought the old outline back", before == BuildRichDocumentOutline(*document));
}

void TestMoveSections() {
    std::cerr << "\n--- Moving sections ---" << std::endl;
    auto document = std::make_shared<UCRichDocument>(UCRichDocument::FromMarkdown(kOutlineMarkdown));
    UCRichDocumentEditor editor(document);
    const std::string original = HeadingList(*document);
    const std::string originalText = BodyText(*document);
    TEST("the outline to start with", original == "1Alpha|2Alpha one|2Alpha two|3Alpha two a|1Beta");

    // Alpha two (with its subheading and text) before Alpha one.
    const size_t blocks = document->blocks.size();
    TEST("move up past the sibling", editor.MoveHeadingSectionUp(HeadingBlock(*document, "Alpha two")));
    TEST("...Alpha two and its subheading now come first under Alpha",
         HeadingList(*document) == "1Alpha|2Alpha two|3Alpha two a|2Alpha one|1Beta");
    TEST("...its text went with it", document->blocks.size() == blocks
                                         && RichOutlineText(document->blocks[static_cast<size_t>(HeadingBlock(*document, "Alpha two a") + 1)].runs)
                                                == "Two a text.");
    TEST("...the caret is on the moved heading", editor.GetCaret() == RichDocPosition(HeadingBlock(*document, "Alpha two"), 0));
    TEST("one undo takes the move back", editor.Undo() && HeadingList(*document) == original
                                             && BodyText(*document) == originalText);
    TEST("redo makes it again", editor.Redo() && HeadingList(*document) == "1Alpha|2Alpha two|3Alpha two a|2Alpha one|1Beta");
    editor.Undo();

    TEST("move down past the sibling", editor.MoveHeadingSectionDown(HeadingBlock(*document, "Alpha")));
    TEST("...Alpha's whole chapter after Beta", HeadingList(*document) == "1Beta|1Alpha|2Alpha one|2Alpha two|3Alpha two a");
    TEST("...nothing lost", document->blocks.size() == blocks);
    editor.Undo();
    TEST("undone", BodyText(*document) == originalText);

    TEST("no sibling above the first", !editor.MoveHeadingSectionUp(HeadingBlock(*document, "Alpha")));
    TEST("no sibling below the last", !editor.MoveHeadingSectionDown(HeadingBlock(*document, "Beta")));
    TEST("no sibling under another parent", !editor.MoveHeadingSectionDown(HeadingBlock(*document, "Alpha two a")));
    TEST("not into its own section", !editor.MoveHeadingSection(HeadingBlock(*document, "Alpha"),
                                                                HeadingBlock(*document, "Alpha two")));
    TEST("not where it is", !editor.MoveHeadingSection(HeadingBlock(*document, "Alpha one"),
                                                       HeadingBlock(*document, "Alpha one")));
    TEST("a paragraph is no section", !editor.MoveHeadingSection(1, 0));
    TEST("refusals change nothing", BodyText(*document) == originalText);

    // A drop: Beta before Alpha one (into Alpha's chapter), and to the end.
    TEST("a drop before another heading", editor.MoveHeadingSection(HeadingBlock(*document, "Beta"),
                                                                    HeadingBlock(*document, "Alpha one")));
    TEST("...Beta keeps its level, before Alpha one",
         HeadingList(*document) == "1Alpha|1Beta|2Alpha one|2Alpha two|3Alpha two a");
    editor.Undo();
    TEST("a drop at the end", editor.MoveHeadingSection(HeadingBlock(*document, "Alpha one"), editor.GetBlockCount()));
    TEST("...Alpha one is last", HeadingList(*document) == "1Alpha|2Alpha two|3Alpha two a|1Beta|2Alpha one");
    editor.Undo();
    TEST("all undone", BodyText(*document) == originalText && HeadingList(*document) == original);
}

void TestPromoteDemote() {
    std::cerr << "\n--- Promoting and demoting sections ---" << std::endl;
    auto document = std::make_shared<UCRichDocument>(UCRichDocument::FromMarkdown(kOutlineMarkdown));
    UCRichDocumentEditor editor(document);
    const std::string original = HeadingList(*document);

    TEST("promote Alpha two", editor.ChangeHeadingSectionLevel(HeadingBlock(*document, "Alpha two"), -1));
    TEST("...it and its subheading go up a level", HeadingList(*document) == "1Alpha|2Alpha one|1Alpha two|2Alpha two a|1Beta");
    TEST("...and it is now a chapter of its own",
         BuildRichDocumentOutline(*document).headings[2].parent == -1);
    TEST("one undo takes it back", editor.Undo() && HeadingList(*document) == original);

    TEST("level 1 cannot be promoted", !editor.ChangeHeadingSectionLevel(HeadingBlock(*document, "Alpha"), -1));
    TEST("demote Alpha: the whole chapter", editor.ChangeHeadingSectionLevel(HeadingBlock(*document, "Alpha"), +1));
    TEST("...every heading in it one down", HeadingList(*document) == "2Alpha|3Alpha one|3Alpha two|4Alpha two a|1Beta");
    TEST("...undone in one step", editor.Undo() && HeadingList(*document) == original && !editor.CanUndo());

    // Level 6 is the last.
    auto deep = std::make_shared<UCRichDocument>(UCRichDocument::FromMarkdown("# A\n\n###### F\n"));
    UCRichDocumentEditor deepEditor(deep);
    TEST("a section holding a level 6 heading cannot be demoted", !deepEditor.ChangeHeadingSectionLevel(0, +1));
    TEST("nothing to undo after a refusal", !deepEditor.CanUndo());

    // A heading in its level's style takes the new level's style.
    auto styled = std::make_shared<UCRichDocument>(UCRichDocument::FromMarkdown("Title words\n\nSub words\n"));
    styled->styles = UCRichDocument::DefaultStyles();
    UCRichDocumentEditor styledEditor(styled);
    styledEditor.SetCaret(RichDocPosition(0, 0));
    styledEditor.ApplyParagraphStyle("Heading2");
    styledEditor.SetCaret(RichDocPosition(1, 0));
    styledEditor.ApplyParagraphStyle("Heading3");
    TEST("styled headings", styled->blocks[0].styleId == "Heading2" && styled->blocks[0].headingLevel == 2
                                && styled->blocks[1].headingLevel == 3);
    TEST("promote the styled section", styledEditor.ChangeHeadingSectionLevel(0, -1));
    TEST("...each takes its new level's style", styled->blocks[0].styleId == "Heading1"
                                                    && styled->blocks[0].headingLevel == 1
                                                    && styled->blocks[1].styleId == "Heading2"
                                                    && styled->blocks[1].headingLevel == 2);
    styledEditor.Undo();
    TEST("...and gives it back on undo", styled->blocks[0].styleId == "Heading2" && styled->blocks[1].styleId == "Heading3");
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Rich Document Outline Suite"          << std::endl;
    std::cerr << "========================================" << std::endl;
    TestHeadings();
    TestOtherLists();
    TestFollowsEdits();
    TestMoveSections();
    TestPromoteDemote();
    std::cerr << "\n" << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
