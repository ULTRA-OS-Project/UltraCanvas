// Tests/HTMLRichImportTest.cpp
// HTML → UCRichDocument (HTMLRichDocumentImporter), the quote level every
// block can carry, and the way the serializers and the editing core treat
// it: what a mail client needs to reply to and forward an HTML message
// with its formatting - and the options a rich paste reads it with
// (UCRichDocument::FromHTML).
//
// Builds without the UI stack (the parser, the style resolver, the model and
// the editing core are all UI-free), so it needs no display.
// Version: 1.1.0 - dir="rtl", preAsCodeBlock, skipWordListLabels
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "HTMLReader/HTMLRichDocumentImporter.h"
#include "UltraCanvasRichDocumentEditor.h"
#include "UltraCanvasTextUtils.h"

#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        ++failures; \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    ++checks; \
    auto&& a_ = (actual); auto&& e_ = (expected); \
    if (!(a_ == e_)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #actual "\n" \
                  << "  expected: [" << e_ << "]\n  actual:   [" << a_ << "]\n"; \
        ++failures; \
    } \
} while (0)

// ===== HELPERS =====

static std::string Text(const RichDocBlock& block) {
    return UCRichDocumentEditor::RunsText(block.runs);
}

static bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The first bytes of a PNG of the given size: enough for the importer, which
// only reads the size, and for the model, which stores the bytes as they are.
static std::vector<uint8_t> PngHeader(uint32_t width, uint32_t height) {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
                                0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (uint32_t value : {width, height}) {
        png.push_back(static_cast<uint8_t>(value >> 24));
        png.push_back(static_cast<uint8_t>(value >> 16));
        png.push_back(static_cast<uint8_t>(value >> 8));
        png.push_back(static_cast<uint8_t>(value));
    }
    png.insert(png.end(), {8, 2, 0, 0, 0, 0, 0, 0, 0});
    return png;
}

static std::string DataUri(const std::vector<uint8_t>& bytes) {
    return "data:image/png;base64," + Base64Encode(bytes, false);
}

// ===== TESTS =====

static void TestParagraphsAndRuns() {
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<p>Hello <b>bold</b> <i>italic</i> <u>under</u> <s>gone</s></p><p>Two</p>");
    CHECK_EQ(doc.blocks.size(), size_t(2));
    CHECK_EQ(Text(doc.blocks[0]), std::string("Hello bold italic under gone"));
    CHECK_EQ(Text(doc.blocks[1]), std::string("Two"));
    bool bold = false, italic = false, underline = false, strike = false;
    for (const RichTextRun& run : doc.blocks[0].runs) {
        if (run.text == "bold") bold = run.bold;
        if (run.text == "italic") italic = run.italic;
        if (run.text == "under") underline = run.underline;
        if (run.text == "gone") strike = run.strikethrough;
    }
    CHECK(bold && italic && underline && strike);
    // <p> margins (1em = 16px) become the space between the paragraphs,
    // collapsed as CSS does: 12pt, not 24.
    CHECK_EQ(doc.blocks[0].spaceAfterPt, 12.0f);
    CHECK_EQ(doc.blocks[1].spaceBeforePt, 0.0f);

    // White space: collapsed within a line, kept between inline elements.
    doc = ImportHTMLToRichDocument("<div>  a \n\n  b <b>c</b> <i>d</i>  </div>");
    CHECK_EQ(Text(doc.blocks[0]), std::string("a b c d"));
}

static void TestLineBreaksAndDivs() {
    // How Gmail and most editors write text: a <div> per line, <div><br></div>
    // for an empty one.
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<div>one<br>two</div><div><br></div><div>three<br></div><div>four<br><br></div>");
    CHECK_EQ(doc.blocks.size(), size_t(4));
    CHECK_EQ(Text(doc.blocks[0]), std::string("one\ntwo"));
    CHECK_EQ(Text(doc.blocks[1]), std::string(""));
    CHECK_EQ(Text(doc.blocks[2]), std::string("three"));       // a trailing <br> draws nothing
    CHECK_EQ(Text(doc.blocks[3]), std::string("four\n"));      // the second one is an empty line
    CHECK_EQ(doc.blocks[0].spaceAfterPt, 0.0f);                 // divs have no margins
}

