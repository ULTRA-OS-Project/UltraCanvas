// Tests/PresentationOdpTest.cpp
// UltraCanvasPresentationOdp: OpenDocument presentations read into the
// PowerPoint reader's model and outlined the same way.
//
//   - office-pptx-sample.odp is the PowerPoint fixture as LibreOffice Impress
//     converts it (make-odp-fixtures.py): the same five slides read from an
//     independent ODF producer - checked shape by shape and against what the
//     .pptx reader makes of the original
//   - presentation-features.fodp, a flat deck written by hand: white space,
//     nested and unnumbered lists, inherited styles, groups, hyperlinked
//     shapes, slide furniture and prompts left out (a prompting placeholder
//     kept empty), a hidden slide, a table
//     with a covered cell, an embedded object left out, an inline picture, a
//     page comment left out
//   - a flat deck written in the test for the look as drawn: style parents and
//     defaults, ODF's shape types, paths, lines, gradients, numbering, a
//     table template, backgrounds
//   - a password-protected package and other ODF types refused with a reason
//   - UltraCanvasFileLoader (LoadTextDocument, the Open dialog's types, text
//     extraction) and UltraFiler's preview page take the new types
// Version: 1.1.1 - a prompting placeholder is kept as an empty one (an editor types into it)
// Version: 1.1.0 - the look as drawn: a flat deck written in the test
// Version: 1.0.1 - the protected package's manifest is a whole one (it now opens with a password;
//                  OdfEncryptionTest reads real ones)
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasZipPackage.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef ODPTEST_FIXTURE_DIR
#error "ODPTEST_FIXTURE_DIR must name Tests/fixtures"
#endif

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string Fixture(const char* name) { return std::string(ODPTEST_FIXTURE_DIR) + "/" + name; }

const PptxShape* FindShape(const PptxSlide& slide, PptxShapeKind kind, const std::string& name = std::string()) {
    for (const PptxShape& shape : slide.shapes)
        if (shape.kind == kind && (name.empty() || shape.name == name)) return &shape;
    return nullptr;
}

