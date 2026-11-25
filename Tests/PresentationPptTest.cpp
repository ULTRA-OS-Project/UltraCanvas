// Tests/PresentationPptTest.cpp
// The PowerPoint 97-2003 (.ppt) reader (UltraCanvasPresentationPpt.h):
//   * Tests/fixtures/ppt-features.ppt (python-pptx, then LibreOffice's "MS
//     PowerPoint 97" filter; make-ppt-fixtures.py): the slide size, a title
//     slide with its subtitle and a fade, bullets with a bold run, an italic
//     second level, a 32 pt run and a numbered pair, speaker notes, a push
//     after three seconds; a red rectangle where it was put, a turned and
//     mirrored star (a freeform), a described picture with its bytes, a
//     table; a hidden slide;
//   * Tests/fixtures/office-pptx-sample.ppt: the python-pptx sample on 16:9,
//     with its table, notes, hidden slide and sixty bullets;
//   * a deck built here record by record, as PowerPoint writes one: text
//     kept in the slide list (OutlineTextRefAtom), a title placeholder that
//     takes its place and its look (size, bold, typeface, a scheme colour,
//     centred) from its master, bullets by level from the master's body
//     style, a scheme-coloured run with a hyperlink, a wipe - and saved
//     twice, so the second edit's slide is the one read (the UserEditAtom
//     chain), with and without the Current User stream; encrypted, refused;
//   * damaged files - every truncation of the built deck's stream, cut and
//     bit-flipped fixtures - read without a crash or a hang;
//   * UltraCanvasFileLoader: the kind a .ppt is (also renamed), LoadPresentation,
//     the outline; the slides drawn; the deck written as .pptx and read back.
// UC_PPT_TEST_DUMP=<dir> writes the drawn slides there as .ppm.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasPresentationPpt.h"
#include "UltraCanvasPresentationPptx.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

std::string Fixture(const char* name) { return std::string(PPTTEST_FIXTURE_DIR) + "/" + name; }

using Bytes = std::vector<uint8_t>;

