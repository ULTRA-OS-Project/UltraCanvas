// Tests/HTMLReaderTest.cpp
// Unit tests for the HTMLReader module (parser, CSS subset, style resolver).
// Framework-independent: builds against the HTMLReader sources only.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLParser.h"
#include "HTMLReader/CSSStyleSheet.h"
#include "HTMLReader/HTMLStyleResolver.h"

#include <cstdio>
#include <cmath>
#include <string>

using namespace UltraCanvas::HTML;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    ++checks; \
    auto va = (a); auto vb = (b); \
    if (!(va == vb)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
    } \
} while (0)

static bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

// ============================================================================
// PARSER
// ============================================================================

static void TestParserBasics() {
    Parser parser;
    Document doc = parser.Parse(
        "<!DOCTYPE html><html><head><title>My &amp; Book</title>"
        "<style>p { color: red; }</style>"
        "<meta name=\"author\" content=\"Jane\"/></head>"
        "<body><h1 id=\"top\">Title</h1><p class=\"first big\">Hello "
        "<b>bold</b> world</p></body></html>");

    CHECK(doc.root != nullptr);
    CHECK_EQ(doc.title, std::string("My & Book"));
    CHECK_EQ(doc.styleSheets.size(), size_t(1));
    CHECK_EQ(doc.meta["author"], std::string("Jane"));
    CHECK(doc.Body() != nullptr);
    CHECK(doc.Head() != nullptr);

    Node* h1 = doc.GetElementById("top");
    CHECK(h1 != nullptr);
    if (h1) CHECK_EQ(h1->TextContent(), std::string("Title"));

    Node* p = doc.root->FindFirst("p");
    CHECK(p != nullptr);
    if (p) {
        CHECK(p->HasClass("first"));
        CHECK(p->HasClass("big"));
        CHECK(!p->HasClass("nope"));
        CHECK_EQ(p->TextContent(), std::string("Hello bold world"));
        Node* b = p->FindFirst("b");
        CHECK(b != nullptr);
        if (b) CHECK_EQ(b->TextContent(), std::string("bold"));
    }
}

static void TestParserFragmentAndRecovery() {
    Parser parser;
    // Fragment without html/body; unclosed <p>; stray </i>; void elements.
    Document doc = parser.Parse(
        "<p>First paragraph<p>Second</i> with <br>break"
        "<ul><li>one<li>two</ul>");

    Node* body = doc.Body();
    CHECK(body != nullptr);
    if (!body) return;

    int pCount = 0, liCount = 0, brCount = 0;
    doc.root->ForEachElement([&](Node& e) {
        if (e.tag == "p") ++pCount;
        if (e.tag == "li") ++liCount;
        if (e.tag == "br") ++brCount;
        return true;
    });
    CHECK_EQ(pCount, 2);      // implicit close of first <p>
    CHECK_EQ(liCount, 2);     // implicit close of first <li>
    CHECK_EQ(brCount, 1);

    // <li> must not be nested inside the previous <li>.
    Node* ul = doc.root->FindFirst("ul");
    CHECK(ul != nullptr);
    if (ul) {
        int directLi = 0;
        for (const auto& child : ul->children) {
            if (child->IsElement("li")) ++directLi;
        }
        CHECK_EQ(directLi, 2);
    }
}

static void TestParserXhtmlAndEntities() {
    Parser parser;
    Document doc = parser.Parse(
        "<?xml version=\"1.0\"?><html xmlns=\"http://www.w3.org/1999/xhtml\">"
        "<body><p>caf&#233; &mdash; &#x2019;quoted&#x2019; &copy;</p>"
        "<img src=\"pic.png\" width=\"100\" height=\"50\"/></body></html>");

    Node* p = doc.root->FindFirst("p");
    CHECK(p != nullptr);
    if (p) {
        CHECK_EQ(p->TextContent(),
                 std::string("caf\xC3\xA9 \xE2\x80\x94 \xE2\x80\x99quoted\xE2\x80\x99 \xC2\xA9"));
    }
    Node* img = doc.root->FindFirst("img");
    CHECK(img != nullptr);
    if (img) {
        CHECK_EQ(img->GetAttribute("src"), std::string("pic.png"));
        CHECK_EQ(img->GetAttribute("width"), std::string("100"));
    }
}

