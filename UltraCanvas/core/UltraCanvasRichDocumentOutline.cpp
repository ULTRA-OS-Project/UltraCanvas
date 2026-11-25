// core/UltraCanvasRichDocumentOutline.cpp
// See include/UltraCanvasRichDocumentOutline.h.
//
// Version: 1.0.0 - headings, tables, pictures, charts, comments, bookmarks, notes and tracked changes
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#include "UltraCanvasRichDocumentOutline.h"

#include <algorithm>
#include <functional>

namespace UltraCanvas {

namespace {

constexpr size_t kCommentedTextLimit = 200;

bool IsCaption(const RichDocBlock& block) {
    if (block.type != RichBlockType::Paragraph) return false;
    for (const RichTextRun& run : block.runs) {
        if (run.field == RichTextRun::Field::Sequence) return true;
    }
    return false;
}

std::string CaptionAt(const UCRichDocument& document, int index) {
    if (index < 0 || index >= static_cast<int>(document.blocks.size())) return "";
    const RichDocBlock& block = document.blocks[static_cast<size_t>(index)];
    return IsCaption(block) ? RichOutlineText(block.runs) : std::string();
}

// A paragraph holding nothing but one picture or frame: the paragraph a
// caption after it describes.
bool HoldsOnlyObject(const RichDocBlock& block) {
    int objects = 0;
    for (const RichTextRun& run : block.runs) {
        if (run.IsInlineImage() || run.IsTextFrame()) { ++objects; continue; }
        for (char c : run.text) {
            if (c != ' ' && c != '\t' && c != '\n') return false;
        }
    }
    return objects == 1;
}

// Calls `visit(runs, position-of-the-container)` for every text container of
// the body in document order: a block's own runs, then a table's cells row by
// row (RichDocPosition::cellColumn is the cell's index in its row).
void ForEachContainer(const UCRichDocument& document,
                      const std::function<void(const std::vector<RichTextRun>&, const RichDocPosition&)>& visit) {
    for (size_t b = 0; b < document.blocks.size(); ++b) {
        const RichDocBlock& block = document.blocks[b];
        const int bi = static_cast<int>(b);
        if (block.type == RichBlockType::Table) {
            for (size_t r = 0; r < block.tableRows.size(); ++r) {
                const RichTableRow& row = block.tableRows[r];
                for (size_t c = 0; c < row.cells.size(); ++c) {
                    visit(row.cells[c].runs, RichDocPosition(bi, static_cast<int>(r), static_cast<int>(c), 0));
                }
            }
        } else {
            visit(block.runs, RichDocPosition(bi, 0));
        }
    }
}

bool SameRange(const RichDocRange& a, const RichDocRange& b) { return a.start == b.start && a.end == b.end; }

void AppendLimited(std::string& to, const std::string& text, size_t limit) {
    if (to.size() >= limit) return;
    to += text.substr(0, limit - to.size());
}

} // namespace

std::string RichOutlineText(const std::vector<RichTextRun>& runs) {
    std::string out;
    bool space = false;
    auto put = [&](char c) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = !out.empty();
            return;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    };
    const std::string placeholder = RichTextRun::kObjectReplacement;
    for (const RichTextRun& run : runs) {
        if (run.IsDeleted()) continue;
        if (run.lineBreakBefore) put(' ');
        if (run.IsInlineImage() || run.IsTextFrame() || run.text == placeholder) continue;
        for (char c : run.text) put(c);
    }
    return out;
}

int RichDocumentOutline::HeadingAt(int blockIndex) const {
    // The headings are in document order: the last one at or before the block.
    auto it = std::upper_bound(headings.begin(), headings.end(), blockIndex,
                               [](int block, const RichOutlineHeading& heading) { return block < heading.blockIndex; });
    return static_cast<int>(it - headings.begin()) - 1;
}

