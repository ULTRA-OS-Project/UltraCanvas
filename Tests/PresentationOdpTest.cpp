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
//     shapes, slide furniture and prompts left out, a hidden slide, a table
//     with a covered cell, an embedded object left out, an inline picture, a
//     page comment left out
//   - a password-protected package and other ODF types refused with a reason
//   - UltraCanvasFileLoader (LoadTextDocument, the Open dialog's types, text
//     extraction) and UltraFiler's preview page take the new types
// Version: 1.0.1 - the protected package's manifest is a whole one (it now opens with a password;
//                  OdfEncryptionTest reads real ones)
// Version: 1.0.0
// Last Modified: 2026-11-18
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
    Check(first.shapes.size() == 5, "a rectangle without text is no shape of the outline");
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

} // namespace

int main() {
    TestLibreOfficeDeck();
    TestFlatDeck();
    TestRefusals();
    TestFileLoader();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? "" : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
