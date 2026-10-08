// Tests/Fuzz/CSSFuzz.cpp
// Fuzz target: the HTML reader's CSS on any bytes - the style sheet parser
// (rules, selectors, @media, comments, !important), the declaration list
// of a style="" attribute, colours and lengths, and the cascade applying
// every declaration to a small fixed document. Mail and web pages ship their
// own CSS; a stylesheet must never crash or hang the reader.
//
// Built as a libFuzzer target (ULTRACANVAS_BUILD_FUZZERS, clang) and as a
// deterministic smoke run in ctest (FuzzSmokeMain.cpp). See Tests/Fuzz/README.md.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "HTMLReader/CSSStyleSheet.h"
#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "HTMLReader/HTMLStyleResolver.h"

#include <cstddef>
#include <cstdint>
#include <string>

using namespace UltraCanvas::HTML;

namespace {

// Elements of the kinds the resolver treats specially (tables, lists, links,
// images, form controls, a grid with items), so the selectors in the input
// have something to match and every declaration lands on some element.
const char* const kDocument =
    "<!DOCTYPE html><html><head><title>t</title></head>"
    "<body class='a b' id='top'><div id='main' class='grid' style='display:grid'>"
    "<p class='x'>one <b>two</b> <a href='#'>three</a></p>"
    "<ul><li>i</li><li class='odd'>ii</li></ul>"
    "<table cellpadding='2'><tr><td width='50%'>c</td><th>h</th></tr></table>"
    "<img src='x.png' alt='x' width='10'><span lang='en' data-x='1'>s</span>"
    "<input type='text' value='v'><section><h2>h</h2><pre> p </pre></section>"
    "</div></body></html>";

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 256 * 1024) return 0;
    const std::string css(reinterpret_cast<const char*>(data), size);

    StyleSheet sheet;
    sheet.SetMediaWidth(size % 2 ? 320.f : 1280.f);
    sheet.ParseAppend(css);
    (void)StyleSheet::MediaMatches(css, 800.f);

    // The same bytes as a style="" attribute and as single values.
    for (const auto& decl : StyleSheet::ParseDeclarationList(css)) {
        (void)CssColor::Parse(decl.value);
        (void)CssLength::Parse(decl.value);
    }
    (void)CssColor::Parse(css);
    (void)CssLength::Parse(css);

    // The cascade: the bytes as the document's style sheet, and again as the
    // inline style of an element, so every property handler sees them.
    Parser parser;
    Document doc = parser.Parse(kDocument);
    if (Node* main = doc.GetElementById("main")) main->SetAttribute("style", css);
    StyleResolver resolver;
    resolver.SetMediaWidth(800.f);
    resolver.AddStyleSheet(css);
    resolver.Resolve(doc);
    if (doc.root) {
        float sink = 0.f;
        doc.root->ForEachElement([&](Node& node) {
            sink += resolver.StyleOf(&node).fontSizePx;
            return true;
        });
        (void)sink;
    }
    return 0;
}
