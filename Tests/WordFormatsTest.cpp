// Tests/WordFormatsTest.cpp
// Standalone round-trip test for the UltraCanvas Word document module.
// Exercises: Markdown -> UCRichDocument -> ODT/DOCX -> UCRichDocument -> Markdown,
// signature-based format detection, cross-format conversion, HTML output,
// and the ODF package invariants (mimetype first entry, stored).
// Builds without the UI stack: only the Word module sources, miniz and
// tinyxml2 (see Tests/CMakeLists.txt).
// Usage: WordFormatsTest [output-dir]   (default: current directory)
#include "Plugins/Documents/Word/UltraCanvasRichDocument.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasZipPackage.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        ++failures; \
    } \
} while (0)

#define CHECK_MSG(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "  [" << (msg) << "]\n"; \
        ++failures; \
    } \
} while (0)

// Minimal valid 1x1 red PNG.
static const unsigned char kTinyPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
    0x00, 0x03, 0x01, 0x01, 0x00, 0xC9, 0xFE, 0x92, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
    0x44, 0xAE, 0x42, 0x60, 0x82
};

static std::string gTmpDir;

static std::string TmpPath(const std::string& name) { return gTmpDir + "/" + name; }

static void WriteFile(const std::string& path, const void* data, size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

static UCRichDocument BuildSampleDocument() {
    std::string imgPath = TmpPath("sample.png");
    WriteFile(imgPath, kTinyPng, sizeof(kTinyPng));

    std::string markdown =
        "# Title Heading\n"
        "\n"
        "Plain paragraph with **bold**, *italic*, ***both***, ~~strike~~ and `code`.\n"
        "A [link](https://example.com/page) in a line.\n"
        "\n"
        "## Second level\n"
        "\n"
        "- first bullet\n"
        "- second bullet\n"
        "  - nested bullet\n"
        "1. first number\n"
        "2. second number\n"
        "\n"
        "> a block quote line\n"
        "\n"
        "```cpp\n"
        "int main() { return 0; }\n"
        "```\n"
        "\n"
        "| Name | Value |\n"
        "| --- | --- |\n"
        "| alpha | 1 |\n"
        "| beta | 2 |\n"
        "\n"
        "![tiny](" + imgPath + ")\n"
        "\n"
        "---\n"
        "\n"
        "Final paragraph.\n";

    UCRichDocument doc = UCRichDocument::FromMarkdown(markdown);
    doc.metadata.title = "Round Trip Sample";
    doc.metadata.author = "UltraCanvas Test";
    return doc;
}

static void CheckModelShape(const UCRichDocument& doc, const char* label) {
    int headings = 0, lists = 0, orderedItems = 0, tables = 0, images = 0,
        quotes = 0, codeBlocks = 0, rules = 0;
    bool sawBold = false, sawItalic = false, sawStrike = false, sawCode = false, sawLink = false;
    for (const auto& b : doc.blocks) {
        switch (b.type) {
            case RichBlockType::Heading: ++headings; break;
            case RichBlockType::ListItem: ++lists; if (b.orderedList) ++orderedItems; break;
            case RichBlockType::Table: ++tables; break;
            case RichBlockType::Image: ++images; break;
            case RichBlockType::BlockQuote: ++quotes; break;
            case RichBlockType::CodeBlock: ++codeBlocks; break;
            case RichBlockType::HorizontalRule: ++rules; break;
            default: break;
        }
        for (const auto& r : b.runs) {
            if (r.bold) sawBold = true;
            if (r.italic) sawItalic = true;
            if (r.strikethrough) sawStrike = true;
            if (r.code) sawCode = true;
            if (!r.linkTarget.empty()) sawLink = true;
        }
    }
    CHECK_MSG(headings == 2, label);
    CHECK_MSG(lists == 5, label);
    CHECK_MSG(orderedItems == 2, label);
    CHECK_MSG(tables == 1, label);
    CHECK_MSG(images == 1, label);
    CHECK_MSG(quotes == 1, label);
    CHECK_MSG(codeBlocks == 1, label);
    CHECK_MSG(rules >= 1, label);
    CHECK_MSG(sawBold && sawItalic && sawStrike && sawCode && sawLink, label);
    CHECK_MSG(!doc.media.empty(), label);
    if (!doc.media.empty()) {
        CHECK_MSG(doc.media[0].data.size() == sizeof(kTinyPng), label);
    }
    // Table content check
    for (const auto& b : doc.blocks) {
        if (b.type != RichBlockType::Table) continue;
        CHECK_MSG(b.tableRows.size() == 3, label);
        if (b.tableRows.size() == 3 && b.tableRows[1].cells.size() == 2) {
            CHECK_MSG(UCRichDocument::ConcatenateRunText(b.tableRows[1].cells[0].runs) == "alpha", label);
        }
    }
    // Inline text sanity
    std::string plain = doc.ToPlainText();
    CHECK_MSG(plain.find("Plain paragraph with bold, italic, both, strike and code.") != std::string::npos, label);
    CHECK_MSG(plain.find("int main() { return 0; }") != std::string::npos, label);
}

int main(int argc, char** argv) {
    gTmpDir = (argc > 1) ? argv[1] : ".";
    std::filesystem::create_directories(gTmpDir);

    // ===== 1. Markdown parse sanity =====
    UCRichDocument doc = BuildSampleDocument();
    CheckModelShape(doc, "markdown-parse");

    // ===== 2. ODT round trip =====
    {
        std::string err;
        CHECK_MSG(UCWordDocumentIO::SaveOdt(TmpPath("sample.odt"), doc, err), err);
        UCRichDocument back;
        CHECK_MSG(UCWordDocumentIO::LoadOdt(TmpPath("sample.odt"), back, err), err);
        CheckModelShape(back, "odt-roundtrip");
        CHECK(back.metadata.title == "Round Trip Sample");
        CHECK(back.metadata.author == "UltraCanvas Test");

        // Second generation must be stable: Save(Load(Save(x))) == same shape.
        CHECK_MSG(UCWordDocumentIO::SaveOdt(TmpPath("sample2.odt"), back, err), err);
        UCRichDocument gen2;
        CHECK_MSG(UCWordDocumentIO::LoadOdt(TmpPath("sample2.odt"), gen2, err), err);
        CheckModelShape(gen2, "odt-gen2");
    }

    // ===== 3. DOCX round trip =====
    {
        std::string err;
        CHECK_MSG(UCWordDocumentIO::SaveDocx(TmpPath("sample.docx"), doc, err), err);
        UCRichDocument back;
        CHECK_MSG(UCWordDocumentIO::LoadDocx(TmpPath("sample.docx"), back, err), err);
        CheckModelShape(back, "docx-roundtrip");
        CHECK(back.metadata.title == "Round Trip Sample");

        CHECK_MSG(UCWordDocumentIO::SaveDocx(TmpPath("sample2.docx"), back, err), err);
        UCRichDocument gen2;
        CHECK_MSG(UCWordDocumentIO::LoadDocx(TmpPath("sample2.docx"), gen2, err), err);
        CheckModelShape(gen2, "docx-gen2");
    }

    // ===== 4. Cross-format conversion docx -> odt =====
    {
        std::string err;
        UCRichDocument fromDocx;
        CHECK_MSG(UCWordDocumentIO::Load(TmpPath("sample.docx"), fromDocx, err), err);
        CHECK_MSG(UCWordDocumentIO::Save(TmpPath("converted.odt"), fromDocx, err), err);
        UCRichDocument converted;
        CHECK_MSG(UCWordDocumentIO::Load(TmpPath("converted.odt"), converted, err), err);
        CheckModelShape(converted, "docx-to-odt");
    }

    // ===== 5. Signature-based detection =====
    {
        CHECK(DetectWordDocumentFormat(TmpPath("sample.odt")) == WordDocumentFormat::Odt);
        CHECK(DetectWordDocumentFormat(TmpPath("sample.docx")) == WordDocumentFormat::Docx);

        // Renamed files must still classify by content.
        std::ifstream src(TmpPath("sample.docx"), std::ios::binary);
        std::ofstream dst(TmpPath("renamed.odt"), std::ios::binary);
        dst << src.rdbuf();
        src.close(); dst.close();
        CHECK(DetectWordDocumentFormat(TmpPath("renamed.odt")) == WordDocumentFormat::Docx);

        // CFB magic -> legacy .doc; a truncated CFB fails extraction with a
        // friendly convert-to-.docx message.
        const unsigned char cfb[16] = {0xD0,0xCF,0x11,0xE0,0xA1,0xB1,0x1A,0xE1,0,0,0,0,0,0,0,0};
        WriteFile(TmpPath("legacy.doc"), cfb, sizeof(cfb));
        CHECK(DetectWordDocumentFormat(TmpPath("legacy.doc")) == WordDocumentFormat::LegacyDoc);
        UCRichDocument dummy;
        std::string err;
        CHECK(!UCWordDocumentIO::Load(TmpPath("legacy.doc"), dummy, err));
        CHECK(err.find(".docx") != std::string::npos);

        // Garbage is Unknown with a clear error.
        WriteFile(TmpPath("garbage.docx"), "not a zip file at all", 21);
        CHECK(DetectWordDocumentFormat(TmpPath("garbage.docx")) == WordDocumentFormat::Unknown);
        CHECK(!UCWordDocumentIO::Load(TmpPath("garbage.docx"), dummy, err));
        CHECK(!err.empty());
    }

    // ===== 5b. Legacy .doc text extraction (real Word 97 fixture) =====
#ifdef WORDTEST_FIXTURE_DIR
    {
        std::string fixture = std::string(WORDTEST_FIXTURE_DIR) + "/legacy-word97.doc";
        if (std::ifstream(fixture, std::ios::binary).good()) {
            UCRichDocument legacy;
            std::string err;
            CHECK_MSG(UCWordDocumentIO::Load(fixture, legacy, err), err);
            std::string plain = legacy.ToPlainText();
            CHECK(plain.find("Real Document") != std::string::npos);
            CHECK(plain.find("Normal text with bold part and italic part and a link.")
                  != std::string::npos);
            CHECK(plain.find("numbered two") != std::string::npos);
            CHECK(plain.find("cell 1\tcell 2") != std::string::npos);
            CHECK(plain.find("Closing paragraph.") != std::string::npos);
            // Field instructions (e.g. "HYPERLINK http://...") must be dropped.
            CHECK(plain.find("HYPERLINK") == std::string::npos);
        } else {
            std::cerr << "NOTE: fixture missing, skipping legacy .doc check: "
                      << fixture << "\n";
        }
    }
#endif

    // ===== 6. Markdown emission and re-parse =====
    {
        RichDocumentMarkdownOptions opts;
        opts.imageDirectory = TmpPath("mdmedia");
        std::string md = doc.ToMarkdown(opts);
        CHECK(md.find("# Title Heading") != std::string::npos);
        CHECK(md.find("**bold**") != std::string::npos);
        CHECK(md.find("*italic*") != std::string::npos);
        CHECK(md.find("~~strike~~") != std::string::npos);
        CHECK(md.find("`code`") != std::string::npos);
        CHECK(md.find("[link](https://example.com/page)") != std::string::npos);
        CHECK(md.find("| alpha | 1 |") != std::string::npos);
        CHECK(md.find("```cpp") != std::string::npos);
        CHECK(md.find("![") != std::string::npos);

        UCRichDocument reparsed = UCRichDocument::FromMarkdown(md);
        CheckModelShape(reparsed, "markdown-reemit");
    }

    // ===== 7. HTML output =====
    {
        std::string html = doc.ToHTML();
        CHECK(html.find("<h1>") != std::string::npos);
        CHECK(html.find("<table") != std::string::npos);
        CHECK(html.find("data:image/png;base64,") != std::string::npos);
        CHECK(html.find("<ol>") != std::string::npos);
        CHECK(html.find("<ul>") != std::string::npos);
        CHECK(html.find("<blockquote>") != std::string::npos);
    }

    // ===== 8. ODF package invariants (mimetype first + stored) =====
    {
        std::ifstream odt(TmpPath("sample.odt"), std::ios::binary);
        char header[64] = {};
        odt.read(header, sizeof(header));
        // Local file header: name length at offset 26, name at offset 30.
        CHECK(std::string(header + 30, 8) == "mimetype");
        // Compression method (offset 8) must be 0 (stored).
        CHECK(header[8] == 0 && header[9] == 0);
    }

    // ===== 9. Embedded math import (OMML in DOCX, MathML in ODT) =====
    {
        // Minimal DOCX whose single paragraph is an OMML fraction b/2a.
        std::string documentXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
            "xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\">"
            "<w:body><w:p><w:r><w:t xml:space=\"preserve\">value: </w:t></w:r>"
            "<m:oMath><m:f><m:num><m:r><m:t>b</m:t></m:r></m:num>"
            "<m:den><m:r><m:t>2a</m:t></m:r></m:den></m:f></m:oMath>"
            "</w:p></w:body></w:document>";
        {
            UCZipPackageWriter zip;
            CHECK(zip.Open(TmpPath("math.docx")));
            zip.AddEntry("[Content_Types].xml",
                std::string("<?xml version=\"1.0\"?>"
                "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
                "</Types>"));
            zip.AddEntry("_rels/.rels",
                std::string("<?xml version=\"1.0\"?>"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
                "</Relationships>"));
            zip.AddEntry("word/document.xml", documentXml);
            CHECK(zip.Finalize());
        }
        UCRichDocument mathDoc;
        std::string err;
        CHECK_MSG(UCWordDocumentIO::Load(TmpPath("math.docx"), mathDoc, err), err);
        std::string plain = mathDoc.ToPlainText();
        CHECK_MSG(plain.find("$\\frac{b}{2a}$") != std::string::npos, plain);

        // Minimal ODT with an embedded MathML formula object (x^2).
        std::string contentXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-content "
            "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
            "xmlns:xlink=\"http://www.w3.org/1999/xlink\" office:version=\"1.2\">"
            "<office:body><office:text>"
            "<text:p>square: <draw:frame><draw:object xlink:href=\"./Object 1\"/></draw:frame></text:p>"
            "</office:text></office:body></office:document-content>";
        std::string mathMl =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<math:math xmlns:math=\"http://www.w3.org/1998/Math/MathML\">"
            "<math:semantics><math:msup><math:mi>x</math:mi><math:mn>2</math:mn></math:msup>"
            "</math:semantics></math:math>";
        {
            UCZipPackageWriter zip;
            CHECK(zip.Open(TmpPath("math.odt")));
            zip.AddEntry("mimetype", std::string("application/vnd.oasis.opendocument.text"), false);
            zip.AddEntry("content.xml", contentXml);
            zip.AddEntry("Object 1/content.xml", mathMl);
            zip.AddEntry("META-INF/manifest.xml",
                std::string("<?xml version=\"1.0\"?>"
                "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
                "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
                "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
                "</manifest:manifest>"));
            CHECK(zip.Finalize());
        }
        UCRichDocument odtMathDoc;
        CHECK_MSG(UCWordDocumentIO::Load(TmpPath("math.odt"), odtMathDoc, err), err);
        plain = odtMathDoc.ToPlainText();
        CHECK_MSG(plain.find("$x^{2}$") != std::string::npos, plain);
    }

    // ===== 10. ODT letterhead features: text-box frames, master-page =====
    // header/footer, hidden sections, "./"-prefixed picture hrefs.
    {
        std::string contentXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-content "
            "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
            "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
            "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
            "xmlns:xlink=\"http://www.w3.org/1999/xlink\" office:version=\"1.2\">"
            "<office:body><office:text>"
            // Paragraph-anchored frame with a text box (the letterhead sender
            // block) — its paragraphs must appear as separate blocks.
            "<text:p>"
            "<draw:frame draw:name=\"SenderBox\" text:anchor-type=\"paragraph\">"
            "<draw:text-box>"
            "<text:p>Example GmbH</text:p>"
            "<text:p>Sample Street 1</text:p>"
            "<text:p>phone +49 123 456</text:p>"
            "</draw:text-box></draw:frame>"
            "Dear applicant,</text:p>"
            // Page-anchored frame directly in the text flow with a picture
            // whose href carries the "./" prefix.
            "<draw:frame draw:name=\"Signature\" svg:width=\"96pt\" svg:height=\"48pt\">"
            "<draw:image xlink:href=\"./Pictures/sig.png\" xlink:type=\"simple\"/>"
            "</draw:frame>"
            "<text:p>body paragraph</text:p>"
            // A switched-off section must not render.
            "<text:section text:name=\"Hidden\" text:display=\"none\">"
            "<text:p>Payment and Order Details</text:p>"
            "</text:section>"
            "</office:text></office:body></office:document-content>";
        std::string stylesXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-styles "
            "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
            "office:version=\"1.2\">"
            "<office:master-styles><style:master-page style:name=\"Standard\">"
            "<style:header><text:p>Example GmbH - Sample Street 1</text:p></style:header>"
            "<style:footer><table:table table:name=\"FooterTable\">"
            "<table:table-row>"
            "<table:table-cell><text:p>Bank Ltd</text:p><text:p>Konto: 12345</text:p></table:table-cell>"
            "<table:table-cell><text:p>Register B 7741</text:p></table:table-cell>"
            "</table:table-row>"
            "</table:table></style:footer>"
            "</style:master-page></office:master-styles>"
            "</office:document-styles>";
        {
            UCZipPackageWriter zip;
            CHECK(zip.Open(TmpPath("letter.odt")));
            zip.AddEntry("mimetype", std::string("application/vnd.oasis.opendocument.text"), false);
            zip.AddEntry("content.xml", contentXml);
            zip.AddEntry("styles.xml", stylesXml);
            zip.AddEntry("Pictures/sig.png", kTinyPng, sizeof(kTinyPng));
            CHECK(zip.Finalize());
        }
        UCRichDocument letter;
        std::string err;
        CHECK_MSG(UCWordDocumentIO::Load(TmpPath("letter.odt"), letter, err), err);
        std::string plain = letter.ToPlainText();

        // Text-box (sender block) content survives as separate lines.
        CHECK_MSG(plain.find("Example GmbH") != std::string::npos, plain);
        CHECK_MSG(plain.find("Sample Street 1") != std::string::npos, plain);
        CHECK_MSG(plain.find("phone +49 123 456") != std::string::npos, plain);
        CHECK(plain.find("Dear applicant,") != std::string::npos);

        // Master-page header renders before the body, footer table after it.
        CHECK(!letter.blocks.empty());
        if (!letter.blocks.empty()) {
            CHECK(UCRichDocument::ConcatenateRunText(letter.blocks.front().runs)
                  == "Example GmbH - Sample Street 1");
            CHECK(letter.blocks.back().type == RichBlockType::Table);
        }
        CHECK_MSG(plain.find("Bank Ltd\nKonto: 12345\tRegister B 7741") != std::string::npos, plain);
        size_t headerPos = plain.find("Example GmbH - Sample Street 1");
        size_t bodyPos = plain.find("Dear applicant,");
        size_t footerPos = plain.find("Bank Ltd");
        CHECK(headerPos != std::string::npos && bodyPos != std::string::npos
              && footerPos != std::string::npos && headerPos < bodyPos && bodyPos < footerPos);

        // Hidden section content must not render.
        CHECK_MSG(plain.find("Payment and Order Details") == std::string::npos, plain);

        // "./"-prefixed picture href loads; the page-anchored frame emits an image block.
        CHECK(letter.media.size() == 1);
        bool sawSignature = false;
        for (const auto& b : letter.blocks) {
            if (b.type == RichBlockType::Image && b.imageAltText == "Signature") sawSignature = true;
        }
        CHECK(sawSignature);
    }

    // ===== 11. Letterhead: applied first-page master, pinned by a table =====
    // Mirrors the real "IK Softwareportal" letterhead. The letterhead chrome
    // (logo, sender, four-line contact block) sits in PAGE-ANCHORED FRAMES in
    // the body, not in any header. Page 1 draws from the "First Page" master,
    // pinned by the leading layout TABLE's style (table:style-name), and that
    // master carries only the bank-details footer. The unrelated "Standard"
    // continuation master (written first) owns a "Seite N / 1" page-number
    // header that only ever shows on page 2+, so it must NOT be rendered for a
    // one-page letter.
    {
        std::string contentXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-content "
            "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
            "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
            "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
            "xmlns:xlink=\"http://www.w3.org/1999/xlink\" office:version=\"1.2\">"
            "<office:automatic-styles>"
            // A TABLE style pins the first-page master (as the real file does).
            "<style:style style:name=\"TB\" style:family=\"table\" "
            "style:master-page-name=\"First_20_Page\"/>"
            "</office:automatic-styles>"
            "<office:body><office:text>"
            // Leading non-flow nodes: page-anchored logo and contact frames.
            "<draw:frame draw:name=\"Logo\" text:anchor-type=\"page\" "
            "svg:width=\"72pt\" svg:height=\"48pt\">"
            "<draw:image xlink:href=\"Pictures/logo.png\"/></draw:frame>"
            "<draw:frame draw:name=\"Contacts\" text:anchor-type=\"page\">"
            "<draw:text-box>"
            "<text:p>phone +49 (0) 2761 82 81 69</text:p>"
            "<text:p>fax +49 (0) 911 308 44 77 844</text:p>"
            "<text:p>mobile +49 (0) 170 754 22 91</text:p>"
            "<text:p>info@interkontakt.net</text:p>"
            "</draw:text-box></draw:frame>"
            // First FLOW block is the reference table, which pins First Page.
            "<table:table table:name=\"Ref\" table:style-name=\"TB\">"
            "<table:table-row>"
            "<table:table-cell><text:p>Datum</text:p></table:table-cell>"
            "<table:table-cell><text:p>9.2.2021</text:p></table:table-cell>"
            "</table:table-row></table:table>"
            "<text:p>Business Bonus Payment</text:p>"
            "<text:p>The company pays a bonus every month.</text:p>"
            "</office:text></office:body></office:document-content>";
        std::string stylesXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-styles "
            "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
            "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            "office:version=\"1.2\">"
            "<office:master-styles>"
            // Continuation master (written first): page-number header only.
            "<style:master-page style:name=\"Standard\">"
            "<style:header>"
            "<text:p>Seite <text:page-number text:select-page=\"current\"/> / "
            "<text:page-count>1</text:page-count></text:p>"
            "</style:header></style:master-page>"
            // First-page master: bank-details footer only, no header.
            "<style:master-page style:name=\"First_20_Page\">"
            "<style:footer><table:table table:name=\"F\">"
            "<table:table-row>"
            "<table:table-cell><text:p>Deutsche Bank 24</text:p></table:table-cell>"
            "<table:table-cell><text:p>Handelsregister B 126110</text:p></table:table-cell>"
            "<table:table-cell><text:p>Finanzamt</text:p></table:table-cell>"
            "</table:table-row></table:table></style:footer>"
            "</style:master-page>"
            "</office:master-styles></office:document-styles>";
        {
            UCZipPackageWriter zip;
            CHECK(zip.Open(TmpPath("letterhead.odt")));
            zip.AddEntry("mimetype", std::string("application/vnd.oasis.opendocument.text"), false);
            zip.AddEntry("content.xml", contentXml);
            zip.AddEntry("styles.xml", stylesXml);
            zip.AddEntry("Pictures/logo.png", kTinyPng, sizeof(kTinyPng));
            CHECK(zip.Finalize());
        }
        UCRichDocument letter;
        std::string err;
        CHECK_MSG(UCWordDocumentIO::Load(TmpPath("letterhead.odt"), letter, err), err);
        std::string plain = letter.ToPlainText();

        // Logo and contact block come from the body's page-anchored frames.
        CHECK_MSG(letter.media.size() == 1, "logo image loaded");
        bool sawLogo = false;
        for (const auto& b : letter.blocks) {
            if (b.type == RichBlockType::Image) sawLogo = true;
        }
        CHECK_MSG(sawLogo, "logo image block emitted");
        CHECK_MSG(plain.find("+49 (0) 2761 82 81 69") != std::string::npos, plain);
        CHECK_MSG(plain.find("+49 (0) 911 308 44 77 844") != std::string::npos, plain);
        CHECK_MSG(plain.find("+49 (0) 170 754 22 91") != std::string::npos, plain);
        CHECK_MSG(plain.find("info@interkontakt.net") != std::string::npos, plain);

        // The First-Page master's footer is rendered even though the header is
        // on the (different) Standard master.
        CHECK_MSG(plain.find("Deutsche Bank 24") != std::string::npos, plain);
        CHECK_MSG(plain.find("Handelsregister B 126110") != std::string::npos, plain);
        CHECK_MSG(plain.find("Finanzamt") != std::string::npos, plain);
        CHECK_MSG(plain.find("Business Bonus Payment") != std::string::npos, plain);

        // The Standard continuation master's page-number header must NOT leak
        // in — it never displays on a one-page letter (matches OpenOffice).
        CHECK_MSG(plain.find("Seite") == std::string::npos, plain);

        // Ordering: contacts (body) before body text, footer after it.
        size_t contactPos = plain.find("+49 (0) 2761 82 81 69");
        size_t bodyPos = plain.find("Business Bonus Payment");
        size_t footerPos = plain.find("Deutsche Bank 24");
        CHECK(contactPos < bodyPos && bodyPos < footerPos);
    }

    // ===== 11b. Header page furniture: page-number field + empty layout table =====
    // When a header IS displayed (default "Standard" master, no pin), a
    // text:page-number field must render as page 1 (not an empty gap), and a
    // text-free layout table must not inject a stray cell separator.
    {
        std::string contentXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-content "
            "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            "office:version=\"1.2\">"
            "<office:body><office:text>"
            "<text:p>body</text:p>"
            "</office:text></office:body></office:document-content>";
        std::string stylesXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-styles "
            "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
            "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            "office:version=\"1.2\">"
            "<office:master-styles><style:master-page style:name=\"Standard\">"
            "<style:header>"
            "<table:table table:name=\"Layout\">"
            "<table:table-row><table:table-cell><text:p/></table:table-cell>"
            "<table:table-cell><text:p/></table:table-cell></table:table-row></table:table>"
            "<text:p>Seite <text:page-number text:select-page=\"current\"/> / "
            "<text:page-count>1</text:page-count></text:p>"
            "</style:header></style:master-page></office:master-styles>"
            "</office:document-styles>";
        {
            UCZipPackageWriter zip;
            CHECK(zip.Open(TmpPath("furniture.odt")));
            zip.AddEntry("mimetype", std::string("application/vnd.oasis.opendocument.text"), false);
            zip.AddEntry("content.xml", contentXml);
            zip.AddEntry("styles.xml", stylesXml);
            CHECK(zip.Finalize());
        }
        UCRichDocument doc3;
        std::string err;
        CHECK_MSG(UCWordDocumentIO::Load(TmpPath("furniture.odt"), doc3, err), err);
        std::string plain = doc3.ToPlainText();
        CHECK_MSG(plain.find("Seite 1 / 1") != std::string::npos, plain);
        // No table block precedes the page-number line (empty table skipped).
        CHECK(!doc3.blocks.empty());
        CHECK_MSG(doc3.blocks.front().type != RichBlockType::Table, "empty layout table skipped");
    }

    // ===== 12. Table cells preserve non-paragraph block content =====
    // A cell can hold headings, lists and nested tables, not just text:p. All
    // of it must survive into the flattened cell text.
    {
        std::string contentXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<office:document-content "
            "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            "office:version=\"1.2\">"
            "<office:body><office:text>"
            "<table:table table:name=\"T\">"
            "<table:table-row>"
            "<table:table-cell>"
            "<text:h text:outline-level=\"3\">Cell Heading</text:h>"
            "<text:list><text:list-item><text:p>bullet one</text:p></text:list-item>"
            "<text:list-item><text:p>bullet two</text:p></text:list-item></text:list>"
            "</table:table-cell>"
            "<table:table-cell><text:p>plain cell</text:p></table:table-cell>"
            "</table:table-row></table:table>"
            "</office:text></office:body></office:document-content>";
        {
            UCZipPackageWriter zip;
            CHECK(zip.Open(TmpPath("cellcontent.odt")));
            zip.AddEntry("mimetype", std::string("application/vnd.oasis.opendocument.text"), false);
            zip.AddEntry("content.xml", contentXml);
            CHECK(zip.Finalize());
        }
        UCRichDocument doc2;
        std::string err;
        CHECK_MSG(UCWordDocumentIO::Load(TmpPath("cellcontent.odt"), doc2, err), err);
        CHECK(!doc2.blocks.empty());
        bool foundTable = false;
        for (const auto& b : doc2.blocks) {
            if (b.type != RichBlockType::Table || b.tableRows.empty()) continue;
            foundTable = true;
            const auto& cell0 = b.tableRows[0].cells[0];
            std::string cellText = UCRichDocument::ConcatenateRunText(cell0.runs);
            CHECK_MSG(cellText.find("Cell Heading") != std::string::npos, cellText);
            CHECK_MSG(cellText.find("bullet one") != std::string::npos, cellText);
            CHECK_MSG(cellText.find("bullet two") != std::string::npos, cellText);
        }
        CHECK(foundTable);
    }

    if (failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << failures << " FAILURES\n";
    return 1;
}
