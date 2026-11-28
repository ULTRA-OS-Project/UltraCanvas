// Tests/RichTextDialogsTest.cpp
// What the document dialogs of UltraCanvasRichTextEdit apply
// (UltraCanvasRichTextEditDialogs.h): each dialog's Read...DialogValues, and
// what its OK does - Apply...DialogValues - to the document, and that one
// undo takes it back where the editor keeps an undo step. Page setup,
// paragraph, styles, table properties, picture, contents, caption,
// cross-reference, footnote, comments, and the review calls behind the
// Review tab (previous and next change and comment, accept, reject).
//
// The element is made but never shown, so no display is needed.
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasRichTextEditDialogs.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int checks = 0;
int failures = 0;

void Check(bool condition, const std::string& what) {
    ++checks;
    std::cout << (condition ? "  ok    " : "  FAIL  ") << what << "\n";
    if (!condition) ++failures;
}

bool Near(float a, float b, float tolerance = 0.5f) { return std::fabs(a - b) <= tolerance; }

RichDocBlock Paragraph(const std::string& text, RichBlockType type = RichBlockType::Paragraph, int level = 0) {
    RichDocBlock block;
    block.type = type;
    block.headingLevel = level;
    RichTextRun run;
    run.text = text;
    block.runs.push_back(run);
    return block;
}

std::shared_ptr<UltraCanvasRichTextEdit> MakeEdit(std::vector<RichDocBlock> blocks) {
    auto edit = CreateRichTextEdit("DialogsTest", 0, 0, 700, 500);
    auto document = std::make_shared<UCRichDocument>();
    document->blocks = std::move(blocks);
    edit->SetDocument(document);
    return edit;
}

// A 1x1 PNG, so a picture has a size of its own.
const std::vector<uint8_t> kPng = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
    0xDE,0x00,0x00,0x00,0x0C,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0xF8,0xCF,0xC0,0x00,
    0x00,0x03,0x01,0x01,0x00,0xC9,0xFE,0x92,0xEF,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,
    0x44,0xAE,0x42,0x60,0x82};

void TestPageSetup() {
    std::cout << "\nPage setup\n";
    auto edit = MakeEdit({Paragraph("One"), Paragraph("Two")});
    RichPageSetupDialogValues values = ReadPageSetupDialogValues(*edit);
    Check(values.paper == 1 && !values.landscape, "a document without a page opens on A4, upright");
    Check(Near(values.marginLeftPt, 56.69f), "with 2 cm margins");

    Check(ApplyPageSetupDialogValues(*edit, values) && edit->GetDocument()->page.HasPage(),
          "OK on what was shown states the page the document was laid out on");
    edit->GetEditor().ClearUndoHistory();
    const RichPageSetup before = edit->GetDocument()->page;

    values.paper = 4;                 // Letter
    values.landscape = true;
    values.marginTopPt = 36.0f;
    values.marginLeftPt = 72.0f;
    values.headerTopPt = 18.0f;
    values.footerBottomPt = 20.0f;
    Check(ApplyPageSetupDialogValues(*edit, values), "OK applies a new page");
    const RichPageSetup& page = edit->GetDocument()->page;
    Check(Near(page.widthPt, 792.0f) && Near(page.heightPt, 612.0f), "Letter, turned on its side");
    Check(Near(page.marginTopPt, 36.0f) && Near(page.marginLeftPt, 72.0f), "with the margins typed");
    Check(Near(page.headerTopPt, 18.0f) && Near(page.footerBottomPt, 20.0f), "and the header and footer distance");
    Check(Near(edit->GetEffectivePageSetup().widthPt, 792.0f), "the element lays out on it");
    RichPageSetupDialogValues reread = ReadPageSetupDialogValues(*edit);
    Check(reread.paper == 4 && reread.landscape, "and the dialog reads it back as Letter, landscape");

    Check(!ApplyPageSetupDialogValues(*edit, reread), "OK with nothing changed changes nothing");
    Check(edit->Undo(), "the page setup is an undo step");
    Check(edit->GetDocument()->page == before, "undo brings the page back");
    Check(edit->Redo() && Near(edit->GetDocument()->page.widthPt, 792.0f), "redo applies it again");

    RichPageSetupDialogValues custom = reread;
    custom.paper = -1;
    custom.widthPt = 400.0f;
    custom.heightPt = 500.0f;
    Check(ApplyPageSetupDialogValues(*edit, custom) && Near(edit->GetDocument()->page.widthPt, 400.0f),
          "a custom paper is the size typed");
    custom.marginLeftPt = custom.marginRightPt = 200.0f;
    Check(!ApplyPageSetupDialogValues(*edit, custom), "margins that leave no text column are refused");
}

