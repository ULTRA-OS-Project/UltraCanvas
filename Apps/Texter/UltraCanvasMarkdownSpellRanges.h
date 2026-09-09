// Apps/Texter/UltraCanvasMarkdownSpellRanges.h
// Markdown regions that must not be spell checked
// Version: 1.0.0
// Last Modified: 2026-08-28
// Author: UltraCanvas Framework
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace UltraCanvas {

    // A half-open byte range [startByte, startByte + byteLength) of a document.
    struct TextByteSpan {
        size_t startByte = 0;
        size_t byteLength = 0;

        size_t EndByte() const { return startByte + byteLength; }
    };

    // Byte ranges of a markdown document whose words are not prose and would
    // only produce noise if flagged:
    //
    //   - fenced code blocks (``` and ~~~), including the fence lines
    //   - indented code blocks (four spaces or a tab, outside a list)
    //   - inline code spans (`code`, ``code with ` in it``)
    //   - link and image destinations - the (target) half of [text](target),
    //     reference definitions, and bare autolinks <https://...>
    //   - inline HTML tags
    //   - math spans ($...$ and $$...$$)
    //   - YAML front matter at the very start of the document
    //
    // The result is sorted by startByte and non-overlapping, so SpanCoversRange
    // can binary search it. Byte offsets, because that is what
    // SpellCheckOptions::shouldSkipRange is given.
    std::vector<TextByteSpan> ScanMarkdownNoSpellRanges(const std::string& text);

    // True when [startByte, startByte + byteLength) intersects any span.
    // `spans` must be sorted and non-overlapping, as returned above.
    bool SpanCoversRange(const std::vector<TextByteSpan>& spans,
                         size_t startByte, size_t byteLength);

} // namespace UltraCanvas