static void TestHeadingsAndAlignment() {
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<h2 style=\"text-align:center\">Title</h2><p align=\"right\">R</p><center>C</center>");
    CHECK_EQ(doc.blocks.size(), size_t(3));
    CHECK(doc.blocks[0].type == RichBlockType::Heading);
    CHECK_EQ(doc.blocks[0].headingLevel, 2);
    CHECK(doc.blocks[0].align == RichTextAlign::Center);
    CHECK_EQ(doc.blocks[0].runs[0].fontSizePt, 0.0f);          // the view's heading size
    CHECK(doc.blocks[1].align == RichTextAlign::Right);
    CHECK(doc.blocks[2].align == RichTextAlign::Center);
}

static void TestLists() {
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<ol start=\"3\"><li>three</li><li>four<ul><li>nested</li></ul></li></ol>"
        "<ol type=\"a\"><li><p>para</p><p>more</p></li></ol>");
    CHECK_EQ(doc.blocks.size(), size_t(4));
    CHECK(doc.blocks[0].type == RichBlockType::ListItem && doc.blocks[0].orderedList);
    CHECK_EQ(doc.blocks[0].listStartNumber, 3);
    CHECK_EQ(doc.blocks[1].listStartNumber, 0);                // counts on
    CHECK_EQ(Text(doc.blocks[1]), std::string("four"));
    CHECK(doc.blocks[2].type == RichBlockType::ListItem && !doc.blocks[2].orderedList);
    CHECK_EQ(doc.blocks[2].listLevel, 1);
    CHECK(doc.blocks[3].numberFormat == RichNumberFormat::LowerLetter);
    // Paragraphs inside one item stay one item, a line each.
    CHECK_EQ(Text(doc.blocks[3]), std::string("para\nmore"));
}

static void TestQuotes() {
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<p>reply</p><blockquote type=\"cite\"><p>quoted</p>"
        "<blockquote><p>older</p></blockquote><ul><li>item</li></ul></blockquote>");
    CHECK_EQ(doc.blocks.size(), size_t(4));
    CHECK_EQ(doc.blocks[0].quoteLevel, 0);
    CHECK_EQ(doc.blocks[1].quoteLevel, 1);
    CHECK_EQ(doc.blocks[2].quoteLevel, 2);
    CHECK_EQ(doc.blocks[3].quoteLevel, 1);
    CHECK(doc.blocks[3].type == RichBlockType::ListItem);      // a quoted list stays a list
    CHECK_EQ(doc.blocks[1].leftIndentPt, 0.0f);                // the bar replaces the margin

    // A reply puts the whole message one level in.
    HTMLRichImportOptions options;
    options.quoteLevel = 1;
    doc = ImportHTMLToRichDocument("<p>a</p><blockquote>b</blockquote>", options);
    CHECK_EQ(doc.blocks[0].quoteLevel, 1);
    CHECK_EQ(doc.blocks[1].quoteLevel, 2);
}

static void TestCharacterStyles() {
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<style>.warn { color: #cc0000; }</style>"
        "<p><font color=\"#0000ff\" face=\"Arial, sans-serif\" size=\"5\">big</font> "
        "<span style=\"background-color: yellow\">marked</span> "
        "<span class=\"warn\">warn</span> <span style=\"font-size:10pt\">small</span> "
        "H<sub>2</sub>O <code>x</code> <a href=\"https://example.com/\">link</a> "
        "<a href=\"javascript:alert(1)\">bad</a></p>");
    const auto& runs = doc.blocks.at(0).runs;
    auto find = [&](const std::string& text) -> const RichTextRun* {
        for (const auto& run : runs) if (run.text == text) return &run;
        return nullptr;
    };
    const RichTextRun* big = find("big");
    CHECK(big && big->color == "#0000FF" && big->fontFamily == "Arial" && big->fontSizePt == 18.0f);
    const RichTextRun* marked = find("marked");
    CHECK(marked && marked->highlightColor == "#FFFF00");
    const RichTextRun* warn = find("warn");
    CHECK(warn && warn->color == "#CC0000");
    const RichTextRun* small = find("small");
    CHECK(small && small->fontSizePt == 10.0f);
    const RichTextRun* two = find("2");
    CHECK(two && two->subscript && two->fontSizePt == 0.0f);
    const RichTextRun* code = find("x");
    CHECK(code && code->code);
    const RichTextRun* link = find("link");
    CHECK(link && link->linkTarget == "https://example.com/" && link->color.empty() && !link->underline);
    const RichTextRun* bad = find("bad");
    CHECK(bad && bad->linkTarget.empty());
}