bool AnyText(const PptxPresentation& deck, const std::string& text) {
    for (const PptxSlide& slide : deck.slides) {
        for (const PptxShape& shape : slide.shapes) {
            for (const PptxParagraph& p : shape.paragraphs)
                if (Contains(p.Text(), text)) return true;
            for (const auto& row : shape.rows)
                for (const auto& cell : row)
                    for (const PptxParagraph& p : cell)
                        if (Contains(p.Text(), text)) return true;
        }
        for (const PptxParagraph& p : slide.notes)
            if (Contains(p.Text(), text)) return true;
    }
    return false;
}

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream file(PathFromUtf8(path), std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool Near(int64_t value, int64_t expected) { return std::llabs(value - expected) <= 400; }   // a few µm

void TestLibreOfficeDeck() {
    std::cout << "\n=== The PowerPoint fixture as LibreOffice writes it (.odp) ===\n";
    const std::string path = Fixture("office-pptx-sample.odp");
    Check(IsOdpPresentationFile(path), "an .odp is told by its content");
    Check(!IsOdpPresentationFile(Fixture("office-pptx-sample.pptx")) &&
              !IsOdpPresentationFile(Fixture("word97-formatting.odt")),
          "... and not a .pptx, nor an OpenDocument text");
    Check(IsOdpExtension("deck.ODP") && IsOdpExtension("otp") && IsOdpExtension(".fodp") &&
              !IsOdpExtension("x.odt") && !IsOdpExtension("pptx"),
          "the OpenDocument presentation extensions are known");

    PptxPresentation deck;
    std::string error;
    const bool ok = ReadOdpPresentation(path, deck, error);
    Check(ok, "ReadOdpPresentation reads it");
    if (!ok) {
        std::cout << "         " << error << "\n";
        return;
    }
    Check(Near(deck.slideWidth, 12192000) && Near(deck.slideHeight, 6858000), "16:9 slide size, in EMU");
    Check(deck.slides.size() == 5, "five slides, in order");
    if (deck.slides.size() != 5) return;
    Check(deck.slides[0].title == "Quarterly Review" && deck.slides[1].title == "Agenda" &&
              deck.slides[2].title == "Figures" && deck.slides[4].title == "Appendix",
          "each slide's title");
    const PptxShape* subtitle = FindShape(deck.slides[0], PptxShapeKind::Subtitle);
    Check(subtitle && subtitle->paragraphs.size() == 1 && subtitle->paragraphs[0].Text() == "October 2026",
          "the title slide's subtitle");

    const PptxShape* body = FindShape(deck.slides[1], PptxShapeKind::Body);
    Check(body && body->paragraphs.size() == 6, "the agenda's six paragraphs");
    if (body && body->paragraphs.size() == 6) {
        const auto& p = body->paragraphs;
        Check(p[0].Text() == "Results" && p[0].bullet && p[0].level == 0 && !p[0].runs.empty() &&
                  p[0].runs[0].bold,
              "a bold top-level bullet");
        Check(p[1].Text() == "Revenue up" && p[1].level == 1 && p[2].level == 1 && !p[1].runs[0].bold,
              "two level-2 bullets");
        Check(p[3].Text() == "Next steps" && p[3].bullet && !p[3].numbered && p[4].Text() == "Hire" &&
                  p[4].numbered && p[5].numbered,
              "a numbered pair after a plain bullet");
    }
    const PptxShape* box = FindShape(deck.slides[1], PptxShapeKind::Text);
    Check(box && box->paragraphs.size() == 1 && box->paragraphs[0].Text() == "Confidential" &&
              !box->paragraphs[0].bullet && Near(box->x, 1633680) && Near(box->y, 5943600),
          "the text box (a custom shape here), unbulleted, at its place (4.538 cm, 16.51 cm)");
    Check(deck.slides[1].notes.size() == 1 && deck.slides[1].notes[0].Text() == "Remember the Q3 numbers.",
          "the speaker notes, and not the notes page's slide number");
    Check(deck.slides[0].notes.empty(), "an empty notes placeholder gives no notes");

    const PptxShape* table = FindShape(deck.slides[2], PptxShapeKind::Table);
    Check(table && table->rows.size() == 2 && table->rows[0].size() == 2 && table->rows[0][0][0].Text() == "Quarter" &&
              table->rows[1][1][0].Text() == "1.2M",
          "the table's cells");
    const PptxShape* picture = FindShape(deck.slides[2], PptxShapeKind::Picture);
    Check(picture && picture->altText == "Revenue chart" && picture->pictureTarget.rfind("Pictures/", 0) == 0 &&
              picture->pictureTarget.find(".png") != std::string::npos,
          "the picture's description and part");
    size_t pictures = 0;
    for (const PptxShape& shape : deck.slides[2].shapes) pictures += shape.kind == PptxShapeKind::Picture;
    Check(pictures == 1, "the table's preview picture is not a second picture");
    Check(deck.slides[3].hidden && !deck.slides[2].hidden && !deck.slides[4].hidden, "the hidden slide is marked");
    const PptxShape* appendix = FindShape(deck.slides[4], PptxShapeKind::Body);
    Check(appendix && appendix->paragraphs.size() == 60 && appendix->paragraphs[59].Text() == "Item 60",
          "sixty bullets on the last slide");
    Check(!AnyText(deck, "<number>"), "no slide-number placeholder text");

    // The same deck through the PowerPoint reader: the same slides.
    PptxPresentation original;
    if (ReadPptxPresentation(Fixture("office-pptx-sample.pptx"), original, error) &&
        original.slides.size() == deck.slides.size()) {
        bool same = true;
        for (size_t i = 0; i < deck.slides.size(); ++i) {
            same = same && deck.slides[i].title == original.slides[i].title &&
                   deck.slides[i].hidden == original.slides[i].hidden &&
                   deck.slides[i].notes.size() == original.slides[i].notes.size();
            const PptxShape* a = FindShape(deck.slides[i], PptxShapeKind::Body);
            const PptxShape* b = FindShape(original.slides[i], PptxShapeKind::Body);
            if (a && b) {
                same = same && a->paragraphs.size() == b->paragraphs.size();
                for (size_t k = 0; same && k < a->paragraphs.size(); ++k)
                    same = a->paragraphs[k].Text() == b->paragraphs[k].Text() &&
                           a->paragraphs[k].level == b->paragraphs[k].level &&
                           a->paragraphs[k].bullet == b->paragraphs[k].bullet &&
                           a->paragraphs[k].numbered == b->paragraphs[k].numbered;
            } else {
                same = same && !a && !b;
            }
        }
        Check(same, "titles, hidden slides, notes and every bullet match the .pptx reader's");
    } else {
        Check(false, "the .pptx original reads for the comparison");
    }

    const UCRichDocument outline = PptxPresentationToRichDocument(deck);
    const std::string text = outline.ToPlainText();
    Check(Contains(text, "1. Quarterly Review") && Contains(text, "4. Backup (hidden)") &&
              Contains(text, "Revenue up") && Contains(text, "Remember the Q3 numbers.") &&
              Contains(text, "[Picture: Revenue chart]"),
          "the outline holds headings, bullets, notes and the picture");

    // From memory: the same.
    PptxPresentation fromMemory;
    Check(ReadOdpPresentationFromMemory(ReadFile(path), fromMemory, error) && fromMemory.slides.size() == 5 &&
              fromMemory.slides[1].title == "Agenda",
          "ReadOdpPresentationFromMemory reads the package");
}

void TestFlatDeck() {
    std::cout << "\n=== A flat deck written by hand (.fodp) ===\n";
    const std::string path = Fixture("presentation-features.fodp");
    Check(IsOdpPresentationFile(path), "a .fodp is told by its office:mimetype");
    PptxPresentation deck;
    std::string error;
    const bool ok = ReadOdpPresentation(path, deck, error);
    Check(ok, "ReadOdpPresentation reads it");
    if (!ok) {
        std::cout << "         " << error << "\n";
        return;
    }
    Check(deck.slideWidth == 9144000 && deck.slideHeight == 6858000, "4:3 slide size from the master's page layout");
    Check(deck.slides.size() == 3, "three slides");
    if (deck.slides.size() != 3) return;

    const PptxSlide& first = deck.slides[0];
    Check(first.title == "Welcome  to ODP",
          "white space: collapsed and trimmed, a <text:s> kept (\"" + first.title + "\")");
    const PptxShape* title = FindShape(first, PptxShapeKind::Title);
    Check(title && title->x == 914400 && title->y == 914400 && title->width == 7200000 && title->height == 914400,
          "lengths in cm, in and pt become EMU");
    const PptxShape* subtitle = FindShape(first, PptxShapeKind::Subtitle);
    Check(subtitle && subtitle->paragraphs.size() == 1 && subtitle->paragraphs[0].runs.size() == 3 &&
              subtitle->paragraphs[0].runs[1].text == "flat" && subtitle->paragraphs[0].runs[1].italic &&
              !subtitle->paragraphs[0].runs[0].italic && subtitle->x == 360000,
          "a common text style's italic, on its run only; 10 mm");
    Check(!AnyText(deck, "Click to add text") && !AnyText(deck, "Footer text"),
          "a prompting placeholder and the footer are left out");

    const PptxShape* body = FindShape(first, PptxShapeKind::Body, "Body");
    Check(body && body->paragraphs.size() == 5, "the outline's five paragraphs");
    if (body && body->paragraphs.size() == 5) {
        const auto& p = body->paragraphs;
        Check(p[0].Text() == "First point" && p[0].bullet && !p[0].numbered && p[0].level == 0 &&
                  p[0].runs.size() == 2 && p[0].runs[0].bold && p[0].runs[0].italic && !p[0].runs[1].bold,
              "a bullet whose first run is bold and, by its parent style, italic");
        Check(p[1].Text() == "Sub one" && p[1].level == 1 && p[1].bullet && p[1].numbered,
              "a nested list takes the enclosing list's style: numbered at level 2");
        Check(p[2].Text() == "No bullet" && p[2].level == 1 && !p[2].bullet, "a list header has no bullet");
        Check(p[3].Text() == "Line\nbreak" && p[3].runs.size() == 3 && p[3].runs[1].text == "\n",
              "a line break inside a paragraph");
        Check(p[4].Text() == "A link\there" && p[4].runs.back().text == "here" && p[4].runs.back().bold &&
                  p[4].runs.back().underline && !p[4].runs.front().underline,
              "a link's text and a tab; weight 700 is bold, underline solid");
    }
    Check(FindShape(first, PptxShapeKind::Text, "Shape") &&
              FindShape(first, PptxShapeKind::Text, "Shape")->paragraphs[0].Text() == "Grouped",
          "a shape inside a group is read");
    Check(FindShape(first, PptxShapeKind::Text, "LinkedBox") != nullptr, "a frame behind a hyperlink is read");
    Check(first.shapes.size() == 6, "a rectangle without text is no shape of the outline (the empty placeholder is)");
    const PptxShape* prompt = nullptr;
    for (const PptxShape& shape : first.shapes)
        if (shape.kind == PptxShapeKind::Body && shape.width == 360000 && shape.height == 360000) prompt = &shape;
    Check(prompt && prompt->placeholder == "body" && prompt->paragraphs.size() == 1 && prompt->paragraphs[0].Text().empty(),
          "a prompting placeholder is kept empty, without its prompt");
    Check(first.notes.size() == 2 && first.notes[0].Text() == "Speaker note one" &&
              first.notes[1].Text() == "Speaker note two",
          "both paragraphs of the speaker notes");

    const PptxSlide& second = deck.slides[1];
    Check(second.hidden && !first.hidden && !deck.slides[2].hidden, "the hidden slide by its drawing-page style");
    Check(second.title == "Hidden", "its title");
    const PptxShape* table = FindShape(second, PptxShapeKind::Table, "Grid");
    Check(table && table->rows.size() == 2 && table->rows[0].size() == 2 && table->rows[0][1][0].Text() == "B" &&
              table->rows[1].size() == 2 && table->rows[1][0][0].Text() == "C" && table->rows[1][1].empty(),
          "header rows and body rows; a covered cell keeps its column, empty");
    const PptxShape* logo = FindShape(second, PptxShapeKind::Picture);
    Check(logo && logo->name == "Logo" && logo->altText == "Company logo" && logo->pictureTarget.empty(),
          "an inline picture: its title as the description, no part");
    Check(second.shapes.size() == 3, "the embedded chart is left out, its replacement picture with it");

    const PptxSlide& third = deck.slides[2];
    Check(third.title.empty(), "a slide without a title");
    const PptxShape* remarks = FindShape(third, PptxShapeKind::Text, "Remarks");
    Check(remarks && remarks->paragraphs.size() == 3 && remarks->paragraphs[0].Text() == "Visible text" &&
              remarks->paragraphs[1].Text() == "Page 3" && remarks->paragraphs[2].Text() == "Numbering off" &&
              !remarks->paragraphs[2].bullet && !remarks->paragraphs[0].bullet,
          "a field's text; a list level with no number format has no label");
    Check(!AnyText(deck, "a comment") && !AnyText(deck, "Reviewer"), "a comment on the page is not slide text");

    const UCRichDocument outline = PptxPresentationToRichDocument(deck);
    const std::string text = outline.ToPlainText();
    Check(Contains(text, "1. Welcome  to ODP") && Contains(text, "2. Hidden (hidden)") && Contains(text, "3. Slide 3") &&
              Contains(text, "[Picture: Company logo]") && Contains(text, "Speaker note two"),
          "the outline");

    PptxPresentation fromMemory;
    Check(ReadOdpPresentationFromMemory(ReadFile(path), fromMemory, error) && fromMemory.slides.size() == 3 &&
              fromMemory.slides[0].title == "Welcome  to ODP",
          "ReadOdpPresentationFromMemory reads a flat file's XML");
}

std::vector<uint8_t> Package(const std::string& mimetype, const std::string& manifest, const std::string& content) {
    UCZipPackageWriter zip;
    zip.OpenInMemory();
    zip.AddEntry("mimetype", mimetype, false);
    zip.AddEntry("META-INF/manifest.xml", manifest);
    zip.AddEntry("content.xml", content);
    zip.Finalize();
    return zip.TakeBytes();
}

void TestRefusals() {
    std::cout << "\n=== What is refused, and why ===\n";
    const std::string mime = "application/vnd.oasis.opendocument.presentation";
    const std::string manifest =
        "<?xml version=\"1.0\"?><manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"" + mime + "\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\">"
        "<manifest:encryption-data manifest:checksum-type=\"SHA1/1K\" manifest:checksum=\"AAAA\">"
        "<manifest:algorithm manifest:algorithm-name=\"http://www.w3.org/2001/04/xmlenc#aes256-cbc\" "
        "manifest:initialisation-vector=\"AAAA\"/>"
        "<manifest:start-key-generation manifest:start-key-generation-name=\"http://www.w3.org/2000/09/xmldsig#sha256\" "
        "manifest:key-size=\"32\"/>"
        "<manifest:key-derivation manifest:key-derivation-name=\"PBKDF2\" manifest:iteration-count=\"100000\" "
        "manifest:salt=\"AAAA\" manifest:key-size=\"32\"/></manifest:encryption-data></manifest:file-entry>"
        "</manifest:manifest>";
    PptxPresentation deck;
    std::string error;
    Check(!ReadOdpPresentationFromMemory(Package(mime, manifest, "\x01\x02 not XML"), deck, error) &&
              Contains(error, "password-protected"),
          "a password-protected deck without its password says so (\"" + error + "\")");

    const std::string content =
        "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\">"
        "<office:body><office:text/></office:body></office:document-content>";
    const std::string plainManifest =
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\"/>";
    Check(!ReadOdpPresentationFromMemory(Package("application/vnd.oasis.opendocument.text", plainManifest, content),
                                         deck, error) &&
              Contains(error, "another kind"),
          "an OpenDocument text is not taken for a deck");
    Check(!ReadOdpPresentationFromMemory(Package(mime, plainManifest, content), deck, error) &&
              Contains(error, "no presentation body"),
          "a package that says presentation but holds none");
    Check(!ReadOdpPresentationFromMemory(std::vector<uint8_t>{'P', 'K', 3, 4, 0, 0}, deck, error) && !error.empty(),
          "a broken package");
    Check(!ReadOdpPresentation(Fixture("does-not-exist.odp"), deck, error) && !error.empty(), "a missing file");
    Check(ReadOdpPresentationFromMemory(Package(mime + "-template", plainManifest,
              "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\">"
              "<office:body><office:presentation/></office:body></office:document-content>"),
                                        deck, error) &&
              deck.slides.empty(),
          "a template (.otp) is a presentation too");
}

void TestFileLoader() {
    std::cout << "\n=== UltraCanvasFileLoader and UltraFiler ===\n";
    std::string error;
    auto odp = UltraCanvasFileLoader::LoadTextDocument(Fixture("office-pptx-sample.odp"), error);
    Check(odp && Contains(odp->ToPlainText(), "2. Agenda") && Contains(odp->ToPlainText(), "Item 60"),
          "LoadTextDocument opens an .odp as the outline of its slides");
    auto fodp = UltraCanvasFileLoader::LoadTextDocument(Fixture("presentation-features.fodp"), error);
    Check(fodp && Contains(fodp->ToPlainText(), "Welcome  to ODP"), "... and a flat .fodp");

    // A deck's name on a file that is no deck is not shown as text.
    const fs::path fake = fs::temp_directory_path() / "uc-odp-test-not-a-deck.odp";
    {
        std::ofstream out(fake, std::ios::binary);
        out << "just some text\n";
    }
    auto notDeck = UltraCanvasFileLoader::LoadTextDocument(PathToUtf8(fake), error);
    Check(!notDeck && Contains(error, "not a valid OpenDocument presentation"),
          "an .odp that is no deck is refused, not read as text");
    const fs::path fakePptx = fs::temp_directory_path() / "uc-odp-test-not-a-deck.pptx";
    fs::copy_file(fake, fakePptx, fs::copy_options::overwrite_existing);
    auto notPptx = UltraCanvasFileLoader::LoadTextDocument(PathToUtf8(fakePptx), error);
    Check(!notPptx && Contains(error, "not a valid PowerPoint presentation"), "... and so is such a .pptx");
    std::error_code ignored;
    fs::remove(fake, ignored);
    fs::remove(fakePptx, ignored);

    FileDialogOptions open;
    UltraCanvasFileLoader::AddTextDocumentLoadTypes(open);
    const FileFilter* docs = open.filters.empty() ? nullptr : &open.filters.front();
    auto holds = [&](const char* ext) {
        return docs && std::find(docs->extensions.begin(), docs->extensions.end(), ext) != docs->extensions.end();
    };
    Check(holds("odp") && holds("otp") && holds("fodp"), "the Open dialog's \"Documents\" offer .odp, .otp, .fodp");
    Check(holds("pptx") && holds("ppsm") && holds("potm"), "... and every PowerPoint extension the reader takes");

    Check(UltraCanvasFileLoader::GetTextExtractionMethod("deck.odp") ==
              UltraCanvasFileLoader::TextExtractionMethod::Document,
          "an .odp's text is extracted as a document's");
    const auto extracted = UltraCanvasFileLoader::ExtractText(Fixture("office-pptx-sample.odp"));
    Check(extracted.Ok() && Contains(extracted.text, "Remember the Q3 numbers."), "ExtractText reads it");

    std::vector<std::string> lines;
    bool tabular = true;
    Check(UltraCanvasFilerWidget::TextPreviewLines(Fixture("office-pptx-sample.odp"), lines, tabular) && !tabular &&
              !lines.empty() && lines.front() == "1. Quarterly Review" &&
              std::find(lines.begin(), lines.end(), "Revenue up") != lines.end(),
          "UltraFiler's preview page shows an .odp's slides");
    const bool flat = UltraCanvasFilerWidget::TextPreviewLines(Fixture("presentation-features.fodp"), lines, tabular);
    // (The preview page lays its lines out with single spaces.)
    Check(flat && !lines.empty() && lines.front() == "1. Welcome to ODP",
          "... and a .fodp's (\"" + (lines.empty() ? std::string() : lines.front()) + "\")");
}

// The look as drawn, from a flat deck written here: style parents and the
// family defaults, a share of the size under it, font faces, ODF's own shape
// types, a turn, a mirrored enhanced path, a rounded rectangle, an ellipse, a
// line with a marker, a polyline, an SVG path, a regular polygon, a dotted
// outline, radial and linear gradients (LibreOffice's bare tenths of a
// degree), numbering placed by label alignment and continued across lists, a
// link to a page by its name, a table template, and backgrounds from the page
// and from its master page.
const char* kLookDeck = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 xmlns:xlink="http://www.w3.org/1999/xlink"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:presentation="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0"
 office:version="1.3" office:mimetype="application/vnd.oasis.opendocument.presentation">
 <office:font-face-decls>
  <style:font-face style:name="Body" svg:font-family="'Liberation Sans'"/>
 </office:font-face-decls>
 <office:styles>
  <style:default-style style:family="graphic">
   <style:graphic-properties draw:fill="solid" draw:fill-color="#729fcf" draw:stroke="solid" svg:stroke-color="#3465a4"/>
   <style:text-properties fo:font-size="20pt" fo:color="#333333" style:font-name="Body"/>
  </style:default-style>
  <style:style style:name="Base" style:family="graphic">
   <style:graphic-properties draw:fill="solid" draw:fill-color="#112233" draw:stroke="none" draw:textarea-vertical-align="bottom"/>
  </style:style>
  <style:style style:name="Child" style:family="graphic" style:parent-style-name="Base">
   <style:graphic-properties draw:stroke="dash" draw:stroke-dash="Dots" svg:stroke-color="#ff0000" svg:stroke-width="0.1cm"/>
   <style:text-properties fo:font-size="150%"/>
  </style:style>
  <draw:stroke-dash draw:name="Dots" draw:style="round" draw:dots1="1" draw:dots1-length="0.02cm" draw:distance="0.05cm"/>
  <draw:gradient draw:name="Radial" draw:style="radial" draw:start-color="#000000" draw:end-color="#ffffff" draw:angle="0"/>
  <draw:gradient draw:name="Tenths" draw:style="linear" draw:start-color="#ff0000" draw:end-color="#00ff00" draw:angle="900"/>
  <draw:marker draw:name="Arrow" svg:viewBox="0 0 20 30" svg:d="M10 0l-10 30h20z"/>
  <style:style style:name="HeadCell" style:family="table-cell">
   <style:table-cell-properties fo:background-color="#203040"/>
   <style:text-properties fo:color="#ffffff" fo:font-weight="bold"/>
  </style:style>
  <style:style style:name="BodyCell" style:family="table-cell">
   <style:table-cell-properties fo:background-color="#eeeeee" fo:border="0.05cm solid #000000" style:vertical-align="middle" fo:padding="0.2cm"/>
  </style:style>
  <table:table-template table:name="Banded">
   <table:first-row table:style-name="HeadCell"/>
   <table:body table:style-name="BodyCell"/>
  </table:table-template>
  <text:list-style style:name="Num">
   <text:list-level-style-number text:level="1" style:num-format="I" style:num-suffix="." text:start-value="2">
    <style:list-level-properties text:list-level-position-and-space-mode="label-alignment">
     <style:list-level-label-alignment fo:margin-left="1.27cm" fo:text-indent="-0.635cm"/>
    </style:list-level-properties>
   </text:list-level-style-number>
  </text:list-style>
 </office:styles>
 <office:automatic-styles>
  <style:page-layout style:name="PM1">
   <style:page-layout-properties fo:page-width="25.4cm" fo:page-height="19.05cm"/>
  </style:page-layout>
  <style:style style:name="dpMaster" style:family="drawing-page">
   <style:drawing-page-properties draw:fill="solid" draw:fill-color="#fafad2"/>
  </style:style>
  <style:style style:name="dp1" style:family="drawing-page"><style:drawing-page-properties/></style:style>
  <style:style style:name="dp2" style:family="drawing-page">
   <style:drawing-page-properties draw:fill="solid" draw:fill-color="#000080"/>
  </style:style>
  <style:style style:name="gr1" style:family="graphic" style:parent-style-name="Child"><style:graphic-properties/></style:style>
  <style:style style:name="gr2" style:family="graphic">
   <style:graphic-properties draw:fill="gradient" draw:fill-gradient-name="Radial"/>
  </style:style>
  <style:style style:name="gr3" style:family="graphic">
   <style:graphic-properties draw:fill="gradient" draw:fill-gradient-name="Tenths" draw:stroke="none"/>
  </style:style>
  <style:style style:name="gr4" style:family="graphic">
   <style:graphic-properties draw:fill="none" draw:stroke="solid" svg:stroke-color="#00ff00" draw:marker-start="Arrow"/>
  </style:style>
  <style:style style:name="gr5" style:family="graphic">
   <style:graphic-properties draw:fill="none" draw:stroke="none"/>
  </style:style>
  <style:style style:name="T1" style:family="text"><style:text-properties style:use-window-font-color="true"/></style:style>
 </office:automatic-styles>
 <office:master-styles>
  <style:master-page style:name="M" style:page-layout-name="PM1" draw:style-name="dpMaster"/>
 </office:master-styles>
 <office:body>
  <office:presentation>
   <draw:page draw:name="First" draw:style-name="dp1" draw:master-page-name="M">
    <draw:custom-shape draw:name="Oval" draw:style-name="gr1" svg:x="1cm" svg:y="1cm" svg:width="4cm" svg:height="2cm">
     <text:p>Big</text:p>
     <draw:enhanced-geometry svg:viewBox="0 0 21600 21600" draw:type="ellipse"/>
    </draw:custom-shape>
    <draw:custom-shape draw:name="Tri" svg:width="4cm" svg:height="2cm" draw:transform="rotate (1.5707963267949) translate (2cm 8cm)">
     <draw:enhanced-geometry svg:viewBox="0 0 21600 21600" draw:type="isosceles-triangle"/>
    </draw:custom-shape>
    <draw:custom-shape draw:name="Free" draw:style-name="gr3" svg:x="6cm" svg:y="1cm" svg:width="2cm" svg:height="2cm">
     <draw:enhanced-geometry svg:viewBox="0 0 21600 21600" draw:type="non-primitive" draw:mirror-vertical="true"
      draw:enhanced-path="M 0 0 L 21600 0 10800 21600 Z N"/>
    </draw:custom-shape>
    <draw:rect draw:name="Rounded" draw:style-name="gr2" draw:corner-radius="0.5cm" svg:x="9cm" svg:y="1cm" svg:width="3cm" svg:height="2cm"/>
    <draw:ellipse draw:name="Disc" svg:x="13cm" svg:y="1cm" svg:width="2cm" svg:height="2cm"/>
    <draw:line draw:name="Line" draw:style-name="gr4" svg:x1="10cm" svg:y1="5cm" svg:x2="6cm" svg:y2="7cm"/>
    <draw:polyline draw:name="Zig" draw:style-name="gr4" svg:x="1cm" svg:y="12cm" svg:width="4cm" svg:height="2cm"
     svg:viewBox="0 0 100 100" draw:points="0,0 50,100 100,0"/>
    <draw:path draw:name="Blob" svg:x="6cm" svg:y="12cm" svg:width="4cm" svg:height="2cm" svg:viewBox="0 0 200 100"
     svg:d="M0 0 h200 v100 q-100 -50 -200 0 z"/>
    <draw:regular-polygon draw:name="Hex" draw:corners="6" svg:x="11cm" svg:y="12cm" svg:width="2cm" svg:height="2cm"/>
    <draw:frame draw:name="Listed" draw:style-name="gr5" svg:x="14cm" svg:y="4cm" svg:width="8cm" svg:height="6cm">
     <draw:text-box>
      <text:list text:style-name="Num">
       <text:list-item><text:p>Two <text:span text:style-name="T1">auto</text:span></text:p></text:list-item>
       <text:list-item><text:p>Three</text:p></text:list-item>
      </text:list>
      <text:p>Go to <text:a xlink:type="simple" xlink:href="#Second">the second</text:a></text:p>
      <text:list text:style-name="Num" text:continue-numbering="true">
       <text:list-item><text:p>Four</text:p></text:list-item>
      </text:list>
     </draw:text-box>
    </draw:frame>
    <draw:frame draw:name="Grid" svg:x="1cm" svg:y="15cm" svg:width="6cm" svg:height="2cm">
     <table:table table:template-name="Banded" table:use-first-row-styles="true">
      <table:table-column table:number-columns-repeated="2" table:style-name="co1"/>
      <table:table-row><table:table-cell><text:p>H1</text:p></table:table-cell><table:table-cell><text:p>H2</text:p></table:table-cell></table:table-row>
      <table:table-row><table:table-cell><text:p>B1</text:p></table:table-cell><table:table-cell><text:p>B2</text:p></table:table-cell></table:table-row>
     </table:table>
    </draw:frame>
   </draw:page>
   <draw:page draw:name="Second" draw:style-name="dp2" draw:master-page-name="M"/>
  </office:presentation>
 </office:body>
</office:document>
)XML";

