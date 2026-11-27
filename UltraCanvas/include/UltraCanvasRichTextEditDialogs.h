// include/UltraCanvasRichTextEditDialogs.h
// The document dialogs of a word processor, for any application that edits
// with UltraCanvasRichTextEdit: page setup, paragraph, named styles, table
// properties, a picture's alt text and wrapping, inserting a table of
// contents, a caption, a cross-reference or a footnote, and a comment, with
// the list of a document's comments. Each is a form (UltraCanvasFormLayout)
// of the framework's own elements in an UltraCanvasModalDialog, over a struct
// the model already has.
//
// Every dialog comes in three parts, so what it does can be tested without a
// window:
//   Read<Name>DialogValues(edit)          what the form opens with
//   Apply<Name>DialogValues(edit, values) what OK does: true when it changed
//                                         the document (one undo step, where
//                                         the editor keeps one)
//   Show<Name>Dialog(edit, onClosed)      the window: it reads, lets the
//                                         person change the values, and on
//                                         OK applies them. Cancel changes
//                                         nothing. onClosed(applied) runs
//                                         after either.
// A dialog is modal over the element's window and keeps the element alive
// only as long as it is open (it holds it weakly).
//
// doc-check: std::shared_ptr<UltraCanvasRichTextEdit> edit;
// doc-check: example
//     RichParagraphFormat format = ReadParagraphDialogValues(*edit);
//     format.spaceAfterPt = 6.0f;
//     ApplyParagraphDialogValues(*edit, format);     // one undo step
//     ShowParagraphDialog(*edit, [](bool applied) { (void)applied; });
//
// Version: 1.0.1 - RichPictureDialogValues read a picture of its own size at that size, not 0 x 0
// Version: 1.0.0 - page setup, paragraph, styles, table properties, picture, contents, caption,
//                  cross-reference, footnote, comment and comments dialogs
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocumentEditor.h"

#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasRichTextEdit;

// What a Show...Dialog calls when it closes: true when OK changed the document.
using RichDialogClosed = std::function<void(bool applied)>;

// ===== PAGE SETUP =====

// A paper size, upright (width < height), in points.
struct RichPaperSize {
    std::string name;       // "A4", "Letter" (shown through Tr)
    float widthPt = 0.0f;
    float heightPt = 0.0f;
};
// A3, A4, A5, B5, Letter, Legal, Tabloid.
const std::vector<RichPaperSize>& RichPaperSizes();

struct RichPageSetupDialogValues {
    int paper = -1;                 // index into RichPaperSizes(); -1 = custom (widthPt, heightPt)
    bool landscape = false;         // the paper turned on its side
    float widthPt = 0.0f;           // the page as it stands (a custom paper's size)
    float heightPt = 0.0f;
    float marginTopPt = 0.0f, marginBottomPt = 0.0f, marginLeftPt = 0.0f, marginRightPt = 0.0f;
    float headerTopPt = 0.0f;       // the header's distance from the top edge
    float footerBottomPt = 0.0f;    // the footer's distance from the bottom edge
};
// The document's page (A4 with 2 cm margins when it states none), with the
// paper it matches.
RichPageSetupDialogValues ReadPageSetupDialogValues(const UltraCanvasRichTextEdit& edit);
// The page the values describe: a listed paper's size, turned for landscape.
RichPageSetup PageSetupFromDialogValues(const RichPageSetupDialogValues& values);
bool ApplyPageSetupDialogValues(UltraCanvasRichTextEdit& edit, const RichPageSetupDialogValues& values);
void ShowPageSetupDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);

// ===== PARAGRAPH =====
// The values are the caret paragraph's RichParagraphFormat; OK gives them to
// every paragraph the selection touches.
RichParagraphFormat ReadParagraphDialogValues(const UltraCanvasRichTextEdit& edit);
bool ApplyParagraphDialogValues(UltraCanvasRichTextEdit& edit, const RichParagraphFormat& values);
void ShowParagraphDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);

// ===== NAMED STYLES =====
struct RichStylesDialogValues {
    enum class Action {
        Apply,              // give the selection styleId
        Change,             // give styleId the name, character and paragraph below
        NewFromSelection    // a new paragraph style named `name`, from the caret's paragraph
    };
    Action action = Action::Apply;
    std::string styleId;
    std::string name;
    // Change: what the dialog shows of a style. Properties left unset keep
    // what the style had.
    RichStyleCharacter character;
    RichStyleParagraph paragraph;
};
// The caret paragraph's style, as Apply.
RichStylesDialogValues ReadStylesDialogValues(const UltraCanvasRichTextEdit& edit);
// The values a style shows when it is picked in the dialog.
RichStylesDialogValues StylesDialogValuesFor(const UltraCanvasRichTextEdit& edit, const std::string& styleId);
bool ApplyStylesDialogValues(UltraCanvasRichTextEdit& edit, const RichStylesDialogValues& values);
void ShowStylesDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);