static void TestImages() {
    const std::vector<uint8_t> logo = PngHeader(200, 100);
    // Standalone: a picture paragraph, sized as stated, centred by its block.
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<p style=\"text-align:center\"><img src=\"" + DataUri(logo) + "\" width=\"100\" alt=\"Logo\"></p>");
    CHECK_EQ(doc.blocks.size(), size_t(1));
    CHECK(doc.blocks[0].type == RichBlockType::Image);
    CHECK(doc.blocks[0].align == RichTextAlign::Center);
    CHECK_EQ(doc.blocks[0].imageWidthPt, 75.0f);
    CHECK_EQ(doc.blocks[0].imageHeightPt, 37.5f);               // half the width, as the picture is
    CHECK_EQ(doc.media.size(), size_t(1));
    CHECK_EQ(doc.media[0].mimeType, std::string("image/png"));

    // In a line of text: an inline picture run; cid: through the resolver.
    HTMLRichImportOptions options;
    int asked = 0;
    options.resolveImage = [&](const std::string& src, HTMLRichImportImage& out) {
        ++asked;
        if (src != "cid:icon@mail") return false;
        out.data = PngHeader(16, 16);
        return true;
    };
    doc = ImportHTMLToRichDocument(
        "<p>Look <img src=\"cid:icon@mail\"> here <img src=\"https://tracker/x.gif\" alt=\"banner\">"
        "<img src=\"https://tracker/p.gif\" width=\"1\" height=\"1\"></p>", options);
    CHECK_EQ(doc.blocks.size(), size_t(1));
    CHECK_EQ(asked, 2);                                         // the 1x1 pixel is never fetched
    int images = 0;
    for (const auto& run : doc.blocks[0].runs) if (run.IsInlineImage()) ++images;
    CHECK_EQ(images, 1);
    CHECK(Contains(Text(doc.blocks[0]), "[banner]"));           // an unresolved picture keeps its alt text
}

static void TestTables() {
    // A one-column layout table is unwrapped; a two-column one is a table.
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<table width=\"600\" bgcolor=\"#eeeeee\"><tr><td><p>Intro</p></td></tr>"
        "<tr><td><table border=\"1\"><tr><th>Item</th><th>Price</th></tr>"
        "<tr><td bgcolor=\"#ffffcc\">Tea</td><td align=\"right\">3.50</td></tr></table></td></tr></table>");
    CHECK_EQ(doc.blocks.size(), size_t(2));
    CHECK(doc.blocks[0].type == RichBlockType::Paragraph);
    CHECK_EQ(Text(doc.blocks[0]), std::string("Intro"));
    CHECK(doc.blocks[0].paragraphBackground.empty());           // no layout backdrop
    const RichDocBlock& table = doc.blocks[1];
    CHECK(table.type == RichBlockType::Table);
    CHECK_EQ(table.tableRows.size(), size_t(2));
    CHECK(table.tableRows[0].header);
    CHECK_EQ(UCRichDocument::ConcatenateRunText(table.tableRows[1].cells[0].runs), std::string("Tea"));
    CHECK_EQ(table.tableRows[1].cells[0].backgroundColor, std::string("#FFFFCC"));
    CHECK(table.tableRows[1].cells[1].align == RichTextAlign::Right);
    CHECK(table.tableRows[1].cells[0].borderTop.IsVisible());

    // A table inside a cell becomes lines of that cell.
    doc = ImportHTMLToRichDocument(
        "<table><tr><td>A</td><td><table><tr><td>x</td><td>y</td></tr><tr><td>z</td></tr></table></td></tr></table>");
    CHECK_EQ(doc.blocks.size(), size_t(1));
    CHECK_EQ(UCRichDocument::ConcatenateRunText(doc.blocks[0].tableRows[0].cells[1].runs),
             std::string("x y\nz"));
}