void TestParagraph() {
    std::cout << "\nParagraph\n";
    auto edit = MakeEdit({Paragraph("First"), Paragraph("Second"), Paragraph("Third")});
    edit->GetEditor().SetSelection(RichDocPosition(0, 1), RichDocPosition(1, 2));
    RichParagraphFormat format = ReadParagraphDialogValues(*edit);
    Check(format.spaceBeforePt < 0.0f && format.lineSpacing == 0.0f, "an unformatted paragraph reads as not stated");
    format.align = RichTextAlign::Center;
    format.leftIndentPt = 36.0f;
    format.rightIndentPt = 18.0f;
    format.firstLineIndentPt = -18.0f;
    format.spaceBeforePt = 6.0f;
    format.spaceAfterPt = 12.0f;
    format.lineSpacing = 1.5f;
    format.tabStops = {{144.0f, RichTabKind::Right}, {72.0f, RichTabKind::Left}};
    Check(ApplyParagraphDialogValues(*edit, format), "OK applies the layout");
    const auto& blocks = edit->GetDocument()->blocks;
    for (int b = 0; b < 2; ++b) {
        const RichDocBlock& block = blocks[static_cast<size_t>(b)];
        Check(block.align == RichTextAlign::Center && block.leftIndentPt == 36.0f && block.rightIndentPt == 18.0f
                  && block.firstLineIndentPt == -18.0f,
              "paragraph " + std::to_string(b + 1) + " of the selection is indented and centred");
        Check(block.spaceBeforePt == 6.0f && block.spaceAfterPt == 12.0f && block.lineSpacing == 1.5f,
              "and spaced");
        Check(block.tabStops.size() == 2 && block.tabStops[0].positionPt == 72.0f
                  && block.tabStops[1].kind == RichTabKind::Right,
              "with its tab stops in order");
    }
    Check(blocks[2].align == RichTextAlign::Default && blocks[2].leftIndentPt == 0.0f,
          "a paragraph outside the selection is left alone");
    Check(!ApplyParagraphDialogValues(*edit, ReadParagraphDialogValues(*edit)), "OK with nothing changed changes nothing");

    RichParagraphFormat exact = ReadParagraphDialogValues(*edit);
    exact.lineSpacing = 2.0f;
    exact.lineHeightPt = 14.0f;
    exact.lineHeightAtLeast = true;
    Check(ApplyParagraphDialogValues(*edit, exact), "a line height");
    Check(blocks[0].lineHeightPt == 14.0f && blocks[0].lineHeightAtLeast && blocks[0].lineSpacing == 0.0f,
          "is at least that, and wins over a multiple");

    Check(edit->Undo(), "undo the line height");
    Check(blocks[0].lineHeightPt == 0.0f && blocks[0].lineSpacing == 1.5f, "brings the multiple back");
    Check(edit->Undo(), "undo the layout");
    Check(blocks[0].align == RichTextAlign::Default && blocks[1].tabStops.empty() && blocks[0].spaceAfterPt < 0.0f,
          "takes all of it back in one step");

    // In a table cell, the alignment and the line spacing are the cell's.
    auto table = MakeEdit({Paragraph("Above")});
    table->GetEditor().SetCaret(RichDocPosition(0, 5));
    table->InsertTable(2, 2);
    Check(table->IsCaretInTable(), "the caret is in the new table");
    RichParagraphFormat cell = ReadParagraphDialogValues(*table);
    cell.align = RichTextAlign::Right;
    cell.lineSpacing = 1.15f;
    cell.leftIndentPt = 99.0f;
    Check(ApplyParagraphDialogValues(*table, cell), "OK in a cell");
    const RichDocPosition caret = table->GetEditor().GetCaret();
    const RichTableCell& tableCell = table->GetDocument()->blocks[static_cast<size_t>(caret.blockIndex)]
                                         .tableRows[static_cast<size_t>(caret.cellRow)]
                                         .cells[static_cast<size_t>(caret.cellColumn)];
    Check(tableCell.align == RichTextAlign::Right && Near(tableCell.lineSpacing, 1.15f, 0.001f),
          "aligns the cell and spaces its lines");
    Check(table->GetDocument()->blocks[static_cast<size_t>(caret.blockIndex)].leftIndentPt == 0.0f,
          "and indents nothing");
}