bool RichDocumentOutline::operator==(const RichDocumentOutline& o) const {
    auto same = [](const auto& a, const auto& b, auto equal) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (!equal(a[i], b[i])) return false;
        }
        return true;
    };
    return same(headings, o.headings, [](const RichOutlineHeading& a, const RichOutlineHeading& b) {
               return a.blockIndex == b.blockIndex && a.level == b.level && a.text == b.text && a.parent == b.parent
                   && a.sectionEnd == b.sectionEnd;
           })
        && same(tables, o.tables, [](const RichOutlineTable& a, const RichOutlineTable& b) {
               return a.blockIndex == b.blockIndex && a.rows == b.rows && a.columns == b.columns
                   && a.caption == b.caption && a.firstCellText == b.firstCellText;
           })
        && same(pictures, o.pictures, [](const RichOutlinePicture& a, const RichOutlinePicture& b) {
               return a.position == b.position && a.mediaIndex == b.mediaIndex && a.name == b.name
                   && a.altText == b.altText && a.caption == b.caption && a.floating == b.floating;
           })
        && same(charts, o.charts, [](const RichOutlineChart& a, const RichOutlineChart& b) {
               return a.position == b.position && a.frameIndex == b.frameIndex && a.kind == b.kind
                   && a.title == b.title && a.caption == b.caption;
           })
        && same(comments, o.comments, [](const RichOutlineComment& a, const RichOutlineComment& b) {
               return a.index == b.index && a.author == b.author && a.date == b.date && a.text == b.text
                   && a.resolved == b.resolved && SameRange(a.range, b.range) && a.commentedText == b.commentedText;
           })
        && same(bookmarks, o.bookmarks, [](const RichOutlineBookmark& a, const RichOutlineBookmark& b) {
               return a.name == b.name && a.blockIndex == b.blockIndex && a.text == b.text
                   && a.contentsMark == b.contentsMark;
           })
        && same(notes, o.notes, [](const RichOutlineNote& a, const RichOutlineNote& b) {
               return a.noteIndex == b.noteIndex && a.kind == b.kind && a.mark == b.mark && a.text == b.text
                   && a.position == b.position;
           })
        && same(changes, o.changes, [](const RichOutlineChange& a, const RichOutlineChange& b) {
               return a.change == b.change && a.revision == b.revision && a.author == b.author && a.date == b.date
                   && a.text == b.text && SameRange(a.range, b.range);
           });
}