static void TestExtractPlainText() {
    std::string text = ExtractPlainText(
        "<html><head><style>p{color:red}</style></head>"
        "<body><h1>Head</h1><p>One &amp; two</p></body></html>");
    CHECK_EQ(text, std::string("Head One & two"));
}

// ============================================================================
// CSS VALUES
// ============================================================================

static void TestCssColor() {
    auto c1 = CssColor::Parse("#ff8000");
    CHECK(c1 && c1->r == 255 && c1->g == 128 && c1->b == 0 && c1->a == 255);

    auto c2 = CssColor::Parse("#abc");
    CHECK(c2 && c2->r == 0xAA && c2->g == 0xBB && c2->b == 0xCC);

    auto c3 = CssColor::Parse("rgb(10, 20, 30)");
    CHECK(c3 && c3->r == 10 && c3->g == 20 && c3->b == 30);

    auto c4 = CssColor::Parse("rgba(10, 20, 30, 0.5)");
    CHECK(c4 && c4->a > 120 && c4->a < 135);

    auto c5 = CssColor::Parse("DarkRed");
    CHECK(c5 && c5->r == 0x8B && c5->g == 0 && c5->b == 0);

    auto c6 = CssColor::Parse("transparent");
    CHECK(c6 && c6->a == 0);

    CHECK(!CssColor::Parse("notacolor"));
    CHECK(!CssColor::Parse("#12"));
}

static void TestCssLength() {
    auto l1 = CssLength::Parse("12px");
    CHECK(l1 && l1->unit == CssUnit::Px && Near(l1->value, 12));

    auto l2 = CssLength::Parse("1.5em");
    CHECK(l2 && l2->unit == CssUnit::Em && Near(l2->ToPx(16, 16), 24));

    auto l3 = CssLength::Parse("120%");
    CHECK(l3 && l3->unit == CssUnit::Percent && Near(l3->ToPx(0, 0, 200), 240));

    auto l4 = CssLength::Parse("12pt");
    CHECK(l4 && Near(l4->ToPx(16, 16), 16));   // 12pt = 16px at 96dpi

    auto l5 = CssLength::Parse("auto");
    CHECK(l5 && l5->unit == CssUnit::Auto);

    CHECK(!CssLength::Parse("garbage"));
}

// ============================================================================
// STYLESHEET PARSING
// ============================================================================

static void TestStyleSheetParsing() {
    StyleSheet sheet;
    sheet.ParseAppend(
        "/* comment */\n"
        "p, h1.title { margin: 1em 2em; color: #333; }\n"
        "@media print { p { display: none; } }\n"
        ".a .b > .c { font-weight: bold !important; }\n"
        "div:hover { color: red; }  /* dropped: pseudo-class */\n"
        "#main { padding-left: 10px }");

    CHECK_EQ(sheet.rules.size(), size_t(3));   // @media skipped, :hover dropped

    if (sheet.rules.size() >= 3) {
        CHECK_EQ(sheet.rules[0].selectors.size(), size_t(2));
        CHECK_EQ(sheet.rules[0].declarations.size(), size_t(2));
        CHECK_EQ(sheet.rules[0].selectors[0].Specificity(), 1);        // p
        CHECK_EQ(sheet.rules[0].selectors[1].Specificity(), 101);      // h1.title

        CHECK_EQ(sheet.rules[1].selectors[0].path.size(), size_t(3));  // .a .b .c
        CHECK(sheet.rules[1].declarations[0].important);

        CHECK_EQ(sheet.rules[2].selectors[0].Specificity(), 10000);    // #main
    }

    auto decls = StyleSheet::ParseDeclarationList("color: blue; ; margin-top:2px");
    CHECK_EQ(decls.size(), size_t(2));
}

// ============================================================================
// STYLE RESOLUTION
// ============================================================================