void TestStyles() {
    std::cout << "\nNamed styles\n";
    auto edit = MakeEdit({Paragraph("Title text"), Paragraph("Body")});
    edit->GetEditor().SetCaret(RichDocPosition(0, 2));
    RichStylesDialogValues values = ReadStylesDialogValues(*edit);
    Check(values.styleId == "Normal", "the caret's paragraph is in Normal");

    values.action = RichStylesDialogValues::Action::Apply;
    values.styleId = "Heading1";
    Check(ApplyStylesDialogValues(*edit, values), "Apply gives the paragraph a style");
    Check(edit->GetDocument()->blocks[0].styleId == "Heading1" && edit->GetCurrentHeadingLevel() == 1,
          "it is a heading now");
    Check(edit->Undo() && edit->GetDocument()->blocks[0].styleId.empty(), "undo takes the style off");

    RichStylesDialogValues change = StylesDialogValuesFor(*edit, "Normal");
    change.action = RichStylesDialogValues::Action::Change;
    change.name = "Body";
    change.character = RichStyleCharacter();
    change.character.fontSizePt = 13.0f;
    change.character.italic = true;
    change.paragraph = RichStyleParagraph();
    change.paragraph.spaceAfterPt = 9.0f;
    Check(ApplyStylesDialogValues(*edit, change), "Change changes the style");
    bool found = false;
    for (const RichStyle& style : edit->GetStyles()) {
        if (style.id != "Normal") continue;
        found = true;
        Check(style.name == "Body" && style.character.fontSizePt && *style.character.fontSizePt == 13.0f
                  && style.character.italic && *style.character.italic,
              "its name and its text");
        Check(style.paragraph.spaceAfterPt && *style.paragraph.spaceAfterPt == 9.0f, "and its spacing");
    }
    Check(found, "the changed style is still there");
    Check(edit->GetDocument()->blocks[1].spaceAfterPt == 9.0f, "and its paragraphs follow it");
    Check(!edit->GetDocument()->blocks[1].runs.empty() && edit->GetDocument()->blocks[1].runs[0].italic
              && edit->GetDocument()->blocks[1].runs[0].fontSizePt == 13.0f,
          "and their text");
    Check(edit->Undo(), "the change is an undo step");
    for (const RichStyle& style : edit->GetStyles()) {
        if (style.id == "Normal") Check(style.name != "Body", "undo gives the style its name back");
    }

    RichStylesDialogValues fresh;
    fresh.action = RichStylesDialogValues::Action::NewFromSelection;
    fresh.name = "Pull quote";
    Check(!ApplyStylesDialogValues(*edit, RichStylesDialogValues{RichStylesDialogValues::Action::NewFromSelection, "", "", {}, {}}),
          "a new style needs a name");
    Check(ApplyStylesDialogValues(*edit, fresh), "New from the selection");
    bool made = false;
    for (const RichStyle& style : edit->GetStyles()) made = made || style.name == "Pull quote";
    Check(made && edit->GetDocument()->blocks[0].styleId == "Pullquote", "makes the style and gives it the paragraph");
}