RichDocumentOutline BuildRichDocumentOutline(const UCRichDocument& document) {
    RichDocumentOutline outline;
    const int blockCount = static_cast<int>(document.blocks.size());

    // ===== HEADINGS AND TABLES =====
    std::vector<int> open;   // the headings above the current one, by index, levels rising
    for (int b = 0; b < blockCount; ++b) {
        const RichDocBlock& block = document.blocks[static_cast<size_t>(b)];
        if (block.type == RichBlockType::Heading && block.headingLevel >= 1 && block.headingLevel <= 6) {
            RichOutlineHeading heading;
            heading.blockIndex = b;
            heading.level = block.headingLevel;
            heading.text = RichOutlineText(block.runs);
            heading.sectionEnd = UCRichDocumentEditor::HeadingSectionEnd(document.blocks, b);
            while (!open.empty() && outline.headings[static_cast<size_t>(open.back())].level >= heading.level) {
                open.pop_back();
            }
            heading.parent = open.empty() ? -1 : open.back();
            open.push_back(static_cast<int>(outline.headings.size()));
            outline.headings.push_back(std::move(heading));
        } else if (block.type == RichBlockType::Table) {
            RichOutlineTable table;
            table.blockIndex = b;
            const RichTableGrid grid = BuildTableGrid(block);
            table.rows = grid.rowCount;
            table.columns = grid.columnCount;
            table.caption = CaptionAt(document, b + 1);
            if (table.caption.empty()) table.caption = CaptionAt(document, b - 1);
            for (const RichTableRow& row : block.tableRows) {
                for (const RichTableCell& cell : row.cells) {
                    table.firstCellText = RichOutlineText(cell.runs);
                    if (!table.firstCellText.empty()) break;
                }
                if (!table.firstCellText.empty()) break;
            }
            outline.tables.push_back(std::move(table));
        } else if (block.type == RichBlockType::Image && block.mediaIndex >= 0) {
            RichOutlinePicture picture;
            picture.position = RichDocPosition(b, 0);
            picture.mediaIndex = block.mediaIndex;
            if (block.mediaIndex < static_cast<int>(document.media.size()))
                picture.name = document.media[static_cast<size_t>(block.mediaIndex)].name;
            picture.altText = block.imageAltText;
            picture.caption = CaptionAt(document, b + 1);
            outline.pictures.push_back(std::move(picture));
        }

        // Bookmarks on the block.
        for (const std::string& name : block.bookmarks) {
            RichOutlineBookmark bookmark;
            bookmark.name = name;
            bookmark.blockIndex = b;
            bookmark.text = block.type == RichBlockType::Table ? std::string() : RichOutlineText(block.runs);
            bookmark.contentsMark = name.rfind("_Toc", 0) == 0;
            outline.bookmarks.push_back(std::move(bookmark));
        }
    }

    // ===== WHAT SITS IN THE TEXT: PICTURES, CHARTS, COMMENTS, CHANGES =====
    struct CommentSpan {
        RichDocPosition start, end;
        std::string text;
        bool seen = false;
    };
    std::vector<CommentSpan> spans(document.comments.size());
    ForEachContainer(document, [&](const std::vector<RichTextRun>& runs, const RichDocPosition& container) {
        const bool inCell = container.InCell();
        const RichDocBlock& block = document.blocks[static_cast<size_t>(container.blockIndex)];
        const bool soleObject = !inCell && HoldsOnlyObject(block);
        int offset = 0;
        RichOutlineChange* openChange = nullptr;
        for (const RichTextRun& run : runs) {
            const int start = offset + (run.lineBreakBefore ? 1 : 0);
            const int end = start + static_cast<int>(run.text.size());
            offset = end;
            RichDocPosition at = container;
            at.byteOffset = start;
            RichDocPosition after = container;
            after.byteOffset = end;

            if (run.IsInlineImage()) {
                RichOutlinePicture picture;
                picture.position = at;
                picture.mediaIndex = run.mediaIndex;
                if (run.mediaIndex < static_cast<int>(document.media.size()))
                    picture.name = document.media[static_cast<size_t>(run.mediaIndex)].name;
                picture.altText = run.imageAltText;
                picture.floating = run.IsFloatingImage();
                if (soleObject) picture.caption = CaptionAt(document, container.blockIndex + 1);
                outline.pictures.push_back(std::move(picture));
            } else if (const RichTextFrame* frame = document.FrameFor(run); frame && frame->chart.IsChart()) {
                RichOutlineChart chart;
                chart.position = at;
                chart.frameIndex = run.frameIndex;
                chart.kind = frame->chart.kind;
                chart.title = frame->chart.title;
                if (soleObject) chart.caption = CaptionAt(document, container.blockIndex + 1);
                outline.charts.push_back(std::move(chart));
            }

            for (int id : run.commentIds) {
                if (id < 0 || id >= static_cast<int>(spans.size())) continue;
                CommentSpan& span = spans[static_cast<size_t>(id)];
                if (!span.seen) {
                    span.seen = true;
                    span.start = at;
                } else if (!span.end.SameContainer(at) && !span.text.empty()) {
                    AppendLimited(span.text, " ", kCommentedTextLimit);
                }
                span.end = after;
                if (!run.IsDeleted() && !run.IsInlineImage() && !run.IsTextFrame())
                    AppendLimited(span.text, run.text, kCommentedTextLimit);
            }

            if (run.change == RichTextRun::Change::Unchanged) {
                openChange = nullptr;
                continue;
            }
            if (openChange && openChange->change == run.change && openChange->revision == run.revision
                && openChange->range.end == at) {
                openChange->range.end = after;
                openChange->text += run.text;
                continue;
            }
            RichOutlineChange change;
            change.change = run.change;
            change.revision = run.revision;
            if (run.revision >= 0 && run.revision < static_cast<int>(document.revisions.size())) {
                change.author = document.revisions[static_cast<size_t>(run.revision)].author;
                change.date = document.revisions[static_cast<size_t>(run.revision)].date;
            }
            change.text = run.text;
            change.range = RichDocRange(at, after);
            outline.changes.push_back(std::move(change));
            openChange = &outline.changes.back();
        }
    });
    // A change's text reads as one line, like every other entry's.
    for (RichOutlineChange& change : outline.changes) {
        RichTextRun run;
        run.text = change.text;
        change.text = RichOutlineText({run});
    }

    // Pictures in document order: a picture paragraph was listed with the
    // blocks, those in the line as their containers were walked.
    std::stable_sort(outline.pictures.begin(), outline.pictures.end(),
                     [](const RichOutlinePicture& a, const RichOutlinePicture& b) { return a.position < b.position; });

    for (size_t i = 0; i < spans.size(); ++i) {
        const CommentSpan& span = spans[i];
        if (!span.seen) continue;
        const RichComment& source = document.comments[i];
        RichOutlineComment comment;
        comment.index = static_cast<int>(i);
        comment.author = source.author;
        comment.date = source.date;
        comment.text = source.text;
        comment.resolved = source.resolved;
        comment.range = RichDocRange(span.start, span.end);
        RichTextRun run;
        run.text = span.text;
        comment.commentedText = RichOutlineText({run});
        outline.comments.push_back(std::move(comment));
    }
    std::stable_sort(outline.comments.begin(), outline.comments.end(),
                     [](const RichOutlineComment& a, const RichOutlineComment& b) { return a.range.start < b.range.start; });

    // ===== NOTES =====
    const std::vector<std::string> marks = document.NoteMarks();
    std::vector<bool> listed(document.notes.size(), false);
    for (const UCRichDocument::NoteReference& reference : document.NoteReferences()) {
        if (reference.noteIndex < 0 || reference.noteIndex >= static_cast<int>(document.notes.size())) continue;
        if (listed[static_cast<size_t>(reference.noteIndex)]) continue;   // a second reference to the same note
        listed[static_cast<size_t>(reference.noteIndex)] = true;
        const RichNote& source = document.notes[static_cast<size_t>(reference.noteIndex)];
        RichOutlineNote note;
        note.noteIndex = reference.noteIndex;
        note.kind = source.kind;
        if (reference.noteIndex < static_cast<int>(marks.size())) note.mark = marks[static_cast<size_t>(reference.noteIndex)];
        for (const RichDocBlock& block : source.blocks) {
            const std::string text = RichOutlineText(block.runs);
            if (text.empty()) continue;
            if (!note.text.empty()) note.text += ' ';
            note.text += text;
        }
        // Where the reference is: its run's offset in its container.
        const RichDocBlock& block = document.blocks[static_cast<size_t>(reference.blockIndex)];
        const std::vector<RichTextRun>* runs = &block.runs;
        RichDocPosition at(reference.blockIndex, 0);
        if (reference.cellRow >= 0 && reference.cellRow < static_cast<int>(block.tableRows.size())) {
            const RichTableRow& row = block.tableRows[static_cast<size_t>(reference.cellRow)];
            if (reference.cellIndex >= 0 && reference.cellIndex < static_cast<int>(row.cells.size())) {
                runs = &row.cells[static_cast<size_t>(reference.cellIndex)].runs;
                at = RichDocPosition(reference.blockIndex, reference.cellRow, reference.cellIndex, 0);
            }
        }
        int offset = 0;
        for (int r = 0; r < reference.runIndex && r < static_cast<int>(runs->size()); ++r) {
            const RichTextRun& run = (*runs)[static_cast<size_t>(r)];
            offset += (run.lineBreakBefore ? 1 : 0) + static_cast<int>(run.text.size());
        }
        if (reference.runIndex < static_cast<int>(runs->size()) && (*runs)[static_cast<size_t>(reference.runIndex)].lineBreakBefore)
            ++offset;
        at.byteOffset = offset;
        note.position = at;
        outline.notes.push_back(std::move(note));
    }
    return outline;
}

} // namespace UltraCanvas