static void TestSerializingQuotes() {
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<p>answer</p><blockquote><p>q1</p><blockquote><p>q2</p></blockquote></blockquote>");
    const std::string html = doc.ToHTML();
    CHECK(Contains(html, "<p>answer</p>\n<blockquote type=\"cite\""));
    size_t opened = 0;
    for (size_t at = html.find("<blockquote"); at != std::string::npos; at = html.find("<blockquote", at + 1)) ++opened;
    CHECK_EQ(opened, size_t(2));
    CHECK(Contains(html, "</blockquote>\n</blockquote>"));

    const std::string text = doc.ToPlainText();
    CHECK(Contains(text, "answer\n"));
    CHECK(Contains(text, "> q1\n"));
    CHECK(Contains(text, "> > q2\n"));
    const std::string markdown = doc.ToMarkdown();
    CHECK(Contains(markdown, "answer\n\n> q1\n>\n> > q2"));

    // Pictures: data: URIs by default, whatever the hook says otherwise.
    UCRichDocument pictures = ImportHTMLToRichDocument(
        "<p><img src=\"" + DataUri(PngHeader(40, 20)) + "\" width=\"40\"></p>");
    CHECK(Contains(pictures.ToHTML(), "src=\"data:image/png;base64,"));
    RichDocumentHTMLOptions options;
    options.imageSource = [](int index) { return "cid:part" + std::to_string(index); };
    const std::string mail = pictures.ToHTML(options);
    CHECK(Contains(mail, "src=\"cid:part0\""));
    CHECK(Contains(mail, "width=\"40\" height=\"20\""));

    // Import - export - import keeps the structure.
    UCRichDocument again = ImportHTMLToRichDocument(doc.ToHTML());
    CHECK_EQ(again.blocks.size(), doc.blocks.size());
    for (size_t i = 0; i < again.blocks.size() && i < doc.blocks.size(); ++i) {
        CHECK_EQ(again.blocks[i].quoteLevel, doc.blocks[i].quoteLevel);
        CHECK_EQ(Text(again.blocks[i]), Text(doc.blocks[i]));
    }
}

static void TestEditingQuotes() {
    auto doc = std::make_shared<UCRichDocument>(
        ImportHTMLToRichDocument("<p>top</p><blockquote><p>first line</p></blockquote>"));
    UCRichDocumentEditor editor;
    editor.SetDocument(doc);

    // Enter inside a quote: the new half stays quoted.
    editor.SetCaret(RichDocPosition(1, 5));                     // "first| line"
    editor.SplitBlock();
    CHECK_EQ(doc->blocks.size(), size_t(3));
    CHECK_EQ(doc->blocks[2].quoteLevel, 1);
    CHECK_EQ(Text(doc->blocks[2]), std::string(" line"));

    // Enter again on the empty quoted line in between steps out of the quote -
    // room to answer between two quoted lines.
    editor.SetCaret(RichDocPosition(1, 5));
    editor.SplitBlock();                                        // empty quoted line at 2
    CHECK_EQ(doc->blocks[2].quoteLevel, 1);
    editor.SplitBlock();
    CHECK_EQ(doc->blocks[2].quoteLevel, 0);
    CHECK_EQ(doc->blocks.size(), size_t(4));

    // Backspace at the start of a quoted block takes it out of the quote
    // first, and undo puts it back.
    editor.SetCaret(RichDocPosition(3, 0));
    CHECK(editor.DeleteBackward());
    CHECK_EQ(doc->blocks[3].quoteLevel, 0);
    CHECK_EQ(doc->blocks.size(), size_t(4));
    editor.Undo();
    CHECK_EQ(doc->blocks[3].quoteLevel, 1);
}

static void TestQuoteLevelCommands() {
    auto doc = std::make_shared<UCRichDocument>(
        ImportHTMLToRichDocument("<p>one</p><p>two</p><p>three</p>"));
    UCRichDocumentEditor editor;
    editor.SetDocument(doc);

    // Every block the selection touches moves; one that it only ends at the
    // start of does not.
    editor.SetSelection(RichDocPosition(0, 1), RichDocPosition(2, 0));
    editor.IncreaseQuoteLevel();
    CHECK_EQ(doc->blocks[0].quoteLevel, 1);
    CHECK_EQ(doc->blocks[1].quoteLevel, 1);
    CHECK_EQ(doc->blocks[2].quoteLevel, 0);
    editor.IncreaseQuoteLevel();
    CHECK_EQ(doc->blocks[1].quoteLevel, 2);

    // Just the caret's paragraph, one level out; undo puts it back.
    editor.SetCaret(RichDocPosition(1, 2));
    editor.DecreaseQuoteLevel();
    CHECK_EQ(doc->blocks[1].quoteLevel, 1);
    CHECK_EQ(doc->blocks[0].quoteLevel, 2);
    CHECK(editor.Undo());
    CHECK_EQ(doc->blocks[1].quoteLevel, 2);

    // Out of an unquoted paragraph there is nowhere to go, and no undo step
    // is recorded for it.
    editor.SetCaret(RichDocPosition(2, 0));
    editor.DecreaseQuoteLevel();
    CHECK_EQ(doc->blocks[2].quoteLevel, 0);
    CHECK(editor.Undo());                                       // undoes the earlier change instead
    CHECK_EQ(doc->blocks[1].quoteLevel, 1);
    CHECK_EQ(doc->blocks[0].quoteLevel, 1);

    // The level has a ceiling. (Undo put the caret back where its edit was.)
    editor.SetCaret(RichDocPosition(2, 0));
    for (int i = 0; i < 20; ++i) editor.IncreaseQuoteLevel();
    CHECK_EQ(doc->blocks[2].quoteLevel, 8);
}