void TestTable() {
    std::cout << "\nTable properties\n";
    auto edit = MakeEdit({Paragraph("Above")});
    RichTableProperties none = ReadTableDialogValues(*edit);
    Check(!ApplyTableDialogValues(*edit, none), "outside a table there is nothing to apply");
    edit->GetEditor().SetCaret(RichDocPosition(0, 5));
    edit->InsertTable(3, 2);
    RichTableProperties values = ReadTableDialogValues(*edit);
    Check(values.borderWidthPt < 0.0f && values.widthPercent == 0.0f, "a new table has the editor's grid and the whole column");
    edit->GetEditor().ClearUndoHistory();

    values.widthPercent = 60.0f;
    values.align = RichTextAlign::Center;
    values.headerRow = true;
    values.autoFit = true;
    values.borderWidthPt = 1.5f;
    values.borderColor = "#C00000";
    values.cellPaddingPt = 4.0f;
    values.verticalAlign = RichVerticalAlign::Middle;
    Check(ApplyTableDialogValues(*edit, values), "OK applies them");
    const int at = edit->GetEditor().GetCaret().blockIndex;
    const RichDocBlock& table = edit->GetDocument()->blocks[static_cast<size_t>(at)];
    Check(table.tableWidthPercent == 60.0f && table.tableAlign == RichTextAlign::Center && table.tableAutoFit,
          "a narrower table, centred, fitting its contents");
    Check(table.tableRows[0].header && !table.tableRows[1].header, "the first row is the header");
    Check(table.tableBordersFromDocument, "the cells' own lines are drawn");
    bool everyCell = true;
    for (const RichTableRow& row : table.tableRows) {
        for (const RichTableCell& cell : row.cells) {
            everyCell = everyCell && cell.borderLeft.widthPt == 1.5f && cell.borderBottom.color == "#C00000"
                     && cell.paddingTopPt == 4.0f && cell.verticalAlign == RichVerticalAlign::Middle;
        }
    }
    Check(everyCell, "every cell has the lines, padding and alignment");
    Check(ReadTableDialogValues(*edit) == values, "and the dialog reads them back");
    Check(!ApplyTableDialogValues(*edit, values), "OK with nothing changed changes nothing");

    Check(edit->Undo(), "the change is one undo step");
    const RichDocBlock& undone = edit->GetDocument()->blocks[static_cast<size_t>(at)];
    Check(undone.tableWidthPercent == 0.0f && !undone.tableBordersFromDocument && !undone.tableRows[0].header,
          "undo gives the table back as it was");
}

