// Tests/Fuzz/HTMLParserFuzz.cpp
// Fuzz target: the HTML reader's parser on any bytes, then the cascade over
// the document's own <style> sheets - what UltraMail does with every message
// body and the eBook engines with every chapter, none of which is trusted.
// Also the two DOM-free helpers (DecodeEntities, ExtractPlainText) on the
// same bytes.
//
// Built as a libFuzzer target (ULTRACANVAS_BUILD_FUZZERS, clang) and as a
// deterministic smoke run in ctest (FuzzSmokeMain.cpp). See Tests/Fuzz/README.md.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "HTMLReader/HTMLStyleResolver.h"

#include <cstddef>
#include <cstdint>
#include <string>

using namespace UltraCanvas::HTML;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Past this the time goes into copying, not into new paths.
    if (size > 512 * 1024) return 0;
    const std::string html(reinterpret_cast<const char*>(data), size);

    Parser parser;
    Document doc = parser.Parse(html);
    (void)doc.CountElements();
    (void)doc.Head();
    (void)doc.Body();
    (void)doc.GetElementById("main");

    StyleResolver resolver;
    resolver.SetMediaWidth(size % 2 ? 360.f : 1024.f);
    for (const auto& css : doc.styleSheets) resolver.AddStyleSheet(css);
    resolver.Resolve(doc);
    if (doc.root) {
        float sink = 0.f;
        doc.root->ForEachElement([&](Node& node) {
            const ComputedStyle& style = resolver.StyleOf(&node);
            sink += style.fontSizePx + static_cast<float>(style.gridColumns.tracks.size());
            return true;
        });
        (void)sink;
        (void)doc.root->TextContent();
    }

    (void)DecodeEntities(html);
    (void)ExtractPlainText(html);
    return 0;
}