static void TestDirectionAndPasteOptions() {
    std::cout << "\n--- dir, preAsCodeBlock, skipWordListLabels ---\n";
    // dir="rtl" on the body or a block reaches its paragraphs; dir="ltr"
    // inside turns it back.
    UCRichDocument doc = ImportHTMLToRichDocument(
        "<body dir=\"rtl\"><p>first</p><div dir=\"ltr\"><p>second</p></div><ul><li>item</li></ul></body>");
    CHECK(doc.blocks.size() == 3);
    if (doc.blocks.size() == 3) {
        CHECK(doc.blocks[0].rightToLeft);
        CHECK(!doc.blocks[1].rightToLeft);
        CHECK(doc.blocks[2].type == RichBlockType::ListItem && doc.blocks[2].rightToLeft);
    }

    // <pre>: monospaced lines of a paragraph by default (a mail's quoted
    // plain text), a code block with preAsCodeBlock (a paste).
    const std::string pre = "<pre>int a;\n\treturn a;\n</pre>";
    doc = ImportHTMLToRichDocument(pre);
    CHECK(doc.blocks.size() == 1 && doc.blocks[0].type == RichBlockType::Paragraph);
    if (!doc.blocks.empty()) {
        CHECK_EQ(Text(doc.blocks[0]), std::string("int a;\n return a;"));   // a tab is a space in text
        CHECK(!doc.blocks[0].runs.empty() && doc.blocks[0].runs[0].code);
    }
    HTMLRichImportOptions paste;
    paste.preAsCodeBlock = true;
    paste.skipWordListLabels = true;
    doc = ImportHTMLToRichDocument("<p>before</p>" + pre + "<p>after</p>", paste);
    CHECK(doc.blocks.size() == 3);
    if (doc.blocks.size() == 3) {
        CHECK(doc.blocks[1].type == RichBlockType::CodeBlock);
        CHECK_EQ(Text(doc.blocks[1]), std::string("int a;\n\treturn a;"));   // a code line keeps its tab
        CHECK(!doc.blocks[1].runs.empty() && !doc.blocks[1].runs[0].code);   // the block is code already
        CHECK_EQ(Text(doc.blocks[2]), std::string("after"));
    }

    // The list label Word types out: kept by default (a browser shows it),
    // left out with skipWordListLabels.
    const std::string word =
        "<p class=MsoListParagraph style='mso-list:l0 level1 lfo1'><![if !supportLists]>"
        "<span style='mso-list:Ignore'>1.<span>&nbsp;&nbsp;</span></span><![endif]>Item</p>";
    doc = ImportHTMLToRichDocument(word);
    CHECK(doc.blocks.size() == 1 && Contains(Text(doc.blocks[0]), "1.") && Contains(Text(doc.blocks[0]), "Item"));
    doc = ImportHTMLToRichDocument(word, paste);
    CHECK(doc.blocks.size() == 1 && Text(doc.blocks[0]) == "Item");
}

int main() {
    TestParagraphsAndRuns();
    TestLineBreaksAndDivs();
    TestHeadingsAndAlignment();
    TestLists();
    TestQuotes();
    TestCharacterStyles();
    TestImages();
    TestTables();
    TestSerializingQuotes();
    TestEditingQuotes();
    TestQuoteLevelCommands();
    TestDirectionAndPasteOptions();

    if (failures == 0) {
        std::cout << "ALL TESTS PASSED (" << checks << " checks)\n";
        return 0;
    }
    std::cout << failures << " FAILURES of " << checks << " checks\n";
    return 1;
}
