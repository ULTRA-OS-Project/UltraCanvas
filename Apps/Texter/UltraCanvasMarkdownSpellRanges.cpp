// Apps/Texter/UltraCanvasMarkdownSpellRanges.cpp
// Markdown regions that must not be spell checked
// Version: 1.0.0
// Last Modified: 2026-08-28
// Author: UltraCanvas Framework
//
// Deliberately a plain byte scanner rather than a markdown parser: it has to
// run on every keystroke over the whole document, and it only ever needs to
// answer "is this byte range prose?". Being slightly conservative is fine -
// a missed code span costs a spurious squiggle, never a wrong edit.
//
// No dependency on the editor or the framework, so it can be unit tested on
// its own (Tests/TexterMarkdownSpellRangesTest.cpp).

#include "UltraCanvasMarkdownSpellRanges.h"

#include <algorithm>

namespace UltraCanvas {

    namespace {

        // One source line, newline excluded.
        struct LineSpan {
            size_t start = 0;
            size_t length = 0;
            size_t End() const { return start + length; }
        };

        std::vector<LineSpan> SplitLines(const std::string& text) {
            std::vector<LineSpan> lines;
            size_t lineStart = 0;
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] != '\n') continue;
                size_t end = i;
                if (end > lineStart && text[end - 1] == '\r') --end;   // CRLF
                lines.push_back({lineStart, end - lineStart});
                lineStart = i + 1;
            }
            if (lineStart <= text.size()) {
                size_t end = text.size();
                if (end > lineStart && text[end - 1] == '\r') --end;
                lines.push_back({lineStart, end - lineStart});
            }
            return lines;
        }

        // Index of the first byte of the line that is not a space or tab, and
        // the number of columns those blanks occupy (a tab counts as four).
        size_t FirstNonBlank(const std::string& text, const LineSpan& line, int& indentColumns) {
            indentColumns = 0;
            size_t i = line.start;
            for (; i < line.End(); ++i) {
                if (text[i] == ' ') indentColumns += 1;
                else if (text[i] == '\t') indentColumns += 4;
                else break;
            }
            return i;
        }

        bool IsBlankLine(const std::string& text, const LineSpan& line) {
            int indent = 0;
            return FirstNonBlank(text, line, indent) == line.End();
        }

        // Length of the run of `marker` starting at `pos`.
        size_t RunLength(const std::string& text, size_t pos, size_t limit, char marker) {
            size_t n = 0;
            while (pos + n < limit && text[pos + n] == marker) ++n;
            return n;
        }

        void AddSpan(std::vector<TextByteSpan>& spans, size_t start, size_t end) {
            if (end > start) spans.push_back({start, end - start});
        }

        // A reference definition line: up to three spaces, [label]:, then a
        // destination and optional title that are never prose.
        bool ScanReferenceDefinition(const std::string& text, const LineSpan& line,
                                     size_t firstNonBlank, int indentColumns,
                                     std::vector<TextByteSpan>& spans) {
            if (indentColumns > 3) return false;
            if (firstNonBlank >= line.End() || text[firstNonBlank] != '[') return false;

            size_t closing = text.find(']', firstNonBlank + 1);
            if (closing == std::string::npos || closing + 1 >= line.End()) return false;
            if (text[closing + 1] != ':') return false;

            AddSpan(spans, closing + 1, line.End());
            return true;
        }

        // A math span opened at `pos` with a run of one or two '$'. Returns the
        // byte just past the closing run, or npos when the line has none.
        //
        // "$5 and $10" must stay prose, so a single-dollar span only counts when
        // the content is non-empty and touches neither delimiter with a space.
        size_t FindMathSpanEnd(const std::string& text, size_t pos, size_t limit,
                               size_t markerLength) {
            size_t contentStart = pos + markerLength;
            if (contentStart >= limit) return std::string::npos;

            for (size_t i = contentStart; i + markerLength <= limit; ++i) {
                if (text[i] != '$') continue;
                if (RunLength(text, i, limit, '$') != markerLength) continue;
                if (i == contentStart) return std::string::npos;   // "$$" is empty
                if (markerLength == 1) {
                    if (text[contentStart] == ' ' || text[i - 1] == ' ') return std::string::npos;
                }
                return i + markerLength;
            }
            return std::string::npos;
        }

        // Everything inside one prose line that is markup rather than words.
        void ScanInline(const std::string& text, const LineSpan& line,
                        std::vector<TextByteSpan>& spans) {
            const size_t limit = line.End();
            size_t i = line.start;

            while (i < limit) {
                const char c = text[i];

                if (c == '\\' && i + 1 < limit) {   // escaped punctuation
                    i += 2;
                    continue;
                }

                if (c == '`') {
                    // A code span runs to the next backtick run of the same
                    // length. An unclosed run is literal text, so only the run
                    // itself is skipped.
                    const size_t runLength = RunLength(text, i, limit, '`');
                    size_t closing = std::string::npos;
                    for (size_t j = i + runLength; j < limit; ++j) {
                        if (text[j] != '`') continue;
                        if (RunLength(text, j, limit, '`') == runLength) { closing = j; break; }
                        j += RunLength(text, j, limit, '`') - 1;
                    }
                    const size_t end = (closing == std::string::npos)
                                       ? i + runLength : closing + runLength;
                    AddSpan(spans, i, end);
                    i = end;
                    continue;
                }

                if (c == '$') {
                    const size_t runLength = std::min<size_t>(RunLength(text, i, limit, '$'), 2);
                    const size_t end = FindMathSpanEnd(text, i, limit, runLength);
                    if (end != std::string::npos) {
                        AddSpan(spans, i, end);
                        i = end;
                        continue;
                    }
                    i += runLength;
                    continue;
                }

                if (c == '<') {
                    // An autolink or an inline HTML tag: never prose. A lone '<'
                    // with no '>' on the line is just a character.
                    const size_t closing = text.find('>', i + 1);
                    if (closing != std::string::npos && closing < limit && closing > i + 1) {
                        AddSpan(spans, i, closing + 1);
                        i = closing + 1;
                        continue;
                    }
                    ++i;
                    continue;
                }

                if (c == '(' && i > line.start && text[i - 1] == ']') {
                    // The destination half of [text](target) - and of the image
                    // form ![alt](target), whose alt text stays checkable.
                    int depth = 0;
                    size_t j = i;
                    for (; j < limit; ++j) {
                        if (text[j] == '\\') { ++j; continue; }
                        if (text[j] == '(') ++depth;
                        else if (text[j] == ')') {
                            --depth;
                            if (depth == 0) { ++j; break; }
                        }
                    }
                    const size_t end = std::min(j, limit);
                    AddSpan(spans, i, end);
                    i = end;
                    continue;
                }

                ++i;
            }
        }

        void SortAndMerge(std::vector<TextByteSpan>& spans) {
            if (spans.size() < 2) return;

            std::sort(spans.begin(), spans.end(),
                      [](const TextByteSpan& a, const TextByteSpan& b) {
                          if (a.startByte != b.startByte) return a.startByte < b.startByte;
                          return a.byteLength < b.byteLength;
                      });

            std::vector<TextByteSpan> merged;
            merged.reserve(spans.size());
            for (const TextByteSpan& span : spans) {
                if (!merged.empty() && span.startByte <= merged.back().EndByte()) {
                    const size_t end = std::max(merged.back().EndByte(), span.EndByte());
                    merged.back().byteLength = end - merged.back().startByte;
                } else {
                    merged.push_back(span);
                }
            }
            spans.swap(merged);
        }

    } // namespace

    std::vector<TextByteSpan> ScanMarkdownNoSpellRanges(const std::string& text) {
        std::vector<TextByteSpan> spans;
        if (text.empty()) return spans;

        const std::vector<LineSpan> lines = SplitLines(text);

        size_t firstContentLine = 0;

        // YAML front matter, only when the very first line is the opening fence.
        if (!lines.empty() && text.compare(lines[0].start, lines[0].length, "---") == 0) {
            for (size_t i = 1; i < lines.size(); ++i) {
                const bool closes = text.compare(lines[i].start, lines[i].length, "---") == 0 ||
                                    text.compare(lines[i].start, lines[i].length, "...") == 0;
                if (!closes) continue;
                AddSpan(spans, lines[0].start, lines[i].End());
                firstContentLine = i + 1;
                break;
            }
        }

        bool inFence = false;
        char fenceMarker = '`';
        size_t fenceLength = 0;
        bool previousLineBlank = true;   // start of document behaves like a blank
        bool inIndentedCode = false;

        for (size_t index = firstContentLine; index < lines.size(); ++index) {
            const LineSpan& line = lines[index];

            int indentColumns = 0;
            const size_t firstNonBlank = FirstNonBlank(text, line, indentColumns);
            const bool blank = (firstNonBlank == line.End());

            if (inFence) {
                AddSpan(spans, line.start, line.End());
                if (!blank && indentColumns <= 3) {
                    const size_t run = RunLength(text, firstNonBlank, line.End(), fenceMarker);
                    // A closing fence is the marker alone, at least as long as
                    // the opening one.
                    if (run >= fenceLength) {
                        size_t rest = firstNonBlank + run;
                        while (rest < line.End() && (text[rest] == ' ' || text[rest] == '\t')) ++rest;
                        if (rest == line.End()) inFence = false;
                    }
                }
                previousLineBlank = false;
                continue;
            }

            if (!blank && indentColumns <= 3 &&
                (text[firstNonBlank] == '`' || text[firstNonBlank] == '~')) {
                const char marker = text[firstNonBlank];
                const size_t run = RunLength(text, firstNonBlank, line.End(), marker);
                if (run >= 3) {
                    inFence = true;
                    inIndentedCode = false;
                    fenceMarker = marker;
                    fenceLength = run;
                    AddSpan(spans, line.start, line.End());   // the info string too
                    previousLineBlank = false;
                    continue;
                }
            }

            // Indented code: four columns of indent, opened only after a blank
            // line so that a wrapped list item stays prose.
            if (!blank && indentColumns >= 4 && (inIndentedCode || previousLineBlank)) {
                inIndentedCode = true;
                AddSpan(spans, line.start, line.End());
                previousLineBlank = false;
                continue;
            }
            if (!blank) inIndentedCode = false;

            if (blank) {
                previousLineBlank = true;
                continue;
            }
            previousLineBlank = false;

            if (ScanReferenceDefinition(text, line, firstNonBlank, indentColumns, spans)) {
                continue;
            }

            ScanInline(text, line, spans);
        }

        SortAndMerge(spans);
        return spans;
    }

    bool SpanCoversRange(const std::vector<TextByteSpan>& spans,
                         size_t startByte, size_t byteLength) {
        if (spans.empty() || byteLength == 0) return false;

        // First span that could reach past startByte.
        auto it = std::upper_bound(spans.begin(), spans.end(), startByte,
                                   [](size_t offset, const TextByteSpan& span) {
                                       return offset < span.EndByte();
                                   });
        if (it == spans.end()) return false;
        return it->startByte < startByte + byteLength;
    }

} // namespace UltraCanvas