static void TestStyleResolution() {
    Parser parser;
    Document doc = parser.Parse(
        "<html><body>"
        "<div class=\"chapter\">"
        "  <h2>Heading</h2>"
        "  <p class=\"first\" style=\"margin-top: 0\">Text <b>bold</b></p>"
        "  <p>Plain</p>"
        "  <pre>  code  </pre>"
        "  <a href=\"ch2.xhtml\">next</a>"
        "</div></body></html>");

    StyleResolver resolver;
    resolver.AddStyleSheet(
        "p { color: #112233; margin-top: 8px; }"
        ".chapter p.first { font-size: 20px; }"
        "b { color: red !important; }");

    ResolverOptions options;
    options.baseFontSizePx = 16.f;
    resolver.Resolve(doc, options);

    Node* h2 = doc.root->FindFirst("h2");
    CHECK(h2 != nullptr);
    if (h2) {
        const ComputedStyle& s = resolver.StyleOf(h2);
        CHECK(s.display == DisplayMode::Block);
        CHECK(s.bold);
        CHECK(Near(s.fontSizePx, 16 * 1.5f));
    }

    // Descendant selector + inline style override.
    Node* firstP = nullptr;
    Node* secondP = nullptr;
    doc.root->ForEachElement([&](Node& e) {
        if (e.tag == "p") {
            if (!firstP) firstP = &e;
            else if (!secondP) secondP = &e;
        }
        return true;
    });
    CHECK(firstP && secondP);
    if (firstP) {
        const ComputedStyle& s = resolver.StyleOf(firstP);
        CHECK(Near(s.fontSizePx, 20));                 // .chapter p.first matched
        CHECK(Near(s.marginTop, 0));                   // inline beats stylesheet
        CHECK(s.color.r == 0x11 && s.color.g == 0x22); // stylesheet color
    }
    if (secondP) {
        const ComputedStyle& s = resolver.StyleOf(secondP);
        CHECK(Near(s.fontSizePx, 16));
        CHECK(Near(s.marginTop, 8));                   // stylesheet beats UA 1em
    }

    // Inheritance into <b>, plus !important color.
    Node* b = doc.root->FindFirst("b");
    CHECK(b != nullptr);
    if (b) {
        const ComputedStyle& s = resolver.StyleOf(b);
        CHECK(s.bold);
        CHECK(Near(s.fontSizePx, 20));                 // inherited from p.first
        CHECK(s.color.r == 255 && s.color.g == 0);     // !important
        CHECK(s.display == DisplayMode::Inline);
    }

    Node* pre = doc.root->FindFirst("pre");
    CHECK(pre != nullptr);
    if (pre) {
        const ComputedStyle& s = resolver.StyleOf(pre);
        CHECK(s.monospace);
        CHECK(s.preserveWhitespace);
    }

    Node* a = doc.root->FindFirst("a");
    CHECK(a != nullptr);
    if (a) {
        const ComputedStyle& s = resolver.StyleOf(a);
        CHECK(s.isLink);
        CHECK_EQ(s.href, std::string("ch2.xhtml"));
        CHECK(s.underline);
    }
}

static void TestReadingModeOverride() {
    Parser parser;
    Document doc = parser.Parse(
        "<body><p style=\"color: black; background-color: white\">night</p></body>");

    StyleResolver resolver;
    ResolverOptions options;
    options.overrideAuthorColors = true;
    options.textColor = CssColor{200, 200, 200, 255};
    resolver.Resolve(doc, options);

    Node* p = doc.root->FindFirst("p");
    CHECK(p != nullptr);
    if (p) {
        const ComputedStyle& s = resolver.StyleOf(p);
        CHECK(s.color.r == 200);              // author color ignored
        CHECK(!s.backgroundColor.has_value());
    }
}

// ============================================================================

int main() {
    TestParserBasics();
    TestParserFragmentAndRecovery();
    TestParserXhtmlAndEntities();
    TestExtractPlainText();
    TestCssColor();
    TestCssLength();
    TestStyleSheetParsing();
    TestStyleResolution();
    TestReadingModeOverride();

    std::printf("%s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
