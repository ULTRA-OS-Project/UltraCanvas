// Tests/HTMLReaderTest.cpp
// Unit tests for the HTMLReader module (parser, CSS subset, style resolver).
// Framework-independent: builds against the HTMLReader sources only.
// Version: 1.11.0 - max-width in percent
// Version: 1.10.0 - min-width, min-height, max-height
// Version: 1.9.0 - box-sizing
// Version: 1.8.0 - borders per side
// Version: 1.7.0 - border-radius %, <img border>, border currentColor
// Version: 1.6.0 - object-fit, object-position
// Version: 1.5.0 - background-repeat
// Version: 1.4.0 - background-position
// Version: 1.3.0 - @media, <style media>, background layers, margin: auto
// Version: 1.2.0 - every HTML 4 entity; mail table attributes; a:link
// Version: 1.1.0 - CSS number shapes (exponents, leading dot, sign)
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework

#include "HTMLReader/HTMLParser.h"
#include "HTMLReader/CSSStyleSheet.h"
#include "HTMLReader/HTMLStyleResolver.h"

#include <clocale>
#include <cstdio>
#include <cmath>
#include <functional>
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

static void TestParserUnquotedAttributes() {
    Parser parser;
    // Real-world email HTML: unquoted attribute values, a URL with "://" and
    // path slashes, inside <a><img></a>. The URL must be captured whole, and
    // nothing from the tag may leak out as a text node.
    Document doc = parser.Parse(
        "<body><a href=#><img style=\"display:block\" "
        "src=https://www.gstatic.com/gumdrop/files/logo-w646px-h128px-2x.png "
        "width=162 height=auto border=0 alt=\"\"></a></body>");

    Node* img = doc.root->FindFirst("img");
    CHECK(img != nullptr);
    if (img) {
        CHECK_EQ(img->GetAttribute("src"),
                 std::string("https://www.gstatic.com/gumdrop/files/logo-w646px-h128px-2x.png"));
        CHECK_EQ(img->GetAttribute("width"), std::string("162"));
        CHECK_EQ(img->GetAttribute("height"), std::string("auto"));
        CHECK_EQ(img->GetAttribute("border"), std::string("0"));
    }
    // Nothing from the <img> tag leaks as visible text (previously the URL and
    // trailing attributes rendered as a text run).
    CHECK_EQ(ExtractPlainText(
        "<body><a href=#><img src=https://www.gstatic.com/x/y.png width=162 "
        "height=auto border=0 alt=\"\"></a></body>"),
        std::string(""));

    // An unquoted href keeps ':' '/' and '=' (query strings): value ends only
    // at whitespace or '>'.
    Document doc2 = parser.Parse("<a href=https://c.gle/x?a=b>here</a>");
    Node* a = doc2.root->FindFirst("a");
    CHECK(a != nullptr);
    if (a) {
        CHECK_EQ(a->GetAttribute("href"), std::string("https://c.gle/x?a=b"));
        CHECK_EQ(a->TextContent(), std::string("here"));
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

    // The number shapes ParseFloatClassic() in CSSStyleSheet.cpp has to get
    // right: it delimits the number itself rather than calling
    // std::from_chars, and an 'e' that turns out to be a unit rather than an
    // exponent is where that goes wrong.
    auto l6 = CssLength::Parse(".5em");
    CHECK(l6 && l6->unit == CssUnit::Em && Near(l6->value, 0.5f));

    auto l7 = CssLength::Parse("1.5e2px");
    CHECK(l7 && l7->unit == CssUnit::Px && Near(l7->value, 150));

    auto l8 = CssLength::Parse("-3px");
    CHECK(l8 && l8->unit == CssUnit::Px && Near(l8->value, -3));

    auto l9 = CssLength::Parse("1em");
    CHECK(l9 && l9->unit == CssUnit::Em && Near(l9->value, 1));

    CHECK(!CssLength::Parse("1e"));   // exponent without digits
    CHECK(!CssLength::Parse("px"));

    // Out of range for a float: the number is consumed but the value is left
    // as the caller set it, which is what std::from_chars reported here
    // before ParseFloatClassic replaced it.
    auto l10 = CssLength::Parse("1e999px");
    CHECK(l10 && l10->unit == CssUnit::Px && Near(l10->value, 0));
}

// A CSS number is written with a '.' whatever the user's locale is, so parsing
// one must not depend on LC_NUMERIC. It did once: strtof reads the locale's
// decimal point, so under a comma-decimal locale "0.5" stopped at the '.' and
// every fractional length and rgba() alpha silently became 0. The fix must also
// survive libc++, which does not implement the float overloads of
// std::from_chars - so neither primitive can simply be used on its own.
//
// Skipped where the C library has no comma-decimal locale installed (a bare CI
// image often has only "C"), rather than failing for a missing locale.
static void TestCssNumbersIgnoreLocale() {
    const char* original = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = original ? original : "C";

    const char* commaLocale = nullptr;
    for (const char* candidate : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "de_DE"}) {
        if (std::setlocale(LC_NUMERIC, candidate)) { commaLocale = candidate; break; }
    }
    if (!commaLocale) {
        std::printf("SKIP TestCssNumbersIgnoreLocale: no comma-decimal locale installed\n");
        return;
    }

    auto frac = CssLength::Parse("0.5em");
    CHECK(frac && frac->unit == CssUnit::Em && Near(frac->value, 0.5f));

    auto negative = CssLength::Parse("-3.25%");
    CHECK(negative && negative->unit == CssUnit::Percent && Near(negative->value, -3.25f));

    auto exponent = CssLength::Parse("1.5e2px");
    CHECK(exponent && exponent->unit == CssUnit::Px && Near(exponent->value, 150.f));

    auto alpha = CssColor::Parse("rgba(10, 20, 30, 0.5)");
    CHECK(alpha && alpha->a == 127);   // 0.5 * 255, not 0

    auto percent = CssColor::Parse("rgb(100%, 0%, 50%)");
    CHECK(percent && percent->r == 255 && percent->g == 0 && percent->b == 127);

    std::setlocale(LC_NUMERIC, saved.c_str());
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

// The whole HTML 4 entity set: mail writes feet-and-inches as 5&acute;8&quot;.
static void TestHtml4Entities() {
    Parser parser;
    Document doc = parser.Parse(
        "<p>5&acute;8&quot; &eth;&thorn; &alpha;&Omega; &larr;&hearts; &frac12;&sup2; &bogus;</p>");
    Node* p = doc.root->FindFirst("p");
    CHECK(p != nullptr);
    if (p) {
        CHECK_EQ(p->TextContent(),
                 std::string("5\xC2\xB4" "8\" \xC3\xB0\xC3\xBE \xCE\xB1\xCE\xA9 "
                             "\xE2\x86\x90\xE2\x99\xA5 \xC2\xBD\xC2\xB2 &bogus;"));
    }
}

// The presentational table attributes of mail HTML, <nobr>, and a:link.
static void TestMailTableStyles() {
    Parser parser;
    Document doc = parser.Parse(
        "<html><body><table cellpadding='0' cellspacing='4'>"
        "<tr valign='top' align='center'><td id='c1' nowrap>a</td>"
        "<td id='c2' valign='bottom' style='white-space:nowrap'>b</td></tr></table>"
        "<table style='border-collapse:collapse'><tr><td id='c3'>c</td></tr></table>"
        "<p><nobr id='n'>x</nobr><a id='link' href='h'>l</a><a id='plain'>m</a></p>"
        "<div style='color:#7b7974'><a id='inh' href='h' style='color: inherit'>i</a></div>"
        "</body></html>");
    StyleResolver resolver;
    resolver.AddStyleSheet("a:link { color: #3333aa } a:hover { color: #ff0000 }");
    ResolverOptions options;
    options.baseFontSizePx = 12.f;
    resolver.Resolve(doc, options);

    std::function<Node*(Node*, const std::string&)> find = [&](Node* n, const std::string& id) -> Node* {
        if (n->IsElement() && n->GetAttribute("id") == id) return n;
        for (auto& c : n->children)
            if (Node* hit = find(c.get(), id)) return hit;
        return nullptr;
    };
    auto byId = [&](const char* id) { return find(doc.root.get(), id); };
    Node* table = doc.root->FindFirst("table");
    Node* c1 = byId("c1");
    Node* c2 = byId("c2");
    Node* c3 = byId("c3");
    CHECK(table && c1 && c2 && c3);
    if (!table || !c1 || !c2 || !c3) return;
    const ComputedStyle& t = resolver.StyleOf(table);
    CHECK(t.borderSpacing.has_value() && *t.borderSpacing == 4.f);
    const ComputedStyle& s1 = resolver.StyleOf(c1);
    CHECK(s1.noWrap);
    CHECK_EQ(s1.paddingLeft, 0.f);                 // cellpadding
    CHECK(s1.textAlign == TextAlignMode::Center);  // <tr align>
    const ComputedStyle& s2 = resolver.StyleOf(c2);
    CHECK(s2.noWrap);
    CHECK(s2.verticalAlign == VerticalAlignMode::Bottom);
    CHECK(resolver.StyleOf(c1->parent).verticalAlign == VerticalAlignMode::Top);
    const ComputedStyle& s3 = resolver.StyleOf(c3);
    CHECK_EQ(s3.paddingTop, 1.f);                  // a browser's default cell padding
    CHECK(s3.borderCollapse);
    if (Node* n = byId("n")) CHECK(resolver.StyleOf(n).noWrap);
    Node* link = byId("link");
    Node* plain = byId("plain");
    CHECK(link && plain);
    if (link && plain) {
        CHECK(resolver.StyleOf(link).color.r == 0x33 && resolver.StyleOf(link).color.b == 0xAA);
        CHECK(resolver.StyleOf(plain).color.r != 0x33);   // no href: not a :link
    }
    if (Node* inh = byId("inh")) {
        CHECK(resolver.StyleOf(inh).color.r == 0x7b && resolver.StyleOf(inh).color.b == 0x74);
    } else {
        CHECK(false);
    }
}

// @media answered for a width; <style media>; background layers; margin:auto.
static void TestMediaAndBackgrounds() {
    CHECK(StyleSheet::MediaMatches("only screen and (min-width:480px)", 720.f));
    CHECK(!StyleSheet::MediaMatches("only screen and (min-width:480px)", 400.f));
    CHECK(StyleSheet::MediaMatches("only screen and (max-width:479px)", 400.f));
    CHECK(StyleSheet::MediaMatches("print, screen and (max-width: 30em)", 480.f));
    CHECK(!StyleSheet::MediaMatches("print", 720.f));
    CHECK(StyleSheet::MediaMatches("not print", 720.f));
    CHECK(!StyleSheet::MediaMatches("(-webkit-min-device-pixel-ratio: 2)", 720.f));

    const char* html =
        "<html><head><style>@media only screen and (min-width:480px) { .c { width:65% !important; } }"
        "</style><style media='screen and (max-width:479px)'>.d { color: #ff0000 }</style></head>"
        "<body><div id='c' class='c d' style='width:100%;margin:0 auto;max-width:640px;"
        "background:url(\"wave.gif\") center / contain no-repeat, url(poster.png) #fafafa;"
        "border-radius:20px'>x</div></body></html>";
    auto styleAt = [&](float width, auto check) {
        Parser parser;
        Document doc = parser.Parse(html);
        StyleResolver resolver;
        resolver.SetMediaWidth(width);
        for (const auto& css : doc.styleSheets) resolver.AddStyleSheet(css);
        resolver.Resolve(doc, ResolverOptions{});
        Node* div = doc.root->FindFirst("div");
        CHECK(div != nullptr);
        if (div) check(resolver.StyleOf(div));
    };
    styleAt(720.f, [](const ComputedStyle& st) {
        CHECK(st.widthPercent && *st.widthPercent == 65.f);   // the wide-screen rule
        CHECK(st.color.r == 0);                               // the narrow sheet is off
        CHECK(st.marginLeftAuto && st.marginRightAuto);
        CHECK(st.maxWidthPx && *st.maxWidthPx == 640.f);
        CHECK(st.backgroundImages.size() == 2);
        if (st.backgroundImages.size() == 2) {
            CHECK_EQ(st.backgroundImages[0], std::string("wave.gif"));
            CHECK_EQ(st.backgroundImages[1], std::string("poster.png"));
        }
        CHECK(st.BackgroundSizeAt(0) == BackgroundSizeMode::Contain);
        CHECK(st.BackgroundSizeAt(1) == BackgroundSizeMode::Auto);      // poster: no size
        CHECK(st.BackgroundPositionAt(0).x.value == 0.5f && st.BackgroundPositionAt(0).y.value == 0.5f);
        CHECK(st.BackgroundPositionAt(1).x.value == 0.f && st.BackgroundPositionAt(1).y.value == 0.f);
        CHECK(st.backgroundColor && st.backgroundColor->r == 0xfa);
        CHECK_EQ(st.borderRadius, 20.f);
    });
    styleAt(400.f, [](const ComputedStyle& st) {
        CHECK(st.widthPercent && *st.widthPercent == 100.f);
        CHECK(st.color.r == 0xff);
    });
}

// background-position in its value forms, longhand and shorthand.
static void TestBackgroundPosition() {
    auto positionOf = [](const std::string& css) {
        Parser parser;
        Document doc = parser.Parse("<div style=\"" + css + "\">x</div>");
        StyleResolver resolver;
        ResolverOptions options;
        options.baseFontSizePx = 16.f;
        resolver.Resolve(doc, options);
        Node* div = doc.root->FindFirst("div");
        return div ? resolver.StyleOf(div).BackgroundPositionAt(0) : BackgroundPosition{};
    };
    auto isFraction = [](const BackgroundAxisPosition& a, float f) {
        return !a.pixels && std::fabs(a.value - f) < 0.001f;
    };
    auto isPixels = [](const BackgroundAxisPosition& a, float px, bool fromEnd) {
        return a.pixels && a.fromEnd == fromEnd && std::fabs(a.value - px) < 0.001f;
    };
    BackgroundPosition p = positionOf("background-position: right bottom");
    CHECK(isFraction(p.x, 1.f) && isFraction(p.y, 1.f));
    p = positionOf("background-position: top");             // x centred
    CHECK(isFraction(p.x, 0.5f) && isFraction(p.y, 0.f));
    p = positionOf("background-position: top left");        // vertical word first
    CHECK(isFraction(p.x, 0.f) && isFraction(p.y, 0.f));
    p = positionOf("background-position: 25% 75%");
    CHECK(isFraction(p.x, 0.25f) && isFraction(p.y, 0.75f));
    p = positionOf("background-position: 10px 2em");
    CHECK(isPixels(p.x, 10.f, false) && isPixels(p.y, 32.f, false));
    p = positionOf("background-position: 30px");            // y centred
    CHECK(isPixels(p.x, 30.f, false) && isFraction(p.y, 0.5f));
    p = positionOf("background-position: right 10px bottom 20%");
    CHECK(isPixels(p.x, 10.f, true) && isFraction(p.y, 0.8f));
    p = positionOf("background: #fff url(a.png) no-repeat right 5px top / cover");
    CHECK(isPixels(p.x, 5.f, true) && isFraction(p.y, 0.f));
    p = positionOf("background: url(a.png)");                // CSS initial value
    CHECK(isFraction(p.x, 0.f) && isFraction(p.y, 0.f));
}

// background-repeat: longhand, shorthand, lists per layer, the initial value.
static void TestBackgroundRepeat() {
    auto repeatOf = [](const std::string& css, size_t layer = 0) {
        Parser parser;
        Document doc = parser.Parse("<div style=\"" + css + "\">x</div>");
        StyleResolver resolver;
        resolver.Resolve(doc, ResolverOptions{});
        Node* div = doc.root->FindFirst("div");
        return div ? resolver.StyleOf(div).BackgroundRepeatAt(layer) : BackgroundRepeat{};
    };
    BackgroundRepeat r = repeatOf("background: url(a.png)");
    CHECK(r.x && r.y);                                   // CSS initial: repeat
    r = repeatOf("background: url(a.png) no-repeat center");
    CHECK(!r.x && !r.y);
    r = repeatOf("background: #fff url(a.png) repeat-x top");
    CHECK(r.x && !r.y);
    r = repeatOf("background-image: url(a.png); background-repeat: repeat-y");
    CHECK(!r.x && r.y);
    r = repeatOf("background-image: url(a.png); background-repeat: repeat no-repeat");
    CHECK(r.x && !r.y);
    r = repeatOf("background-image: url(a.png); background-repeat: space");
    CHECK(r.x && r.y);                                   // space: taken as repeat
    r = repeatOf("background: url(a.png) no-repeat, url(b.png) repeat-x", 1);
    CHECK(r.x && !r.y);                                  // the second layer's own
}

// object-fit and object-position on an <img>, with their initial values.
static void TestObjectFitPosition() {
    auto styleOf = [](const std::string& css) {
        Parser parser;
        Document doc = parser.Parse("<img src=\"a.png\" style=\"" + css + "\">");
        StyleResolver resolver;
        ResolverOptions options;
        options.baseFontSizePx = 16.f;
        resolver.Resolve(doc, options);
        Node* img = doc.root->FindFirst("img");
        return img ? resolver.StyleOf(img) : ComputedStyle{};
    };
    auto isFraction = [](const BackgroundAxisPosition& a, float f) {
        return !a.pixels && std::fabs(a.value - f) < 0.001f;
    };
    ComputedStyle st = styleOf("");
    CHECK(st.objectFit == ObjectFitMode::Fill);          // CSS initial: fill
    CHECK(isFraction(st.objectPosition.x, 0.5f) && isFraction(st.objectPosition.y, 0.5f));
    CHECK(styleOf("object-fit: contain").objectFit == ObjectFitMode::Contain);
    CHECK(styleOf("object-fit: COVER").objectFit == ObjectFitMode::Cover);
    CHECK(styleOf("object-fit: none").objectFit == ObjectFitMode::NoScaling);
    CHECK(styleOf("object-fit: scale-down").objectFit == ObjectFitMode::ScaleDown);
    CHECK(styleOf("object-fit: bogus").objectFit == ObjectFitMode::Fill);
    st = styleOf("object-position: right top");
    CHECK(isFraction(st.objectPosition.x, 1.f) && isFraction(st.objectPosition.y, 0.f));
    st = styleOf("object-position: 25% 1em");
    CHECK(isFraction(st.objectPosition.x, 0.25f));
    CHECK(st.objectPosition.y.pixels && std::fabs(st.objectPosition.y.value - 16.f) < 0.001f);
    st = styleOf("object-position: right 10px bottom 5px");
    CHECK(st.objectPosition.x.pixels && st.objectPosition.x.fromEnd);
    CHECK(st.objectPosition.y.pixels && st.objectPosition.y.fromEnd);
}

// border-radius in percent, <img border="N"> and a border shorthand without
// a colour (the text colour).
static void TestImageBorders() {
    auto styleOf = [](const std::string& html, const char* tag) {
        Parser parser;
        Document doc = parser.Parse(html);
        StyleResolver resolver;
        resolver.Resolve(doc, ResolverOptions{});
        Node* n = doc.root->FindFirst(tag);
        return n ? resolver.StyleOf(n) : ComputedStyle{};
    };
    ComputedStyle st = styleOf("<img src=a.png style=\"border-radius:50%\">", "img");
    CHECK(st.borderRadiusPercent == 50.f && st.borderRadius == 0.f);
    st = styleOf("<img src=a.png style=\"border-radius:50%;border-radius:4px\">", "img");
    CHECK(st.borderRadiusPercent == 0.f && st.borderRadius == 4.f);
    st = styleOf("<img src=a.png border=\"2\" style=\"color:#ff0000\">", "img");
    CHECK(st.borderTop.Width() == 2.f && st.borderLeft.Width() == 2.f);
    st = styleOf("<font color=\"#00ff00\"><img src=a.png border=\"3\"></font>", "img");
    CHECK(st.borderTop.Width() == 3.f && st.borderTop.color.g == 0xff && st.borderTop.color.r == 0);
    st = styleOf("<img src=a.png border=\"0\">", "img");
    CHECK(!st.HasBorder());
    st = styleOf("<div style=\"color:#0000ff;border:1px solid\">x</div>", "div");
    CHECK(st.borderTop.Width() == 1.f && st.borderTop.color.b == 0xff && st.borderTop.color.r == 0);
    st = styleOf("<div style=\"color:#0000ff;border:1px solid #ff0000\">x</div>", "div");
    CHECK(st.borderRight.color.r == 0xff && st.borderRight.color.b == 0);
}

// Borders per side: the shorthands, the 1-4 value lists, the longhands, and
// no border without a style.
static void TestBorderSides() {
    auto styleOf = [](const std::string& css) {
        Parser parser;
        Document doc = parser.Parse("<div style=\"color:#0000ff;" + css + "\">x</div>");
        StyleResolver resolver;
        ResolverOptions options;
        options.baseFontSizePx = 16.f;
        resolver.Resolve(doc, options);
        Node* n = doc.root->FindFirst("div");
        return n ? resolver.StyleOf(n) : ComputedStyle{};
    };
    ComputedStyle st = styleOf("border-bottom:1px solid #eeeeee");
    CHECK(st.borderBottom.Width() == 1.f && st.borderBottom.color.r == 0xee);
    CHECK(st.borderTop.Width() == 0.f && st.borderLeft.Width() == 0.f && st.borderRight.Width() == 0.f);
    CHECK(!st.UniformBorder());
    st = styleOf("border:2px solid red;border-left:4px dashed #00ff00");
    CHECK(st.borderTop.Width() == 2.f && st.borderTop.color.r == 0xff);
    CHECK(st.borderLeft.Width() == 4.f && st.borderLeft.style == BorderLineStyle::Dashed);
    CHECK(st.borderLeft.color.g == 0xff);
    st = styleOf("border-width:1px 2px 3px 4px;border-style:solid");
    CHECK(st.borderTop.Width() == 1.f && st.borderRight.Width() == 2.f);
    CHECK(st.borderBottom.Width() == 3.f && st.borderLeft.Width() == 4.f);
    CHECK(st.borderTop.color.b == 0xff);                  // currentColor
    st = styleOf("border-width:1px 2px;border-style:solid dotted;border-color:red green");
    CHECK(st.borderBottom.Width() == 1.f && st.borderLeft.Width() == 2.f);
    CHECK(st.borderRight.style == BorderLineStyle::Dotted && st.borderBottom.style == BorderLineStyle::Solid);
    CHECK(st.borderLeft.color.r == 0 && st.borderLeft.color.g > 0 && st.borderTop.color.r == 0xff);
    st = styleOf("border-top-width:5px;border-top-style:solid;border-top-color:#ff0000");
    CHECK(st.borderTop.Width() == 5.f && st.borderTop.color.r == 0xff && st.borderBottom.Width() == 0.f);
    st = styleOf("border:1px #cccccc");                     // no style: no border
    CHECK(!st.HasBorder());
    st = styleOf("border-style:solid");                     // medium
    CHECK(st.borderTop.Width() == 3.f && st.UniformBorder());
    st = styleOf("border:1px solid #ccc;border-top:none");
    CHECK(st.borderTop.Width() == 0.f && st.borderBottom.Width() == 1.f);
    st = styleOf("border:0");
    CHECK(!st.HasBorder());
    st = styleOf("border:thin solid");
    CHECK(st.borderLeft.Width() == 1.f);
    CHECK(!st.borderBoxSizing);                             // content-box by default
    CHECK(styleOf("box-sizing:border-box").borderBoxSizing);
    CHECK(!styleOf("box-sizing:border-box;box-sizing:content-box").borderBoxSizing);
    st = styleOf("min-width:120px;min-height:2em;max-height:50px");
    CHECK(st.minWidthPx && *st.minWidthPx == 120.f);
    CHECK(st.minHeightPx && *st.minHeightPx == 32.f);
    CHECK(st.maxHeightPx && *st.maxHeightPx == 50.f);
    st = styleOf("max-height:50px;max-height:none;min-width:10px;min-width:auto;min-height:50%");
    CHECK(!st.maxHeightPx && !st.minWidthPx && !st.minHeightPx);
    st = styleOf("max-width:100%");
    CHECK(st.maxWidthPercent && *st.maxWidthPercent == 100.f && !st.maxWidthPx);
    st = styleOf("max-width:100%;max-width:300px");
    CHECK(!st.maxWidthPercent && st.maxWidthPx && *st.maxWidthPx == 300.f);
    st = styleOf("max-width:300px;max-width:none");
    CHECK(!st.maxWidthPercent && !st.maxWidthPx);
}

int main() {
    TestParserBasics();
    TestParserFragmentAndRecovery();
    TestParserXhtmlAndEntities();
    TestParserUnquotedAttributes();
    TestExtractPlainText();
    TestCssColor();
    TestCssLength();
    TestCssNumbersIgnoreLocale();
    TestStyleSheetParsing();
    TestStyleResolution();
    TestReadingModeOverride();
    TestHtml4Entities();
    TestMailTableStyles();
    TestMediaAndBackgrounds();
    TestBackgroundPosition();
    TestBackgroundRepeat();
    TestObjectFitPosition();
    TestImageBorders();
    TestBorderSides();

    std::printf("%s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