void TestPicture() {
    std::cout << "\nPicture\n";
    auto edit = MakeEdit({Paragraph("Before")});
    edit->GetEditor().SetCaret(RichDocPosition(0, 6));
    edit->InsertImageFromMemory("dot.png", "image/png", kPng, "A dot");
    int imageBlock = -1;
    for (int b = 0; b < edit->GetEditor().GetBlockCount(); ++b) {
        if (edit->GetEditor().GetBlock(b).type == RichBlockType::Image) imageBlock = b;
    }
    Check(imageBlock >= 0, "a picture of its own paragraph");
    Check(!ApplyPictureDialogValues(*edit, RichPictureDialogValues()), "with no picture selected there is nothing to apply");
    Check(edit->SelectImage(RichDocPosition(imageBlock, 0)), "it is selected");
    edit->GetEditor().ClearUndoHistory();
    RichPictureDialogValues values = ReadPictureDialogValues(*edit);
    Check(values.altText == "A dot" && values.wrap == RichTextRun::ImageWrap::Inline, "the dialog reads its alt text");

    values.altText = "A red dot";
    values.wrap = RichTextRun::ImageWrap::Square;
    values.align = RichTextAlign::Right;
    values.widthPt = 72.0f;
    values.heightPt = 72.0f;
    Check(ApplyPictureDialogValues(*edit, values), "OK applies them");
    const RichDocBlock& block = edit->GetDocument()->blocks[static_cast<size_t>(imageBlock)];
    Check(block.type == RichBlockType::Paragraph && block.runs.size() == 1 && block.runs[0].IsInlineImage(),
          "a floating picture is one in a paragraph's line");
    const RichTextRun& run = block.runs[0];
    Check(run.imageWrap == RichTextRun::ImageWrap::Square && run.imageFloatAlign == RichTextAlign::Right,
          "with the text beside it, at the right");
    Check(run.imageAltText == "A red dot" && run.imageWidthPt == 72.0f, "and the alt text and size");
    Check(edit->HasSelectedImage(), "it stays selected");
    RichPictureDialogValues reread = ReadPictureDialogValues(*edit);
    Check(reread.wrap == RichTextRun::ImageWrap::Square && reread.align == RichTextAlign::Right,
          "the dialog reads the wrapping back");
    Check(!ApplyPictureDialogValues(*edit, reread), "OK with nothing changed changes nothing");

    while (edit->CanUndo()) edit->Undo();
    const RichDocBlock& undone = edit->GetDocument()->blocks[static_cast<size_t>(imageBlock)];
    Check(undone.type == RichBlockType::Image && undone.imageAltText == "A dot", "undo gives the picture back as it was");
}

void TestContentsCaptionCrossReference() {
    std::cout << "\nContents, caption and cross-reference\n";
    auto edit = MakeEdit({Paragraph("Intro", RichBlockType::Heading, 1), Paragraph("Text"),
                          Paragraph("Detail", RichBlockType::Heading, 2), Paragraph("Deep", RichBlockType::Heading, 3)});
    edit->GetEditor().SetCaret(RichDocPosition(1, 0));
    RichContentsDialogValues contents = ReadContentsDialogValues(*edit);
    Check(!contents.replaceExisting && contents.maxLevel == 3, "no contents yet: three levels offered");
    contents.maxLevel = 2;
    Check(ApplyContentsDialogValues(*edit, contents), "OK inserts the contents");
    int entries = 0;
    for (const RichDocBlock& block : edit->GetDocument()->blocks) entries += block.tocLevel > 0 ? 1 : 0;
    Check(entries == 2, "of the headings to level 2");
    RichContentsDialogValues again = ReadContentsDialogValues(*edit);
    Check(again.replaceExisting && again.maxLevel == 2, "the dialog then offers to rebuild them");
    again.maxLevel = 3;
    Check(ApplyContentsDialogValues(*edit, again), "OK rebuilds them");
    entries = 0;
    for (const RichDocBlock& block : edit->GetDocument()->blocks) entries += block.tocLevel > 0 ? 1 : 0;
    Check(entries == 3, "with level 3 too, and no second contents");
    Check(edit->Undo(), "the rebuild is an undo step");

    const int textBlock = edit->GetEditor().GetBlockCount() - 3;
    edit->GetEditor().SetCaret(RichDocPosition(textBlock, 0));
    RichCaptionDialogValues caption = ReadCaptionDialogValues(*edit);
    Check(caption.label == "Figure", "a caption under text is a figure's");
    caption.text = "Sales by year";
    const int blocksBefore = edit->GetEditor().GetBlockCount();
    Check(ApplyCaptionDialogValues(*edit, caption), "OK inserts the caption");
    Check(edit->GetEditor().GetBlockCount() == blocksBefore + 1, "as a paragraph of its own");

    const std::vector<RichCrossReferenceTarget> targets = RichCrossReferenceTargets(*edit->GetDocument());
    bool listed = false;
    for (const RichCrossReferenceTarget& target : targets) {
        listed = listed || target.shown.find("Sales by year") != std::string::npos;
        Check(target.bookmark.rfind("_Toc", 0) != 0, "the contents' own marks are not offered");
    }
    Check(listed, "the caption is offered to refer to, by its text");
    edit->GetEditor().SetCaret(RichDocPosition(0, 0));
    RichCrossReferenceDialogValues reference = ReadCrossReferenceDialogValues(*edit);
    Check(!reference.bookmark.empty(), "the dialog starts on the first target");
    reference.pageNumber = true;
    Check(ApplyCrossReferenceDialogValues(*edit, reference), "OK inserts the cross-reference");
    bool field = false;
    for (const RichDocBlock& block : edit->GetDocument()->blocks) {
        for (const RichTextRun& run : block.runs) {
            field = field || (run.field == RichTextRun::Field::PageReference && run.fieldArgument == reference.bookmark);
        }
    }
    Check(field, "a page reference to it");
    Check(edit->Undo(), "an undo step");
    Check(!ApplyCrossReferenceDialogValues(*edit, RichCrossReferenceDialogValues()), "nothing chosen, nothing inserted");
}