void TestLookByHand() {
    std::cout << "\n=== The look as drawn (a flat deck written here) ===\n";
    PptxPresentation deck;
    std::string error;
    const std::string xml = kLookDeck;
    if (!ReadOdpPresentationFromMemory(std::vector<uint8_t>(xml.begin(), xml.end()), deck, error) ||
        deck.slides.size() != 2) {
        Check(false, "the deck reads (" + error + ")");
        return;
    }
    const PptxSlide& first = deck.slides[0];
    Check(first.background == "FAFAD2" && deck.slides[1].background == "000080",
          "backgrounds: the master page's where the page says none, else the page's");
    auto named = [&](const char* name) -> const PptxShape* {
        for (const PptxShape& shape : first.shapes)
            if (shape.name == name) return &shape;
        return nullptr;
    };
    const PptxShape* oval = named("Oval");
    Check(oval && oval->geometry == "ellipse" && oval->fill == "112233" && oval->line == "FF0000" &&
              std::abs(oval->lineWidthPt - 2.835) < 0.01 && oval->dash == RichShape::Dash::Dotted &&
              oval->anchor == PptxAnchor::Bottom,
          "a style through its parent: the parent's fill and anchor, its own dotted red outline");
    Check(oval && !oval->paragraphs.empty() && !oval->paragraphs[0].runs.empty() &&
              oval->paragraphs[0].runs[0].sizePt == 30.0f && oval->paragraphs[0].runs[0].font == "Liberation Sans" &&
              oval->paragraphs[0].runs[0].color == "333333",
          "text: 150 % of the default's 20 pt, the default's font face and colour");
    const PptxShape* tri = named("Tri");
    Check(tri && tri->geometry == "triangle" && std::abs(tri->rotationDeg - 270.0) < 0.01 && Near(tri->x, 360000) &&
              Near(tri->y, 1800000) && tri->width == 1440000 && tri->fill == "729FCF" && tri->line == "3465A4",
          "an ODF type as its preset, turned a quarter counter-clockwise about its corner, the defaults' fill and line");
    const PptxShape* free = named("Free");
    Check(free && free->customPath.size() == 4 && free->customPath[2].x == 0.5f && free->customPath[2].y == 1.0f &&
              free->flipV && free->gradientStops.size() == 2 && free->gradientStops[0].color == "FF0000" &&
              std::abs(free->gradientAngleDeg) < 0.01 && free->line.empty(),
          "an enhanced path, mirrored; a gradient at LibreOffice's bare tenths of a degree (900: left to right)");
    const PptxShape* rounded = named("Rounded");
    Check(rounded && rounded->geometry == "roundRect" && rounded->gradientRadial && rounded->gradientStops.size() == 2 &&
              rounded->gradientStops[0].color == "FFFFFF" && rounded->gradientStops[1].color == "000000",
          "a rounded rectangle; a radial gradient from the centre (ODF's end colour) out");
    const PptxShape* disc = named("Disc");
    Check(disc && disc->geometry == "ellipse", "an ellipse");
    const PptxShape* line = named("Line");
    Check(line && line->kind == PptxShapeKind::Connector && line->x == 2160000 && line->width == 1440000 &&
              line->flipH && !line->flipV && line->arrowStart && !line->arrowEnd && line->line == "00FF00",
          "a line drawn right to left: its box, mirrored, an arrowhead at its start");
    const PptxShape* zig = named("Zig");
    Check(zig && zig->kind == PptxShapeKind::Connector && zig->customPath.size() == 3 && zig->customPath[1].x == 0.5f &&
              zig->customPath[1].y == 1.0f,
          "a polyline: an open path, a line");
    const PptxShape* blob = named("Blob");
    Check(blob && blob->kind == PptxShapeKind::Text && blob->customPath.size() == 5 &&
              blob->customPath[1].x == 1.0f && blob->customPath[3].op == RichShape::PathPoint::Op::Curve &&
              blob->customPath[3].x == 0.0f && blob->customPath[3].y == 1.0f,
          "an SVG path: relative lines and a quadratic curve, closed");
    const PptxShape* hex = named("Hex");
    Check(hex && hex->customPath.size() == 7, "a regular polygon: its six corners");
    const PptxShape* listed = named("Listed");
    if (!listed || listed->paragraphs.size() != 4) {
        Check(false, "the list box: four paragraphs");
    } else {
        const auto& p = listed->paragraphs;
        Check(p[0].numbered && p[0].numberScheme == "romanUcPeriod" &&
                  PptxNumberLabel(p[0].numberScheme, p[0].numberStartAt) == "II." &&
                  PptxNumberLabel(p[1].numberScheme, p[1].numberStartAt + 1) == "III." &&
                  PptxNumberLabel(p[3].numberScheme, p[3].numberStartAt) == "IV." && p[0].marginLeft == 457200 &&
                  p[0].indent == -228600 && !p[2].bullet,
              "numbering from II., placed by label alignment, continued across lists");
        Check(p[0].runs.size() == 2 && p[0].runs[0].color == "333333" && p[0].runs[1].color.empty(),
              "the window's text colour: no colour of its own");
        Check(p[2].runs.size() == 2 && p[2].runs[1].hyperlink == "#slide2", "a link to a page by its name: to its slide");
    }
    const PptxShape* grid = FindShape(first, PptxShapeKind::Table, "Grid");
    if (!grid || grid->cellStyles.size() != 2 || grid->cellStyles[1].size() != 2) {
        Check(false, "the table: two rows of two");
    } else {
        Check(grid->cellStyles[0][0].fill == "203040" && grid->rows[0][0][0].runs[0].color == "FFFFFF" &&
                  grid->rows[0][0][0].runs[0].bold,
              "a table template's first row: its fill, white bold text");
        const PptxCellStyle& body = grid->cellStyles[1][1];
        Check(body.fill == "EEEEEE" && body.left.color == "000000" && std::abs(body.bottom.widthPt - 1.417) < 0.01 &&
                  body.anchor == PptxAnchor::Middle && body.insetLeft == 72000 && grid->rows[1][1][0].runs[0].color == "333333",
              "... its body: fill, borders, anchor and padding from the table cell properties");
    }
}

} // namespace

int main() {
    TestLibreOfficeDeck();
    TestFlatDeck();
    TestLookByHand();
    TestRefusals();
    TestFileLoader();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? "" : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
