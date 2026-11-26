// include/UltraCanvasRichDocumentOutline.h
// RichDocumentOutline — what a document navigator lists, read from the model:
// the headings as a tree by level, the tables, the pictures, the charts, the
// comments (with their authors), the bookmarks, the footnotes and endnotes
// and the tracked changes, each with the place it is at, so a click can go
// there. UltraCanvasDocumentNavigator (UltraCanvasDocumentNavigator.h) shows
// it beside an UltraCanvasRichTextEdit; moving and promoting a heading's
// section is the editing core's (UCRichDocumentEditor::MoveHeadingSection,
// ChangeHeadingSectionLevel), so it is undone like any other edit.
//
// Like the model it reads, this is UI-free (std types only): it builds in
// the display-free tests and in any tool that wants a document's outline.
// The words a person reads ("Table 2", "3 rows") are the view's; the
// outline carries the facts they are made from.
//
// doc-check: example
//     UltraCanvas::UCRichDocument document = UltraCanvas::UCRichDocument::FromMarkdown("# One\n\n## Two\n");
//     UltraCanvas::RichDocumentOutline outline = UltraCanvas::BuildRichDocumentOutline(document);
//     int parent = outline.headings[1].parent;               // 0: "Two" is under "One"
//     int under = outline.HeadingAt(outline.headings[1].blockIndex);   // 1
//     (void)parent; (void)under;
//
// Version: 1.0.0 - headings, tables, pictures, charts, comments, bookmarks, notes and tracked changes
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"
#include "UltraCanvasRichDocumentEditor.h"   // RichDocPosition, RichDocRange

#include <string>
#include <vector>

namespace UltraCanvas {

// A heading: a Heading block of level 1..6, in document order.
struct RichOutlineHeading {
    int blockIndex = 0;
    int level = 1;
    std::string text;               // its words (deleted text left out)
    // The heading it stands under (index into RichDocumentOutline::headings),
    // -1 at the top: the nearest heading of a higher level before it.
    int parent = -1;
    int sectionEnd = 0;             // one past its section's last block (UCRichDocumentEditor::HeadingSectionEnd)
};

struct RichOutlineTable {
    int blockIndex = 0;
    int rows = 0;
    int columns = 0;                // grid columns (spans counted)
    std::string caption;            // the caption paragraph after (or before) it, "" for none
    std::string firstCellText;      // what it starts with: a header, often its subject
};

// A picture: a picture paragraph (an Image block, at {block, 0}) or one in
// the line of a paragraph or a table cell, at its placeholder.
struct RichOutlinePicture {
    RichDocPosition position;
    int mediaIndex = -1;
    std::string name;               // the media's name in the package ("image1.png")
    std::string altText;
    std::string caption;            // the caption paragraph after it, "" for none
    bool floating = false;          // placed apart from the line, the text wrapping round it
};

struct RichOutlineChart {
    RichDocPosition position;       // its frame's placeholder
    int frameIndex = -1;
    RichChart::Kind kind = RichChart::Kind::NoChart;
    std::string title;
    std::string caption;
};

// A comment some text is under (one no text refers to any more is not shown,
// so it is not listed either).
struct RichOutlineComment {
    int index = 0;                  // into UCRichDocument::comments
    std::string author;
    std::string date;
    std::string text;
    bool resolved = false;
    RichDocRange range;             // the text it is on
    std::string commentedText;      // ...and what that text says
};

struct RichOutlineBookmark {
    std::string name;
    int blockIndex = 0;
    std::string text;               // the words of the block it is on
    bool contentsMark = false;      // a "_Toc" mark the table of contents gave a heading
};

// A footnote's or endnote's reference, in document order.
struct RichOutlineNote {
    int noteIndex = 0;              // into UCRichDocument::notes
    RichNote::Kind kind = RichNote::Kind::Footnote;
    std::string mark;               // "1", "ii": what it shows (UCRichDocument::NoteMarks)
    std::string text;               // the note's words
    RichDocPosition position;       // its reference
};

// One tracked change: neighbouring runs inserted (or deleted) under one
// revision.
struct RichOutlineChange {
    RichTextRun::Change change = RichTextRun::Change::Inserted;
    int revision = -1;
    std::string author;
    std::string date;
    std::string text;
    RichDocRange range;
};

struct RichDocumentOutline {
    std::vector<RichOutlineHeading> headings;
    std::vector<RichOutlineTable> tables;
    std::vector<RichOutlinePicture> pictures;
    std::vector<RichOutlineChart> charts;
    std::vector<RichOutlineComment> comments;
    std::vector<RichOutlineBookmark> bookmarks;
    std::vector<RichOutlineNote> notes;
    std::vector<RichOutlineChange> changes;

    // The heading whose section block `blockIndex` is in most closely - the
    // last heading at or before it (index into headings); -1 before the first.
    int HeadingAt(int blockIndex) const;
    bool IsEmpty() const {
        return headings.empty() && tables.empty() && pictures.empty() && charts.empty() && comments.empty()
            && bookmarks.empty() && notes.empty() && changes.empty();
    }
    bool operator==(const RichDocumentOutline& other) const;
};

// The outline of the document's body (headers, footers, notes and frames are
// not walked, except a note's text for its entry).
RichDocumentOutline BuildRichDocumentOutline(const UCRichDocument& document);

// The words of a run list as one line: placeholders, line breaks and deleted
// text left out, white space folded, trimmed.
std::string RichOutlineText(const std::vector<RichTextRun>& runs);

} // namespace UltraCanvas