// ===== TABLE PROPERTIES =====
// The caret's table; Read gives the defaults when the caret is in none (and
// Show then says so instead of opening a form).
RichTableProperties ReadTableDialogValues(const UltraCanvasRichTextEdit& edit);
bool ApplyTableDialogValues(UltraCanvasRichTextEdit& edit, const RichTableProperties& values);
void ShowTablePropertiesDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);

// ===== PICTURE =====
struct RichPictureDialogValues {
    std::string altText;
    RichTextRun::ImageWrap wrap = RichTextRun::ImageWrap::Inline;
    RichTextAlign align = RichTextAlign::Left;   // where a floating picture sits across the column
    // Read: the size it is laid out at (GetImageDisplaySize), its own size
    // where the document states 0. Applied: 0 leaves the size as it is.
    float widthPt = 0.0f;
    float heightPt = 0.0f;
};
// The selected picture's; Show says to select one when none is.
RichPictureDialogValues ReadPictureDialogValues(const UltraCanvasRichTextEdit& edit);
bool ApplyPictureDialogValues(UltraCanvasRichTextEdit& edit, const RichPictureDialogValues& values);
void ShowPictureDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);

// ===== TABLE OF CONTENTS =====
struct RichContentsDialogValues {
    int maxLevel = 3;               // headings 1..maxLevel are listed
    // The document has contents already: rebuild those (true) or insert
    // another at the caret (false).
    bool replaceExisting = false;
};
RichContentsDialogValues ReadContentsDialogValues(const UltraCanvasRichTextEdit& edit);
bool ApplyContentsDialogValues(UltraCanvasRichTextEdit& edit, const RichContentsDialogValues& values);
void ShowContentsDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);
// True when the document has a table of contents.
bool HasTableOfContents(const UCRichDocument& document);

// ===== CAPTION =====
struct RichCaptionDialogValues {
    std::string label;              // "Figure", "Table", "Equation" - numbered per label
    std::string text;               // after the number
};
// "Table" under a table, else "Figure".
RichCaptionDialogValues ReadCaptionDialogValues(const UltraCanvasRichTextEdit& edit);
bool ApplyCaptionDialogValues(UltraCanvasRichTextEdit& edit, const RichCaptionDialogValues& values);
void ShowCaptionDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);

// ===== CROSS-REFERENCE =====
// What a cross-reference can point at: the document's bookmarks and
// captions, not the contents' own heading marks.
struct RichCrossReferenceTarget {
    std::string bookmark;           // the name the field refers to
    std::string shown;              // what the list shows: a caption's text, or the bookmark's name
};
std::vector<RichCrossReferenceTarget> RichCrossReferenceTargets(const UCRichDocument& document);
struct RichCrossReferenceDialogValues {
    std::string bookmark;
    bool pageNumber = false;        // its page number rather than its text
};
RichCrossReferenceDialogValues ReadCrossReferenceDialogValues(const UltraCanvasRichTextEdit& edit);
bool ApplyCrossReferenceDialogValues(UltraCanvasRichTextEdit& edit, const RichCrossReferenceDialogValues& values);
void ShowCrossReferenceDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);

// ===== FOOTNOTE =====
struct RichNoteDialogValues {
    RichNote::Kind kind = RichNote::Kind::Footnote;
    std::string text;               // empty: the note opens for typing
};
RichNoteDialogValues ReadNoteDialogValues(const UltraCanvasRichTextEdit& edit);
bool ApplyNoteDialogValues(UltraCanvasRichTextEdit& edit, const RichNoteDialogValues& values);
void ShowNoteDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);

// ===== COMMENTS =====
struct RichCommentDialogValues {
    int index = -1;                 // -1: a new comment on the selection
    std::string text;
    bool resolved = false;
};
// Comment `index` (-1: a new one).
RichCommentDialogValues ReadCommentDialogValues(const UltraCanvasRichTextEdit& edit, int index);
bool ApplyCommentDialogValues(UltraCanvasRichTextEdit& edit, const RichCommentDialogValues& values);
// A new comment on the selection, or comment `index` to edit.
void ShowCommentDialog(UltraCanvasRichTextEdit& edit, int index = -1, RichDialogClosed onClosed = nullptr);

// The list of the document's comments: one picked is gone to, resolved or
// reopened, or deleted.
struct RichCommentsDialogValues {
    enum class Action { GoTo, ToggleResolved, Delete };
    int index = -1;
    Action action = Action::GoTo;
};
// One line per comment shown in the list: "Author: text" (text shortened).
std::string DescribeRichComment(const RichComment& comment);
bool ApplyCommentsDialogValues(UltraCanvasRichTextEdit& edit, const RichCommentsDialogValues& values);
void ShowCommentsDialog(UltraCanvasRichTextEdit& edit, RichDialogClosed onClosed = nullptr);

} // namespace UltraCanvas