void TestNote() {
    std::cout << "\nFootnote\n";
    auto edit = MakeEdit({Paragraph("A claim.")});
    edit->GetEditor().SetCaret(RichDocPosition(0, 7));
    RichNoteDialogValues values = ReadNoteDialogValues(*edit);
    Check(values.kind == RichNote::Kind::Footnote, "a footnote by default");
    values.kind = RichNote::Kind::Endnote;
    values.text = "The source.\nAnd another line.";
    Check(ApplyNoteDialogValues(*edit, values), "OK inserts the note");
    const auto& notes = edit->GetDocument()->notes;
    Check(notes.size() == 1 && notes[0].kind == RichNote::Kind::Endnote, "an endnote");
    Check(notes.size() == 1 && notes[0].blocks.size() == 2
              && UCRichDocument::ConcatenateRunText(notes[0].blocks[0].runs) == "The source.",
          "holding the text, a paragraph a line");
    bool reference = false;
    for (const RichTextRun& run : edit->GetDocument()->blocks[0].runs) reference = reference || run.noteIndex == 0;
    Check(reference, "its reference is in the text");
    Check(!edit->IsEditingHeaderOrFooter(), "a note given its text is not opened for typing");
    Check(edit->Undo(), "an undo step");
    reference = false;
    for (const RichTextRun& run : edit->GetDocument()->blocks[0].runs) reference = reference || run.noteIndex == 0;
    Check(!reference, "undo takes the reference out");
}

