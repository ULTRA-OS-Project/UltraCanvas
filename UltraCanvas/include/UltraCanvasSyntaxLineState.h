// UltraCanvasSyntaxLineState.h
// What one highlighted line leaves open for the next, so a construct that
// spans lines - a /* block comment */, a string or comment cut where the text
// area splits an over-long line into segments, CSS's place inside its rules -
// is coloured on every line it covers, not only on the one that opens it.
// Version: 1.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>

namespace UltraCanvas {

    struct SyntaxLineState {
        // Index into the language's multiLineComments of the comment still
        // open at the end of the line; -1 when none is.
        int8_t blockComment = -1;
        // A string or single-line comment the line ended inside. Both
        // continue into the next segment of a line the text area split for
        // length; at a real line break (AtLineBreak) both end, unless the
        // language lets that string span lines (openStringSpansLines: a
        // multiLineStringDelimiters quote, or a backslash continuation).
        char openString = 0;
        bool openStringSpansLines = false;
        bool lineComment = false;
        // Index into the language's multiLineStrings of the string still open
        // at the end of the line (Python's """); -1 when none is.
        int8_t openLongString = -1;

        // CSS: the context the line ended in and the blocks still open.
        // cssStack holds one bit per open block, 1 = the block was opened
        // from a declaration list (nested rule), 0 = from a selector list.
        uint8_t cssContext = 0;          // 0 selector, 1 declaration, 2 at-rule prelude
        uint8_t cssPreludeParent = 0;    // context an at-rule prelude returns to at ';'
        uint8_t cssDepth = 0;
        uint32_t cssStack = 0;
        bool cssExpectProperty = false;
        bool cssAtHoldsDeclarations = false;

        // The state the line after a real line break starts from.
        SyntaxLineState AtLineBreak() const {
            SyntaxLineState s = *this;
            // The flag stays: an empty line (tokenized to nothing) must not
            // drop a template literal; the next line with text recomputes it.
            if (!s.openStringSpansLines) s.openString = 0;
            s.lineComment = false;
            return s;
        }

        bool operator==(const SyntaxLineState& o) const {
            return blockComment == o.blockComment && openString == o.openString &&
                   openStringSpansLines == o.openStringSpansLines &&
                   lineComment == o.lineComment && openLongString == o.openLongString &&
                   cssContext == o.cssContext &&
                   cssPreludeParent == o.cssPreludeParent && cssDepth == o.cssDepth &&
                   cssStack == o.cssStack && cssExpectProperty == o.cssExpectProperty &&
                   cssAtHoldsDeclarations == o.cssAtHoldsDeclarations;
        }
        bool operator!=(const SyntaxLineState& o) const { return !(*this == o); }
    };

} // namespace UltraCanvas