Bytes ReadFileBytes(const std::string& path) {
    std::ifstream in(PathFromUtf8(path), std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const PptxShape* OfKind(const PptxSlide& slide, PptxShapeKind kind) {
    for (const PptxShape& shape : slide.shapes)
        if (shape.kind == kind) return &shape;
    return nullptr;
}

const PptxShape* WithText(const PptxSlide& slide, const std::string& text) {
    for (const PptxShape& shape : slide.shapes)
        for (const PptxParagraph& p : shape.paragraphs)
            if (Contains(p.Text(), text)) return &shape;
    return nullptr;
}

const PptxRun* RunWith(const PptxParagraph& p, const std::string& text) {
    for (const PptxRun& run : p.runs)
        if (run.text == text) return &run;
    return nullptr;
}

bool Close(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

void Dump(const UCPixmap& pixmap, const std::string& name) {
    const char* dir = std::getenv("UC_PPT_TEST_DUMP");
    if (!dir || !*dir) return;
    std::ofstream out(PathFromUtf8(std::string(dir) + "/" + name + ".ppm"), std::ios::binary);
    out << "P6\n" << pixmap.GetRawWidth() << " " << pixmap.GetRawHeight() << "\n255\n";
    for (int y = 0; y < pixmap.GetRawHeight(); ++y)
        for (int x = 0; x < pixmap.GetRawWidth(); ++x) {
            const uint32_t argb = pixmap.GetPixel(x, y);
            out.put(static_cast<char>((argb >> 16) & 0xFF)).put(static_cast<char>((argb >> 8) & 0xFF))
               .put(static_cast<char>(argb & 0xFF));
        }
}

// How many pixels in the rectangle are close to a colour.
int Coloured(const UCPixmap& pixmap, int x0, int y0, int x1, int y1, int r, int g, int b, int tolerance = 50) {
    int count = 0;
    for (int y = std::max(0, y0); y < std::min(y1, pixmap.GetRawHeight()); ++y)
        for (int x = std::max(0, x0); x < std::min(x1, pixmap.GetRawWidth()); ++x) {
            const uint32_t argb = pixmap.GetPixel(x, y);
            if (std::abs(static_cast<int>((argb >> 16) & 0xFF) - r) <= tolerance &&
                std::abs(static_cast<int>((argb >> 8) & 0xFF) - g) <= tolerance &&
                std::abs(static_cast<int>(argb & 0xFF) - b) <= tolerance)
                ++count;
        }
    return count;
}

// ----- LibreOffice's .ppt of the features deck -----

void TestFeatures() {
    std::cout << "\n=== ppt-features.ppt (LibreOffice's MS PowerPoint 97) ===\n";
    PptxPresentation deck;
    std::string error;
    const bool read = ReadPptPresentation(Fixture("ppt-features.ppt"), deck, error, true);
    Check(read, "read (" + error + ")");
    if (!read) return;
    Check(deck.slides.size() == 4, "four slides (" + std::to_string(deck.slides.size()) + ")");
    Check(deck.slideWidth == 9144000 && deck.slideHeight == 6858000,
          "the slide is 10\" x 7.5\" (" + std::to_string(deck.slideWidth) + " x " + std::to_string(deck.slideHeight) + ")");
    Check(!deck.masters.empty() && !deck.layouts.empty(), "masters and layouts read");
    if (deck.slides.size() != 4) return;

    const PptxSlide& first = deck.slides[0];
    Check(first.title == "Binary Deck", "slide 1's title (" + first.title + ")");
    const PptxShape* subtitle = OfKind(first, PptxShapeKind::Subtitle);
    Check(subtitle && subtitle->paragraphs.size() == 1 && subtitle->paragraphs[0].Text() == "PowerPoint 97-2003",
          "the subtitle placeholder and its text");
    Check(first.transition.kind == PptxTransitionKind::Fade && Close(first.transition.durationSec, 1.0, 0.01),
          "slide 1 fades on, slowly");
    Check(first.layoutIndex >= 0 && first.layoutIndex < static_cast<int>(deck.layouts.size()), "slide 1 on a layout");

    const PptxSlide& agenda = deck.slides[1];
    Check(agenda.title == "Agenda", "slide 2's title");
    const PptxShape* body = WithText(agenda, "First point");
    Check(body != nullptr, "the bulleted text");
    if (body && body->paragraphs.size() >= 5) {
        const PptxParagraph& p0 = body->paragraphs[0];
        const PptxRun* bold = RunWith(p0, "First");
        const PptxRun* plain = RunWith(p0, " point");
        Check(bold && bold->bold && plain && !plain->bold, "\"First\" bold, \" point\" not");
        Check(p0.bullet && !p0.bulletChar.empty() && p0.level == 0, "a bullet at the first level");
        const PptxParagraph& p1 = body->paragraphs[1];
        Check(p1.level == 1 && !p1.runs.empty() && p1.runs[0].italic && p1.Text() == "Detail in italic",
              "the second level, in italic");
        Check(p1.runs.empty() || p1.runs[0].sizePt < p0.runs[0].sizePt, "the second level smaller than the first");
        const PptxParagraph& p2 = body->paragraphs[2];
        Check(!p2.runs.empty() && Close(p2.runs[0].sizePt, 32.0, 0.01), "the 32 pt run");
        Check(body->paragraphs[3].numbered && body->paragraphs[3].numberScheme == "arabicPeriod" &&
                  body->paragraphs[4].numbered && body->paragraphs[3].Text() == "One",
              "the numbered pair: arabic, with a period");
        Check(!p0.runs.empty() && !p0.runs[0].font.empty() && !p0.runs[0].color.empty(),
              "the runs' typeface and colour resolved (" + (p0.runs.empty() ? "" : p0.runs[0].font) + ")");
    } else {
        Check(false, "five paragraphs in the body");
    }
    Check(agenda.notes.size() == 1 && agenda.notes[0].Text() == "Speaker notes here", "slide 2's speaker notes");
    Check(agenda.transition.kind == PptxTransitionKind::Push && Close(agenda.transition.durationSec, 0.5, 0.01),
          "slide 2 is pushed on, fast");
    Check(Close(agenda.transition.advanceAfterSec, 3.0, 0.1), "... and moves on after three seconds (" +
          std::to_string(agenda.transition.advanceAfterSec) + ")");

    const PptxSlide& shapes = deck.slides[2];
    const PptxShape* box = WithText(shapes, "Red box");
    Check(box && box->x == 914400 && box->y == 1828800 && box->width == 2743200 && box->height == 914400,
          "the rectangle at (1\", 2\"), 3\" x 1\"");
    Check(box && box->fill == "FF0000" && box->line == "000080", "filled red, outlined navy");
    const PptxShape* star = nullptr;
    for (const PptxShape& shape : shapes.shapes)
        if (shape.flipH) star = &shape;
    // LibreOffice writes the star as an outline of formulas (pGuides), which
    // the reader leaves out: the shape keeps its box, turn, mirroring and fill.
    Check(star && Close(star->rotationDeg, 30.0, 0.5) && star->y == 3657600 && star->width == 1828800,
          "the star turned 30 degrees and mirrored, in its place");
    Check(star && star->gradientStops.size() >= 2 && star->customPath.empty(),
          "its shade as a gradient; its outline of formulas left out");
    const PptxShape* picture = OfKind(shapes, PptxShapeKind::Picture);
    // (LibreOffice's export keeps no description: the built deck below has one.)
    Check(picture && !picture->pictureTarget.empty(), "the picture");
    if (picture) {
        const auto bytes = deck.pictures.find(picture->pictureTarget);
        Check(bytes != deck.pictures.end() && bytes->second.size() > 8 && bytes->second[1] == 'P',
              "its PNG's bytes kept (" + picture->pictureTarget + ")");
        Check(picture->x == 5486400 && picture->width == 1828800, "the picture at 6\", 2\" wide");
    }
    const PptxShape* table = OfKind(shapes, PptxShapeKind::Table);
    Check(table && table->rows.size() == 2 && table->rows[0].size() == 2, "the 2 x 2 table read as a table");
    if (table && table->rows.size() == 2 && table->rows[0].size() == 2) {
        auto cell = [&](size_t r, size_t c) {
            std::string text;
            for (const PptxParagraph& p : table->rows[r][c]) text += p.Text();
            return text;
        };
        Check(cell(0, 0) == "Name" && cell(0, 1) == "Value" && cell(1, 0) == "Alpha" && cell(1, 1) == "42",
              "its cells' text in place");
        Check(table->columnWidths.size() == 2 && table->rowHeights.size() == 2, "its grid");
        Check(!table->cellStyles[0][0].fill.empty(), "the header cell's fill");
    }
    Check(deck.slides[3].hidden && !deck.slides[0].hidden, "slide 4 hidden, slide 1 not");

    // Drawn: 1000 pixels for 10 inches.
    for (size_t i = 0; i < deck.slides.size(); ++i) {
        const auto pixmap = RenderPptxSlide(deck, i, 1000);
        Check(pixmap && pixmap->GetRawWidth() == 1000 && pixmap->GetRawHeight() == 750,
              "slide " + std::to_string(i + 1) + " drawn");
        if (pixmap) Dump(*pixmap, "ppt-features-" + std::to_string(i + 1));
        if (pixmap && i == 2) {
            Check(Coloured(*pixmap, 120, 210, 380, 290, 0xFF, 0x00, 0x00) > 5000, "the red rectangle drawn");
            Check(Coloured(*pixmap, 620, 220, 780, 380, 40, 90, 160, 30) > 5000, "the picture drawn");
        }
    }

    // The outline, and the deck as .pptx and back.
    const UCRichDocument outline = PptxPresentationToRichDocument(deck);
    Check(Contains(outline.ToPlainText(), "Agenda") && Contains(outline.ToPlainText(), "(hidden)"),
          "the outline names the slides");
    Bytes package;
    std::string writeError;
    PptxPresentation back;
    const bool written = WritePptxPresentationToPackage(deck, package, writeError);
    Check(written && ReadPptxPresentationFromPackage(package, back, writeError, true) &&
              back.slides.size() == deck.slides.size() && back.slides[1].notes.size() == 1 &&
              back.slides[3].hidden,
          "saved as .pptx and read back: slides, notes, hidden (" + writeError + ")");
}

void TestSample() {
    std::cout << "\n=== office-pptx-sample.ppt ===\n";
    PptxPresentation deck;
    std::string error;
    const bool read = ReadPptPresentation(Fixture("office-pptx-sample.ppt"), deck, error);
    Check(read, "read (" + error + ")");
    if (!read) return;
    Check(deck.slides.size() == 5, "five slides");
    Check(deck.slideWidth == 12192000 && deck.slideHeight == 6858000, "16:9");
    if (deck.slides.size() != 5) return;
    Check(deck.slides[0].title == "Quarterly Review" && deck.slides[1].title == "Agenda", "the titles");
    Check(deck.slides[1].notes.size() == 1 && deck.slides[1].notes[0].Text() == "Remember the Q3 numbers.", "the notes");
    Check(deck.slides[3].hidden && !deck.slides[4].hidden, "the hidden slide");
    const PptxShape* items = WithText(deck.slides[4], "Item 60");
    Check(items && items->paragraphs.size() == 60, "sixty bullets");
    const PptxShape* table = OfKind(deck.slides[2], PptxShapeKind::Table);
    Check(table && table->rows.size() == 2 && !table->rows[1][1].empty() && table->rows[1][1][0].Text() == "1.2M",
          "the table");
    Check(WithText(deck.slides[1], "Confidential") != nullptr, "the text box");
}

// ----- A deck built record by record -----

void Put16(Bytes& b, uint32_t v) { b.push_back(static_cast<uint8_t>(v)); b.push_back(static_cast<uint8_t>(v >> 8)); }
void Put32(Bytes& b, uint32_t v) { Put16(b, v & 0xFFFF); Put16(b, v >> 16); }
Bytes Record(uint16_t type, uint16_t instance, uint8_t version, const Bytes& body) {
    Bytes out;
    Put16(out, static_cast<uint32_t>(version | (instance << 4)));
    Put16(out, type);
    Put32(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
Bytes Container(uint16_t type, std::initializer_list<Bytes> children, uint16_t instance = 0) {
    Bytes body;
    for (const Bytes& child : children) body.insert(body.end(), child.begin(), child.end());
    return Record(type, instance, 0x0F, body);
}
Bytes Utf16(const std::string& ascii) {
    Bytes out;
    for (char c : ascii) Put16(out, static_cast<uint8_t>(c));
    return out;
}
Bytes FontEntity(const std::string& name, uint16_t index) {
    Bytes body = Utf16(name);
    body.resize(68, 0);
    return Record(0x0FB7, index, 0, body);
}
Bytes TextHeader(uint32_t type) { Bytes b; Put32(b, type); return Record(0x0F9F, 0, 0, b); }
Bytes TextChars(const std::string& text) { return Record(0x0FA0, 0, 0, Utf16(text)); }
Bytes TextBytes(const std::string& text) { return Record(0x0FA8, 0, 0, Bytes(text.begin(), text.end())); }
Bytes SlidePersist(uint32_t persistId, uint32_t slideId) {
    Bytes b;
    Put32(b, persistId);
    Put32(b, 0);
    Put32(b, 0);
    Put32(b, slideId);
    Put32(b, 0);
    return Record(0x03F3, 0, 0, b);
}
Bytes SlideAtom(uint32_t geom, uint32_t masterId, uint16_t flags) {
    Bytes b;
    Put32(b, geom);
    for (int i = 0; i < 8; ++i) b.push_back(0);
    Put32(b, masterId);
    Put32(b, 0);
    Put16(b, flags);
    Put16(b, 0);
    return Record(0x03EF, 0, 2, b);
}
// A shape: its FSP, an optional 8-byte anchor (top, left, right, bottom),
// its client data and text box.
Bytes Shape(uint16_t type, uint32_t spid, uint32_t flags, const int* anchor, int placeholder, const Bytes& text) {
    Bytes fsp;
    Put32(fsp, spid);
    Put32(fsp, flags);
    Bytes body = Record(0xF00A, type, 2, fsp);
    if (anchor) {
        Bytes a;
        for (int i = 0; i < 4; ++i) Put16(a, static_cast<uint16_t>(anchor[i]));
        const Bytes r = Record(0xF010, 0, 0, a);
        body.insert(body.end(), r.begin(), r.end());
    }
    if (placeholder >= 0) {
        Bytes ph;
        Put32(ph, 0);
        ph.push_back(static_cast<uint8_t>(placeholder));
        ph.push_back(0);
        Put16(ph, 0);
        const Bytes r = Container(0xF011, {Record(0x0BC3, 0, 0, ph)});
        body.insert(body.end(), r.begin(), r.end());
    }
    if (!text.empty()) {
        const Bytes r = Record(0xF00D, 0, 0x0F, text);
        body.insert(body.end(), r.begin(), r.end());
    }
    return Record(0xF004, 0, 0x0F, body);
}
Bytes Drawing(std::initializer_list<Bytes> shapes) {
    Bytes fspgr(16, 0);
    Bytes fsp;
    Put32(fsp, 0x400);
    Put32(fsp, 0x5);                                // group, patriarch
    Bytes group = Container(0xF004, {Record(0xF009, 0, 1, fspgr), Record(0xF00A, 0, 2, fsp)});
    for (const Bytes& s : shapes) group.insert(group.end(), s.begin(), s.end());
    return Container(0x040C, {Container(0xF002, {Record(0xF003, 0, 0x0F, group)})});
}
Bytes OutlineRef(uint32_t index) { Bytes b; Put32(b, index); return Record(0x0F9E, 0, 0, b); }

// A 1 x 1 PNG.
const Bytes kPng = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
                    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
                    0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
                    0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
                    0x42, 0x60, 0x82};

// The document's picture store: one PNG, embedded in its FBSE.
Bytes PictureStore() {
    Bytes blipBody(17, 0);                          // its UID and tag
    blipBody.insert(blipBody.end(), kPng.begin(), kPng.end());
    const Bytes blip = Record(0xF01E, 0x6E0, 0, blipBody);
    Bytes fbse;
    fbse.push_back(6);                              // PNG
    fbse.push_back(6);
    for (int i = 0; i < 16; ++i) fbse.push_back(static_cast<uint8_t>(i));
    Put16(fbse, 0);
    Put32(fbse, static_cast<uint32_t>(blip.size()));
    Put32(fbse, 1);                                 // cRef
    Put32(fbse, 0);                                 // foDelay
    for (int i = 0; i < 4; ++i) fbse.push_back(0);  // cbName 0
    fbse.insert(fbse.end(), blip.begin(), blip.end());
    return Container(0x040B, {Container(0xF000, {Container(0xF001, {Record(0xF007, 6, 2, fbse)}, 1)})});
}

// A picture frame showing the store's first picture, with a description.
Bytes PictureShape(const int* anchor, const std::string& description) {
    Bytes fsp;
    Put32(fsp, 0x804);
    Put32(fsp, 0xA00);
    Bytes text = Utf16(description);
    Put16(text, 0);
    Bytes fopt;
    Put16(fopt, 0x4104);                            // pib, a BStore index
    Put32(fopt, 1);
    Put16(fopt, 0x8381);                            // wzDescription, complex
    Put32(fopt, static_cast<uint32_t>(text.size()));
    fopt.insert(fopt.end(), text.begin(), text.end());
    Bytes a;
    for (int i = 0; i < 4; ++i) Put16(a, static_cast<uint16_t>(anchor[i]));
    return Container(0xF004, {Record(0xF00A, 75, 2, fsp), Record(0xF00B, 2, 3, fopt), Record(0xF010, 0, 0, a)});
}
Bytes Cat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

struct BuiltDeck {
    Bytes stream;
    uint32_t lastEdit = 0;
};

// A title-and-body deck as PowerPoint writes one: the slides' placeholder
// text in the slide list, the looks in the master; saved twice, the second
// edit replacing the slide.
BuiltDeck BuildDeck() {
    // The master: a scheme, a centred bold 40 pt Courier New title in the
    // scheme's title colour, a body whose first level has an en dash bullet
    // at 28 pt and whose second level is italic at 20 pt.
    Bytes scheme;
    const uint32_t colours[8] = {0xFFFFFF, 0x332211, 0x808080, 0xCC4433, 0x00FF00, 0x2050A0, 0x0000FF, 0x800080};
    for (uint32_t c : colours) Put32(scheme, c);   // R, G, B, 0 in that byte order: 0x00BBGGRR
    Bytes title;
    Put16(title, 1);                                // one level
    Put32(title, 0x0800);                           // PF: alignment
    Put16(title, 1);                                // centred
    Put32(title, 0x1 | 0x10000 | 0x20000 | 0x40000);   // CF: bold, font, size, colour
    Put16(title, 1);                                // bold
    Put16(title, 1);                                // Courier New
    Put16(title, 40);
    Put32(title, 0x03000000);                       // scheme colour 3 (title)
    Bytes bodyStyle;
    Put16(bodyStyle, 2);
    Put32(bodyStyle, 0x1 | 0x80);                   // level 1: a bullet, its character
    Put16(bodyStyle, 1);
    Put16(bodyStyle, 0x2013);
    Put32(bodyStyle, 0x20000);
    Put16(bodyStyle, 28);
    Put32(bodyStyle, 0x1);                          // level 2: a bullet
    Put16(bodyStyle, 1);
    Put32(bodyStyle, 0x2 | 0x20000);                // italic, 20 pt
    Put16(bodyStyle, 2);
    Put16(bodyStyle, 20);
    const int titleBox[4] = {100, 200, 5000, 900};  // top, left, right, bottom
    const int bodyBox[4] = {1200, 200, 5000, 4000};
    const Bytes master = Container(0x03F8, {
        SlideAtom(2, 0, 0),
        Record(0x07F0, 1, 0, scheme),
        Record(0x0FA3, 0, 0, title),
        Record(0x0FA3, 1, 0, bodyStyle),
        Drawing({Shape(1, 0x401, 0xA00, titleBox, 0x01, {}), Shape(1, 0x402, 0xA00, bodyBox, 0x02, {})}),
    });

    // A slide: its title placeholder has no place of its own; both
    // placeholders' text is the slide list's; a text box in the scheme's
    // accent, linked.
    auto slide = [&](const std::string& boxText) {
        Bytes cf;
        Put32(cf, 0x40000);
        Put32(cf, 0x05000000);                      // scheme colour 5
        Bytes style;
        Put32(style, static_cast<uint32_t>(boxText.size() + 1));
        Put16(style, 0);
        Put32(style, 0);                            // PF: nothing said
        Put32(style, static_cast<uint32_t>(boxText.size() + 1));
        style.insert(style.end(), cf.begin(), cf.end());
        Bytes info;
        Put32(info, 0);                             // soundIdRef
        Put32(info, 7);                             // exHyperlinkIdRef
        info.push_back(4);                          // action: a hyperlink
        info.push_back(0);
        info.push_back(0);
        info.push_back(0);
        info.push_back(8);
        info.push_back(0); info.push_back(0); info.push_back(0);
        Bytes range;
        Put32(range, 0);
        Put32(range, 6);
        const Bytes text = Cat({TextHeader(4), TextChars(boxText), Record(0x0FA1, 0, 0, style),
                                Container(0x0FF2, {Record(0x0FF3, 0, 0, info)}), Record(0x0FDF, 0, 0, range)});
        const int boxPlace[4] = {4100, 300, 3000, 4300};
        const int picturePlace[4] = {3000, 4000, 5000, 4000};
        Bytes show(16, 0);
        show[9] = 10;                               // a wipe
        show[10] = 1;                               // on a click
        show[12] = 1;                               // medium
        return Container(0x03EE, {
            SlideAtom(1, 0x80000000, 0x7),
            Record(0x03F9, 0, 0, show),
            Drawing({Shape(1, 0x801, 0x800, nullptr, 0x0D, OutlineRef(0)),
                     Shape(1, 0x802, 0xA00, bodyBox, 0x0E, OutlineRef(1)),
                     Shape(202, 0x803, 0xA00, boxPlace, -1, text),
                     PictureShape(picturePlace, "A dot")}),
        });
    };

    // The document: the slide size, two fonts, a hyperlink, the master and
    // slide lists (the slide's placeholder text there: a title with no look
    // of its own, a body of two levels in 8-bit characters).
    Bytes docAtom(40, 0);
    docAtom[0] = 0x80; docAtom[1] = 0x16;           // 5760: 10"
    docAtom[4] = 0xE0; docAtom[5] = 0x10;           // 4320: 7.5"
    Bytes link;
    Put32(link, 7);
    Bytes bodyRuns;
    Put32(bodyRuns, 4); Put16(bodyRuns, 0); Put32(bodyRuns, 0);
    Put32(bodyRuns, 4); Put16(bodyRuns, 1); Put32(bodyRuns, 0);
    Put32(bodyRuns, 8); Put32(bodyRuns, 0);
    const Bytes document = Container(0x03E8, {
        Record(0x03E9, 0, 1, docAtom),
        Container(0x03F2, {Container(0x07D5, {FontEntity("Arial", 0), FontEntity("Courier New", 1)})}),
        PictureStore(),
        Container(0x0409, {Container(0x0FD7, {Record(0x0FD3, 0, 0, link),
                                              Record(0x0FBA, 1, 0, Utf16("https://example.com/"))})}),
        Container(0x0FF0, {SlidePersist(2, 0x80000000)}, 1),
        Container(0x0FF0, {SlidePersist(3, 0x100), TextHeader(0), TextChars("Inherited title"), TextHeader(1),
                           TextBytes("One\rTwo"), Record(0x0FA1, 0, 0, bodyRuns)}, 0),
        Record(0x03EA, 0, 0, {}),
    });

    BuiltDeck built;
    Bytes& s = built.stream;
    auto append = [&](const Bytes& b) {
        const uint32_t at = static_cast<uint32_t>(s.size());
        s.insert(s.end(), b.begin(), b.end());
        return at;
    };
    auto persistDir = [&](uint32_t first, std::initializer_list<uint32_t> offsets) {
        Bytes b;
        Put32(b, first | (static_cast<uint32_t>(offsets.size()) << 20));
        for (uint32_t o : offsets) Put32(b, o);
        return Record(0x1772, 0, 0, b);
    };
    auto userEdit = [&](uint32_t last, uint32_t dir) {
        Bytes b;
        Put32(b, 0x100);
        Put16(b, 0);
        b.push_back(0);
        b.push_back(3);
        Put32(b, last);
        Put32(b, dir);
        Put32(b, 1);                                // the document is persist 1
        Put32(b, 4);
        Put16(b, 1);
        Put16(b, 0);
        return Record(0x0FF5, 0, 0, b);
    };
    const uint32_t docAt = append(document);
    const uint32_t masterAt = append(master);
    const uint32_t slide1 = append(slide("Original"));
    const uint32_t dir1 = append(persistDir(1, {docAt, masterAt, slide1}));
    const uint32_t edit1 = append(userEdit(0, dir1));
    const uint32_t slide2 = append(slide("Edited text"));
    const uint32_t dir2 = append(persistDir(3, {slide2}));
    built.lastEdit = append(userEdit(edit1, dir2));
    return built;
}

Bytes CurrentUser(uint32_t editAt, bool encrypted) {
    Bytes body;
    Put32(body, 0x14);
    Put32(body, encrypted ? 0xF3D1C4DF : 0xE391C05F);
    Put32(body, editAt);
    Put16(body, 4);
    Put16(body, 0x03F4);
    body.push_back(3);
    body.push_back(0);
    Put16(body, 0);
    for (char c : std::string("Test")) body.push_back(static_cast<uint8_t>(c));
    Put32(body, 8);
    return Record(0x0FF6, 0, 0, body);
}

Bytes Package(const Bytes& stream, const Bytes* currentUser) {
    UCCompoundFileWriter writer;
    writer.AddStream("PowerPoint Document", stream);
    if (currentUser) writer.AddStream("Current User", *currentUser);
    return writer.Build();
}

void TestBuiltDeck() {
    std::cout << "\n=== A deck built record by record ===\n";
    const BuiltDeck built = BuildDeck();
    const Bytes user = CurrentUser(built.lastEdit, false);
    PptxPresentation deck;
    std::string error;
    const bool read = ReadPptPresentationFromMemory(Package(built.stream, &user), deck, error, true);
    Check(read, "read (" + error + ")");
    if (!read || deck.slides.size() != 1) {
        Check(false, "one slide");
        return;
    }
    const PptxSlide& slide = deck.slides[0];
    Check(slide.title == "Inherited title", "the title from the slide list (OutlineTextRefAtom)");
    const PptxShape* title = OfKind(slide, PptxShapeKind::Title);
    Check(title && title->placeInherited && title->x == 317500 && title->y == 158750 && title->width == 7620000,
          "the title takes its master's place");
    if (title && !title->paragraphs.empty() && !title->paragraphs[0].runs.empty()) {
        const PptxRun& run = title->paragraphs[0].runs[0];
        Check(Close(run.sizePt, 40.0, 0.01) && run.bold && run.font == "Courier New",
              "... and its master's look: 40 pt, bold, Courier New");
        Check(run.color == "3344CC", "... in the scheme's title colour (" + run.color + ")");
        Check(title->paragraphs[0].align == PptxAlign::Center, "... centred");
    } else {
        Check(false, "the title's text");
    }
    const PptxShape* body = OfKind(slide, PptxShapeKind::Body);
    Check(body && body->paragraphs.size() == 2, "the body's two paragraphs (8-bit text)");
    if (body && body->paragraphs.size() == 2) {
        const PptxParagraph& one = body->paragraphs[0];
        const PptxParagraph& two = body->paragraphs[1];
        Check(one.Text() == "One" && one.level == 0 && one.bullet && one.bulletChar == "\xE2\x80\x93",
              "level 1: the master's en dash bullet");
        Check(!one.runs.empty() && Close(one.runs[0].sizePt, 28.0, 0.01) && !one.runs[0].italic,
              "level 1: 28 pt, upright");
        Check(two.Text() == "Two" && two.level == 1 && two.bullet && !two.runs.empty() &&
                  Close(two.runs[0].sizePt, 20.0, 0.01) && two.runs[0].italic,
              "level 2: 20 pt, italic");
        Check(!one.runs.empty() && one.runs[0].color == "112233", "the scheme's text colour");
    }
    const PptxShape* box = WithText(slide, "Edited text");
    Check(box != nullptr && WithText(slide, "Original") == nullptr, "the second edit's slide is read");
    if (box && !box->paragraphs.empty() && box->paragraphs[0].runs.size() == 2) {
        Check(box->paragraphs[0].runs[0].text == "Edited" &&
                  box->paragraphs[0].runs[0].hyperlink == "https://example.com/" &&
                  box->paragraphs[0].runs[1].hyperlink.empty(),
              "the linked word carries its target");
        Check(box->paragraphs[0].runs[0].color == "A05020", "a run in the scheme's accent");
    } else {
        Check(false, "the text box's two runs");
    }
    const PptxShape* picture = OfKind(slide, PptxShapeKind::Picture);
    Check(picture && picture->altText == "A dot" && picture->pictureTarget == "ppt/media/image1.png" &&
              picture->x == 6350000 && picture->width == 1587500,
          "the picture from the store, with its description, at 4000 master units");
    Check(picture && deck.pictures.count(picture->pictureTarget) && deck.pictures[picture->pictureTarget] == kPng,
          "the picture's bytes, embedded in the store");
    Check(slide.transition.kind == PptxTransitionKind::Wipe && Close(slide.transition.durationSec, 0.75, 0.01),
          "a wipe at medium speed");
    Check(deck.masters.size() == 1 && slide.layoutIndex >= 0 && deck.themeColors["dk1"] == "112233",
          "the master, the slide's layout, the theme colours");
    Check(deck.slideWidth == 9144000 && deck.slideHeight == 6858000, "the slide size");

    // Without the Current User stream: the last edit at the stream's end.
    PptxPresentation noUser;
    Check(ReadPptPresentationFromMemory(Package(built.stream, nullptr), noUser, error) && noUser.slides.size() == 1 &&
              WithText(noUser.slides[0], "Edited text") != nullptr,
          "no Current User stream: the last edit found");

    // Encrypted: refused with a reason.
    const Bytes locked = CurrentUser(built.lastEdit, true);
    PptxPresentation encrypted;
    error.clear();
    Check(!ReadPptPresentationFromMemory(Package(built.stream, &locked), encrypted, error) &&
              Contains(error, "password"),
          "an encrypted deck refused: " + error);

    // Every truncation of the stream: no crash, no hang.
    const auto started = std::chrono::steady_clock::now();
    int readable = 0;
    for (size_t cut = 0; cut < built.stream.size(); ++cut) {
        const Bytes part(built.stream.begin(), built.stream.begin() + static_cast<std::ptrdiff_t>(cut));
        PptxPresentation d;
        std::string e;
        if (ReadPptPresentationFromMemory(Package(part, &user), d, e)) ++readable;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    Check(seconds < 60, "every truncation of the stream read or refused (" + std::to_string(built.stream.size()) +
                            " cuts, " + std::to_string(readable) + " readable, " + std::to_string(seconds) + " s)");
    // Bytes flipped everywhere.
    Bytes flipped = built.stream;
    for (size_t i = 0; i < flipped.size(); i += 7) {
        Bytes copy = built.stream;
        copy[i] ^= 0xFF;
        if (i + 4 < copy.size()) copy[i + 4] ^= 0x80;
        PptxPresentation d;
        std::string e;
        ReadPptPresentationFromMemory(Package(copy, &user), d, e);
    }
    Check(true, "bytes flipped throughout: read or refused");
}

void TestDamagedFixtures() {
    std::cout << "\n=== Damaged files ===\n";
    for (const char* name : {"ppt-features.ppt", "office-pptx-sample.ppt"}) {
        const Bytes file = ReadFileBytes(Fixture(name));
        int cuts = 0;
        for (size_t cut = 0; cut < file.size(); cut += 1531) {
            PptxPresentation d;
            std::string e;
            ReadPptPresentationFromMemory(Bytes(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(cut)), d, e);
            ++cuts;
        }
        // The document stream cut short, inside a sound compound file.
        UCCompoundFileReader cfb;
        Bytes stream, user;
        if (cfb.OpenFromMemory(file) && cfb.ReadStream("PowerPoint Document", stream) && cfb.ReadStream("Current User", user)) {
            for (size_t cut = 0; cut < stream.size(); cut += 389) {
                PptxPresentation d;
                std::string e;
                ReadPptPresentationFromMemory(Package(Bytes(stream.begin(), stream.begin() + static_cast<std::ptrdiff_t>(cut)), &user),
                                              d, e, true);
                ++cuts;
            }
            uint32_t state = 12345;
            for (int round = 0; round < 200; ++round) {
                Bytes copy = stream;
                for (int k = 0; k < 16; ++k) {
                    state = state * 1103515245u + 12345u;
                    copy[(state >> 8) % copy.size()] ^= static_cast<uint8_t>(state >> 24);
                }
                PptxPresentation d;
                std::string e;
                if (ReadPptPresentationFromMemory(Package(copy, &user), d, e, true))
                    for (size_t i = 0; i < d.slides.size() && i < 2; ++i) RenderPptxSlide(d, i, 200);
                ++cuts;
            }
        }
        Check(cuts > 100, std::string(name) + ": cut and scrambled " + std::to_string(cuts) + " ways, no crash");
    }
    PptxPresentation d;
    std::string e;
    Check(!ReadPptPresentationFromMemory(Bytes{1, 2, 3}, d, e) && !e.empty(), "three bytes: refused with a reason");
    UCCompoundFileWriter word;
    word.AddStream("WordDocument", Bytes(600, 0));
    Check(!ReadPptPresentationFromMemory(word.Build(), d, e) && Contains(e, "Word"), "a Word document: " + e);
}

void TestFileLoader() {
    std::cout << "\n=== UltraCanvasFileLoader ===\n";
    const std::string path = Fixture("ppt-features.ppt");
    Check(IsPptExtension("deck.PPT") && IsPptExtension("pps") && IsPptExtension(".pot") && !IsPptExtension("pptx"),
          "the extensions");
    Check(IsPptPresentationFile(path) && !IsPptPresentationFile(Fixture("legacy-word97.doc")),
          "a .ppt by its content, a .doc not");
    Check(UltraCanvasFileLoader::DetectOfficeDocumentKind(path) == UltraCanvasFileLoader::OfficeDocumentKind::Presentation,
          "DetectOfficeDocumentKind: a presentation");
    const std::filesystem::path renamed = std::filesystem::temp_directory_path() / "uc-ppt-test-renamed.bin";
    std::filesystem::copy_file(PathFromUtf8(path), renamed, std::filesystem::copy_options::overwrite_existing);
    Check(UltraCanvasFileLoader::DetectOfficeDocumentKind(PathToUtf8(renamed)) ==
              UltraCanvasFileLoader::OfficeDocumentKind::Presentation,
          "... renamed, by its content");
    PptxPresentation deck;
    std::string error;
    Check(UltraCanvasFileLoader::LoadPresentation(PathToUtf8(renamed), deck, error, "", true) && deck.slides.size() == 4,
          "LoadPresentation reads it, renamed too");
    std::filesystem::remove(renamed);
    const auto outline = UltraCanvasFileLoader::LoadTextDocument(path, error);
    Check(outline && Contains(outline->ToPlainText(), "Binary Deck") && Contains(outline->ToPlainText(), "Speaker notes here"),
          "LoadTextDocument: the outline");
    bool listed = false;
    for (const MediaFormatInfo& format : UltraCanvasFileLoader::GetSupportedFormats(MediaFormatCategory::Document))
        if (format.extension == "ppt" && format.canLoad && !format.canSave) listed = true;
    Check(listed, "the format inventory lists .ppt, read only");
    Check(UltraCanvasFileLoader::IsOfficeTemplate("deck.pot") && !UltraCanvasFileLoader::IsOfficeTemplate("deck.ppt") &&
              UltraCanvasFileLoader::DetectOfficeDocumentKind("deck.pot") ==
                  UltraCanvasFileLoader::OfficeDocumentKind::Presentation,
          "a .pot is a presentation template, a .ppt is not");
    std::string saveError;
    Check(!UltraCanvasFileLoader::SavePresentation(deck, PathToUtf8(std::filesystem::temp_directory_path() / "x.ppt"),
                                                   saveError),
          "SavePresentation refuses .ppt: " + saveError);
}

} // namespace

int main() {
    std::cout << "PresentationPptTest\n";
    TestFeatures();
    TestSample();
    TestBuiltDeck();
    TestDamagedFixtures();
    TestFileLoader();
    std::cout << "\n" << (g_failures == 0 ? "ALL PASSED" : std::to_string(g_failures) + " FAILED") << "\n";
    return g_failures == 0 ? 0 : 1;
}