void TestComments() {
    std::cout << "\nComments\n";
    auto edit = MakeEdit({Paragraph("alpha beta gamma"), Paragraph("delta")});
    edit->SetCommentAuthor("Ann Lee");
    edit->GetEditor().SetSelection(RichDocPosition(0, 6), RichDocPosition(0, 10));
    RichCommentDialogValues values = ReadCommentDialogValues(*edit, -1);
    Check(values.index < 0 && values.text.empty(), "a new comment starts empty");
    Check(!ApplyCommentDialogValues(*edit, values), "an empty comment is not added");
    values.text = "Check this";
    Check(ApplyCommentDialogValues(*edit, values), "OK adds the comment");
    const auto& comments = edit->GetDocument()->comments;
    Check(comments.size() == 1 && comments[0].author == "Ann Lee" && comments[0].initials == "AL",
          "signed with the author's name");

    edit->GetEditor().SetCaret(RichDocPosition(1, 1));
    edit->GetEditor().SelectWordAt(RichDocPosition(1, 1));
    RichCommentDialogValues second;
    second.text = "Second";
    Check(ApplyCommentDialogValues(*edit, second), "a second comment");

    RichCommentDialogValues edited = ReadCommentDialogValues(*edit, 0);
    Check(edited.index == 0 && edited.text == "Check this", "the dialog reads a comment");
    edited.text = "Check this twice";
    edited.resolved = true;
    Check(ApplyCommentDialogValues(*edit, edited), "OK changes it");
    Check(comments[0].text == "Check this twice" && comments[0].resolved, "its text, and it is resolved");
    Check(DescribeRichComment(comments[0]).rfind("Ann Lee: Check this twice", 0) == 0,
          "the list names its author and text");

    edit->GetEditor().SetCaret(RichDocPosition(0, 0));
    Check(edit->GoToNextComment() && edit->GetSelectedText() == "beta", "next comment selects the first one's text");
    Check(edit->GoToNextComment() && edit->GetSelectedText() == "delta", "then the second's");
    Check(edit->GoToNextComment() && edit->GetSelectedText() == "beta", "and wraps round");
    Check(edit->GoToPreviousComment() && edit->GetSelectedText() == "delta", "previous goes back, wrapping");

    RichCommentsDialogValues list;
    list.index = 0;
    list.action = RichCommentsDialogValues::Action::ToggleResolved;
    Check(ApplyCommentsDialogValues(*edit, list) && !comments[0].resolved, "the list reopens a comment");
    list.action = RichCommentsDialogValues::Action::Delete;
    Check(ApplyCommentsDialogValues(*edit, list), "and deletes one");
    Check(edit->GetDocument()->ActiveComments() == std::vector<int>{1}, "leaving the other");
    Check(edit->Undo() && edit->GetDocument()->ActiveComments().size() == 2, "undo brings it back");
    list.action = RichCommentsDialogValues::Action::GoTo;
    list.index = 1;
    Check(ApplyCommentsDialogValues(*edit, list) && edit->GetSelectedText() == "delta", "and goes to one");
}

void TestReview() {
    std::cout << "\nTracked changes\n";
    auto edit = MakeEdit({Paragraph("one two"), Paragraph("three")});
    edit->SetCommentAuthor("Rev");
    edit->SetTrackChanges(true);
    edit->GetEditor().SetCaret(RichDocPosition(0, 3));
    edit->InsertText(" and");
    edit->GetEditor().SetCaret(RichDocPosition(1, 5));
    edit->InsertText("!");
    Check(edit->GetDocument()->HasTrackedChanges(), "two tracked insertions");
    edit->GetEditor().SetCaret(RichDocPosition(0, 0));
    Check(edit->GoToNextChange() && edit->GetSelectedText() == " and", "next change selects the first");
    Check(edit->GoToNextChange() && edit->GetSelectedText() == "!", "then the second");
    Check(edit->GoToPreviousChange() && edit->GetSelectedText() == " and", "previous goes back to the first");
    Check(edit->GoToPreviousChange() && edit->GetSelectedText() == "!", "and wraps round to the last");
    edit->SetTrackChanges(false);
    Check(edit->RejectChangeAtCaret(), "reject the change at the caret");
    Check(edit->GetEditor().BlockText(1) == "three", "the insertion is gone");
    Check(edit->Undo() && edit->GetEditor().BlockText(1) == "three!", "undo brings it back");
    Check(edit->AcceptAllChanges() && !edit->GetDocument()->HasTrackedChanges(), "accept all");
    Check(edit->GetEditor().BlockText(0) == "one and two", "keeps the text");
}

} // namespace

int main() {
    TestPageSetup();
    TestParagraph();
    TestStyles();
    TestTable();
    TestPicture();
    TestContentsCaptionCrossReference();
    TestNote();
    TestComments();
    TestReview();
    std::cout << "\n" << (checks - failures) << " of " << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
