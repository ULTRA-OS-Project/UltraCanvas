// Tests/PresentationLayoutsRoundTripTest.cpp
// The deck's design and transitions through both formats: masters, layouts
// (PptxPresentation::masters / layouts, PptxSlide::layoutIndex) and
// PptxSlide::transition, read and written by the PowerPoint and OpenDocument
// readers and writers.
//
//   - a deck built here with one master, two layouts (Title Slide, Title and
//     Content) and transitions (Fade over 2 s moving on after 5 s, Push),
//     saved as .pptx and as .odp and read back: the masters' and layouts'
//     names, types and placeholder kinds, each slide's layout, the
//     transitions; the package's parts, content types and ids
//   - a deck with no layouts still gets the standard master and layout
//   - PptxSlideFromLayout on a deck read back gives its layout's placeholders
//   - LibreOffice-made decks (Tests/fixtures/pptx-features.pptx, and its
//     conversion pptx-features.odp): their masters and layouts
//   - interoperability with LibreOffice, when soffice is installed: our .pptx
//     converted to .odp and our .odp converted to .pptx keep the layouts'
//     kinds and the transitions
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasZipPackage.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#ifndef LAYOUTTEST_FIXTURE_DIR
#error "LAYOUTTEST_FIXTURE_DIR must name Tests/fixtures"
#endif

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Near(double a, double b) { return std::abs(a - b) < 0.01; }

std::string Fixture(const char* name) { return std::string(LAYOUTTEST_FIXTURE_DIR) + "/" + name; }

PptxRun Run(const std::string& text, float sizePt, const std::string& colour = "1F2937") {
    PptxRun run;
    run.text = text;
    run.sizePt = sizePt;
    run.color = colour;
    run.font = "Calibri";
    return run;
}

PptxParagraph Paragraph(const std::string& text, float sizePt, int level = 0, bool bullet = false) {
    PptxParagraph p;
    p.runs.push_back(Run(text, sizePt));
    p.level = level;
    p.bullet = bullet;
    return p;
}

PptxShape Placeholder(PptxShapeKind kind, const std::string& type, const std::string& name, int64_t x, int64_t y,
                      int64_t w, int64_t h, std::vector<PptxParagraph> paragraphs) {
    PptxShape shape;
    shape.kind = kind;
    shape.placeholder = type;
    shape.name = name;
    shape.x = x;
    shape.y = y;
    shape.width = w;
    shape.height = h;
    shape.anchor = kind == PptxShapeKind::Title ? PptxAnchor::Middle : PptxAnchor::Top;
    shape.paragraphs = std::move(paragraphs);
    return shape;
}

// The slide furniture a master and its layouts carry.
void AddFurniture(std::vector<PptxShape>& shapes) {
    shapes.push_back(Placeholder(PptxShapeKind::Text, "dt", "Date Placeholder", 838200, 6356350, 2743200, 365125,
                                 {Paragraph("25/11/2026", 12)}));
    shapes.push_back(Placeholder(PptxShapeKind::Text, "ftr", "Footer Placeholder", 4038600, 6356350, 4114800, 365125,
                                 {Paragraph("Footer", 12)}));
    shapes.push_back(Placeholder(PptxShapeKind::Text, "sldNum", "Slide Number Placeholder", 8610600, 6356350, 2743200,
                                 365125, {Paragraph("<#>", 12)}));
}

// One master, Title Slide and Title and Content, three slides: the first on
// Title Slide fading in over 2 s and moving on after 5 s, the second pushed
// in, the third with no transition.
PptxPresentation BuildDeck() {
    PptxPresentation deck;
    deck.slideWidth = 12192000;
    deck.slideHeight = 6858000;

    PptxMaster master;
    master.name = "Office Theme";
    master.background = "FFFFFF";
    master.placeholders.push_back(Placeholder(PptxShapeKind::Title, "title", "Title Placeholder 1", 838200, 365125,
                                              10515600, 1325563, {Paragraph("Click to edit Master title style", 44)}));
    master.placeholders.push_back(Placeholder(PptxShapeKind::Body, "body", "Text Placeholder 2", 838200, 1825625,
                                              10515600, 4351338,
                                              {Paragraph("Click to edit Master text styles", 28, 0, true),
                                               Paragraph("Second level", 24, 1, true)}));
    AddFurniture(master.placeholders);
    deck.masters.push_back(master);

    PptxLayout titleSlide;
    titleSlide.name = "Title Slide";
    titleSlide.type = "title";
    titleSlide.masterIndex = 0;
    titleSlide.placeholders.push_back(Placeholder(PptxShapeKind::Title, "ctrTitle", "Title 1", 1524000, 1122363,
                                                  9144000, 2387600, {Paragraph("Click to edit Master title style", 60)}));
    titleSlide.placeholders.push_back(Placeholder(PptxShapeKind::Subtitle, "subTitle", "Subtitle 2", 1524000, 3602038,
                                                  9144000, 1655762, {Paragraph("Click to edit Master subtitle style", 24)}));
    AddFurniture(titleSlide.placeholders);
    deck.layouts.push_back(titleSlide);

    PptxLayout content;
    content.name = "Title and Content";
    content.type = "obj";
    content.masterIndex = 0;
    content.background = "F3F4F6";
    content.placeholders.push_back(Placeholder(PptxShapeKind::Title, "title", "Title 1", 838200, 365125, 10515600,
                                               1325563, {Paragraph("Click to edit Master title style", 44)}));
    content.placeholders.push_back(Placeholder(PptxShapeKind::Body, "body", "Content Placeholder 2", 838200, 1825625,
                                               10515600, 4351338,
                                               {Paragraph("Click to edit Master text styles", 28, 0, true)}));
    AddFurniture(content.placeholders);
    deck.layouts.push_back(content);

    PptxSlide first = PptxSlideFromLayout(deck, 0);
    first.number = 1;
    first.shapes[0].paragraphs = {Paragraph("Quarterly review", 60)};
    first.shapes[1].paragraphs = {Paragraph("Layouts and transitions", 24)};
    first.title = "Quarterly review";
    first.transition.kind = PptxTransitionKind::Fade;
    first.transition.durationSec = 2.0;
    first.transition.advanceAfterSec = 5.0;
    deck.slides.push_back(first);

    PptxSlide second = PptxSlideFromLayout(deck, 1);
    second.number = 2;
    second.shapes[0].paragraphs = {Paragraph("Agenda", 44)};
    second.shapes[1].paragraphs = {Paragraph("Masters", 28, 0, true), Paragraph("Layouts", 28, 0, true)};
    second.title = "Agenda";
    second.transition.kind = PptxTransitionKind::Push;
    deck.slides.push_back(second);

    PptxSlide third = PptxSlideFromLayout(deck, 1);
    third.number = 3;
    third.shapes[0].paragraphs = {Paragraph("Questions", 44)};
    third.shapes[1].paragraphs = {Paragraph("Thank you", 28, 0, true)};
    third.title = "Questions";
    deck.slides.push_back(third);
    return deck;
}

bool HasKind(const std::vector<PptxShape>& shapes, PptxShapeKind kind) {
    return std::any_of(shapes.begin(), shapes.end(), [&](const PptxShape& s) { return s.kind == kind; });
}

bool HasType(const std::vector<PptxShape>& shapes, const std::string& type) {
    return std::any_of(shapes.begin(), shapes.end(), [&](const PptxShape& s) { return s.placeholder == type; });
}

const PptxLayout* LayoutOf(const PptxPresentation& deck, size_t slide) {
    if (slide >= deck.slides.size()) return nullptr;
    const int index = deck.slides[slide].layoutIndex;
    return index >= 0 && index < static_cast<int>(deck.layouts.size()) ? &deck.layouts[static_cast<size_t>(index)] : nullptr;
}

std::string FirstText(const std::vector<PptxShape>& shapes, PptxShapeKind kind) {
    for (const PptxShape& s : shapes)
        if (s.kind == kind && !s.paragraphs.empty()) return s.paragraphs.front().Text();
    return "";
}

// What every round trip must keep: the master, the two layouts with their
// names, types and placeholder kinds, each slide's layout and transition.
void CheckDesign(const PptxPresentation& deck, const std::string& via, bool exactTypes) {
    Check(deck.masters.size() == 1, via + ": one master (" + std::to_string(deck.masters.size()) + ")");
    if (!deck.masters.empty()) {
        Check(deck.masters[0].name == "Office Theme", via + ": the master's name (" + deck.masters[0].name + ")");
        Check(HasKind(deck.masters[0].placeholders, PptxShapeKind::Title) &&
                  HasKind(deck.masters[0].placeholders, PptxShapeKind::Body),
              via + ": the master's title and body placeholders");
        Check(HasType(deck.masters[0].placeholders, "dt") && HasType(deck.masters[0].placeholders, "ftr") &&
                  HasType(deck.masters[0].placeholders, "sldNum"),
              via + ": the master's date, footer and number");
        Check(FirstText(deck.masters[0].placeholders, PptxShapeKind::Title) == "Click to edit Master title style",
              via + ": the master title's prompt text");
    }
    Check(deck.layouts.size() == 2, via + ": two layouts (" + std::to_string(deck.layouts.size()) + ")");
    if (deck.layouts.size() >= 2) {
        const PptxLayout& a = deck.layouts[0];
        const PptxLayout& b = deck.layouts[1];
        Check(a.name == "Title Slide" && b.name == "Title and Content", via + ": the layouts' names (" + a.name + ", " + b.name + ")");
        Check(a.type == "title" && b.type == "obj", via + ": the layouts' types (" + a.type + ", " + b.type + ")");
        Check(a.masterIndex == 0 && b.masterIndex == 0, via + ": the layouts are on the master");
        Check(HasKind(a.placeholders, PptxShapeKind::Title) && HasKind(a.placeholders, PptxShapeKind::Subtitle) &&
                  !HasKind(a.placeholders, PptxShapeKind::Body),
              via + ": Title Slide holds a title and a subtitle");
        Check(HasKind(b.placeholders, PptxShapeKind::Title) && HasKind(b.placeholders, PptxShapeKind::Body) &&
                  !HasKind(b.placeholders, PptxShapeKind::Subtitle),
              via + ": Title and Content holds a title and a body");
        if (exactTypes) {
            Check(HasType(a.placeholders, "ctrTitle") && HasType(a.placeholders, "subTitle"),
                  via + ": Title Slide's placeholder types (ctrTitle, subTitle)");
            Check(b.background == "F3F4F6", via + ": the layout's own background (" + b.background + ")");
            Check(FirstText(a.placeholders, PptxShapeKind::Subtitle) == "Click to edit Master subtitle style",
                  via + ": a layout's prompt text");
        }
        Check(HasType(b.placeholders, "dt") && HasType(b.placeholders, "sldNum"), via + ": the layout's furniture");
    }
    Check(deck.slides.size() == 3, via + ": three slides");
    if (deck.slides.size() == 3) {
        Check(deck.slides[0].layoutIndex == 0 && deck.slides[1].layoutIndex == 1 && deck.slides[2].layoutIndex == 1,
              via + ": each slide on its layout (" + std::to_string(deck.slides[0].layoutIndex) + ", " +
                  std::to_string(deck.slides[1].layoutIndex) + ", " + std::to_string(deck.slides[2].layoutIndex) + ")");
        const PptxTransition& fade = deck.slides[0].transition;
        Check(fade.kind == PptxTransitionKind::Fade, via + ": slide 1 fades in");
        Check(Near(fade.durationSec, 2.0), via + ": over 2 s (" + std::to_string(fade.durationSec) + ")");
        Check(Near(fade.advanceAfterSec, 5.0), via + ": moving on after 5 s (" + std::to_string(fade.advanceAfterSec) + ")");
        Check(fade.advanceOnClick, via + ": and on a click");
        const PptxTransition& push = deck.slides[1].transition;
        Check(push.kind == PptxTransitionKind::Push, via + ": slide 2 is pushed in");
        Check(push.advanceAfterSec < 0, via + ": slide 2 waits for a click");
        Check(deck.slides[2].transition.kind == PptxTransitionKind::NoTransition, via + ": slide 3 has no transition");
        Check(deck.slides[0].title == "Quarterly review" && deck.slides[1].title == "Agenda", via + ": the slides' titles");
    }
}

std::string Entry(const std::vector<uint8_t>& package, const std::string& name) {
    UCZipPackageReader zip;
    std::string xml;
    if (!zip.OpenFromMemory(package) || !zip.ReadEntry(name, xml)) return "";
    return xml;
}

size_t Count(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
    return n;
}

// The package as PowerPoint opens it: every master and layout part has its
// content type and relationships, and the ids are unique and >= 2^31.
void CheckPackage(const std::vector<uint8_t>& package) {
    const std::string types = Entry(package, "[Content_Types].xml");
    const std::string presentation = Entry(package, "ppt/presentation.xml");
    const std::string master = Entry(package, "ppt/slideMasters/slideMaster1.xml");
    const std::string masterRels = Entry(package, "ppt/slideMasters/_rels/slideMaster1.xml.rels");
    Check(!Entry(package, "ppt/slideLayouts/slideLayout2.xml").empty() &&
              !Entry(package, "ppt/slideLayouts/_rels/slideLayout2.xml.rels").empty(),
          "pptx: the second layout's part and relationships");
    Check(Count(types, "slideLayout+xml") == 2 && Count(types, "slideMaster+xml") == 1,
          "pptx: content types for both layouts and the master");
    Check(Count(master, "<p:sldLayoutId ") == 2 && Count(masterRels, "relationships/slideLayout\"") == 2,
          "pptx: the master lists both layouts");
    std::set<std::string> ids;
    size_t count = 0;
    for (const std::string* xml : {&presentation, &master}) {
        for (const char* tag : {"<p:sldMasterId id=\"", "<p:sldLayoutId id=\""}) {
            for (size_t at = xml->find(tag); at != std::string::npos; at = xml->find(tag, at + 1)) {
                const size_t start = at + std::string(tag).size();
                const std::string id = xml->substr(start, xml->find('"', start) - start);
                ids.insert(id);
                ++count;
                Check(std::stoull(id) >= 2147483648ull, "pptx: id " + id + " is at least 2^31");
            }
        }
    }
    Check(count == 3 && ids.size() == 3, "pptx: the master's and layouts' ids are unique");
    const std::string slide1 = Entry(package, "ppt/slides/slide1.xml");
    const std::string slide1Rels = Entry(package, "ppt/slides/_rels/slide1.xml.rels");
    const std::string slide2Rels = Entry(package, "ppt/slides/_rels/slide2.xml.rels");
    Check(slide1Rels.find("slideLayouts/slideLayout1.xml") != std::string::npos &&
              slide2Rels.find("slideLayouts/slideLayout2.xml") != std::string::npos,
          "pptx: each slide links to its layout");
    Check(slide1.find("p14:dur=\"2000\"") != std::string::npos && slide1.find("advTm=\"5000\"") != std::string::npos &&
              slide1.find("<mc:Fallback>") != std::string::npos,
          "pptx: the fade's length in p14:dur with a fallback, and its advance");
    Check(slide1.find("<p:ph type=\"ctrTitle\"/>") != std::string::npos &&
              slide1.find("<p:ph type=\"subTitle\" idx=\"1\"/>") != std::string::npos,
          "pptx: the slide's placeholders name the layout's");
    const std::string layout2 = Entry(package, "ppt/slideLayouts/slideLayout2.xml");
    Check(layout2.find("type=\"obj\"") != std::string::npos && layout2.find("name=\"Title and Content\"") != std::string::npos,
          "pptx: the layout's type and name");
    Check(layout2.find("<a:lvl1pPr") != std::string::npos && layout2.find("type=\"slidenum\"") != std::string::npos,
          "pptx: the layout's list style and slide number field");
}

// PptxSlideFromLayout on a deck read back: the layout's placeholders,
// empty, keeping their look; not the furniture.
void CheckSlideFromLayout(const PptxPresentation& deck, const std::string& via) {
    if (deck.layouts.size() < 2) return;
    const PptxSlide slide = PptxSlideFromLayout(deck, 1);
    Check(slide.layoutIndex == 1, via + ": PptxSlideFromLayout names its layout");
    Check(slide.shapes.size() == 2 && HasKind(slide.shapes, PptxShapeKind::Title) &&
              HasKind(slide.shapes, PptxShapeKind::Body),
          via + ": PptxSlideFromLayout gives the layout's title and body (" + std::to_string(slide.shapes.size()) + ")");
    bool empty = true, sized = true;
    for (const PptxShape& shape : slide.shapes) {
        for (const PptxParagraph& p : shape.paragraphs) empty = empty && p.Text().empty();
        sized = sized && shape.width > 0 && !shape.paragraphs.empty() && !shape.paragraphs.front().runs.empty() &&
                shape.paragraphs.front().runs.front().sizePt > 0;
    }
    Check(empty, via + ": its placeholders are empty");
    Check(sized, via + ": and keep their place and text size");
}

// soffice, when it is installed: converts `from` into `toExtension` in `dir`.
bool HaveSoffice() {
#ifdef _WIN32
    return false;
#else
    return std::system("command -v soffice >/dev/null 2>&1") == 0;
#endif
}

std::string Convert(const fs::path& from, const std::string& toExtension, const fs::path& dir) {
    const fs::path profile = dir / "profile";
    const std::string command = "soffice -env:UserInstallation=file://" + PathToUtf8(profile) +
                                " --headless --convert-to " + toExtension + " --outdir \"" + PathToUtf8(dir) + "\" \"" +
                                PathToUtf8(from) + "\" >/dev/null 2>&1";
    if (std::system(command.c_str()) != 0) return "";
    fs::path out = dir / from.filename();
    out.replace_extension("." + toExtension);
    return fs::exists(out) ? PathToUtf8(out) : "";
}

} // namespace

int main() {
    std::cout << "PresentationLayoutsRoundTripTest\n";
    const PptxPresentation deck = BuildDeck();
    Check(deck.slides.size() == 3 && deck.slides[0].shapes.size() == 2 && deck.slides[1].shapes.size() == 2,
          "PptxSlideFromLayout: the layouts' placeholders without the furniture");

    // ----- PowerPoint -----
    std::vector<uint8_t> pptx;
    std::string error;
    Check(WritePptxPresentationToPackage(deck, pptx, error), "pptx: written " + error);
    CheckPackage(pptx);
    PptxPresentation fromPptx;
    Check(ReadPptxPresentationFromPackage(pptx, fromPptx, error), "pptx: read back " + error);
    CheckDesign(fromPptx, "pptx", true);
    CheckSlideFromLayout(fromPptx, "pptx");

    // Written again from what was read: the same design.
    std::vector<uint8_t> again;
    PptxPresentation twice;
    Check(WritePptxPresentationToPackage(fromPptx, again, error) && ReadPptxPresentationFromPackage(again, twice, error),
          "pptx: the read deck written and read again");
    CheckDesign(twice, "pptx twice", true);

    // ----- OpenDocument -----
    std::vector<uint8_t> odp;
    Check(WriteOdpPresentationToMemory(deck, odp, error), "odp: written " + error);
    PptxPresentation fromOdp;
    Check(ReadOdpPresentationFromMemory(odp, fromOdp, error), "odp: read back " + error);
    CheckDesign(fromOdp, "odp", false);
    CheckSlideFromLayout(fromOdp, "odp");
    const std::string styles = Entry(odp, "styles.xml");
    Check(styles.find("<style:presentation-page-layout style:name=\"UCL2\" style:display-name=\"Title and Content\">") !=
                  std::string::npos &&
              styles.find("style:name=\"Office_20_Theme\" style:display-name=\"Office Theme\"") != std::string::npos,
          "odp: the page layouts and the master page");
    std::vector<uint8_t> flat;
    PptxPresentation fromFlat;
    Check(WriteOdpPresentationToMemory(deck, flat, error, "fodp") && ReadOdpPresentationFromMemory(flat, fromFlat, error),
          "fodp: written and read back " + error);
    CheckDesign(fromFlat, "fodp", false);
    // .odp -> model -> .pptx: the design crosses formats.
    std::vector<uint8_t> crossed;
    PptxPresentation fromCrossed;
    Check(WritePptxPresentationToPackage(fromOdp, crossed, error) && ReadPptxPresentationFromPackage(crossed, fromCrossed, error),
          "odp -> pptx: written and read back " + error);
    CheckDesign(fromCrossed, "odp -> pptx", false);

    // ----- An empty placeholder survives: an editor types into it -----
    {
        PptxPresentation withEmpty = deck;
        PptxSlide blankBody = PptxSlideFromLayout(withEmpty, 1);
        blankBody.number = 4;
        blankBody.shapes[0].paragraphs = {Paragraph("Empty body", 44)};
        withEmpty.slides.push_back(blankBody);   // its content placeholder left empty
        const PptxShape& layoutBody = withEmpty.layouts[1].placeholders[1];
        std::vector<uint8_t> emptyPptx, emptyOdp;
        PptxPresentation emptyFromPptx, emptyFromOdp;
        Check(WritePptxPresentationToPackage(withEmpty, emptyPptx, error) &&
                  ReadPptxPresentationFromPackage(emptyPptx, emptyFromPptx, error) &&
                  WriteOdpPresentationToMemory(withEmpty, emptyOdp, error) &&
                  ReadOdpPresentationFromMemory(emptyOdp, emptyFromOdp, error),
              "empty placeholder: written and read back " + error);
        const std::string slide4 = Entry(emptyPptx, "ppt/slides/slide4.xml");
        Check(slide4.find("<p:ph idx=\"1\"/>") != std::string::npos &&
                  slide4.find("<a:endParaRPr lang=\"en-US\" sz=\"2800\"") != std::string::npos,
              "empty placeholder: written as <p:ph idx=\"1\"/> with an empty paragraph sized 28 pt");
        for (const auto& [via, back] : {std::pair<std::string, const PptxPresentation*>{"pptx", &emptyFromPptx},
                                        std::pair<std::string, const PptxPresentation*>{"odp", &emptyFromOdp}}) {
            const PptxShape* body = nullptr;
            if (back->slides.size() == 4)
                for (const PptxShape& shape : back->slides[3].shapes)
                    if (shape.kind == PptxShapeKind::Body) body = &shape;
            Check(body != nullptr, via + ": the empty content placeholder is read back");
            if (!body) continue;
            // ODF keeps lengths in cm to four places: within 36 EMU.
            auto close = [](int64_t a, int64_t b) { return std::llabs(a - b) <= 40; };
            Check(close(body->x, layoutBody.x) && close(body->y, layoutBody.y) && close(body->width, layoutBody.width) &&
                      close(body->height, layoutBody.height),
                  via + ": in its box");
            Check(body->placeholder == "body", via + ": its type is body (" + body->placeholder + ")");
            Check(body->paragraphs.size() == 1 && body->paragraphs[0].Text().empty() &&
                      body->paragraphs[0].runs.size() == 1 && Near(body->paragraphs[0].runs[0].sizePt, 28),
                  via + ": one empty paragraph whose run keeps 28 pt");
        }
    }

    // ----- No layouts: the standard set -----
    PptxPresentation plain;
    PptxSlide slide;
    slide.shapes.push_back(Placeholder(PptxShapeKind::Title, "", "", 0, 0, 0, 0, {Paragraph("Plain", 44)}));
    plain.slides.push_back(slide);
    std::vector<uint8_t> plainPackage;
    PptxPresentation plainBack;
    Check(WritePptxPresentationToPackage(plain, plainPackage, error) &&
              ReadPptxPresentationFromPackage(plainPackage, plainBack, error),
          "standard: written and read back " + error);
    Check(plainBack.masters.size() == 1 && plainBack.layouts.size() == 1 && plainBack.layouts[0].name == "Title and Content" &&
              plainBack.layouts[0].type == "obj" && !plainBack.slides.empty() && plainBack.slides[0].layoutIndex == 0,
          "standard: one master and its Title and Content layout");
    Check(!plainBack.slides.empty() && plainBack.slides[0].transition.kind == PptxTransitionKind::NoTransition &&
              Entry(plainPackage, "ppt/slides/slide1.xml").find("p:transition") == std::string::npos,
          "standard: no transition written for a slide without one");

    // ----- LibreOffice-made decks -----
    PptxPresentation features;
    Check(ReadPptxPresentation(Fixture("pptx-features.pptx"), features, error), "fixture pptx read " + error);
    Check(features.masters.size() == 1 && features.layouts.size() == 11, "fixture pptx: one master, eleven layouts (" +
                                                                         std::to_string(features.layouts.size()) + ")");
    if (features.layouts.size() == 11) {
        Check(features.layouts[0].name == "Title Slide" && features.layouts[0].type == "title",
              "fixture pptx: the first layout is Title Slide");
        Check(FirstText(features.masters[0].placeholders, PptxShapeKind::Title) == "Click to edit Master title style",
              "fixture pptx: the master's prompt text");
        const PptxLayout* first = LayoutOf(features, 0);
        Check(first && first->name == "Title Only" && first->type == "titleOnly",
              "fixture pptx: the slides are on Title Only");
        CheckSlideFromLayout(features, "fixture pptx");
    }
    PptxPresentation featuresOdp;
    Check(ReadOdpPresentation(Fixture("pptx-features.odp"), featuresOdp, error), "fixture odp read " + error);
    Check(featuresOdp.masters.size() == 11, "fixture odp: LibreOffice's master page per PowerPoint layout (" +
                                                std::to_string(featuresOdp.masters.size()) + ")");
    if (const PptxLayout* layout = LayoutOf(featuresOdp, 0)) {
        Check(layout->name == "Title Only" && layout->type == "titleOnly" && HasKind(layout->placeholders, PptxShapeKind::Title),
              "fixture odp: the slides' page layout is Title Only (" + layout->name + ")");
        Check(featuresOdp.masters[static_cast<size_t>(layout->masterIndex)].name == "Title Only",
              "fixture odp: on the Title Only master page");
        const PptxSlide fresh = PptxSlideFromLayout(featuresOdp, featuresOdp.slides[0].layoutIndex);
        Check(fresh.shapes.size() == 1 && fresh.shapes[0].kind == PptxShapeKind::Title,
              "fixture odp: PptxSlideFromLayout gives the title");
    } else {
        Check(false, "fixture odp: the slides have a layout");
    }

    // ----- LibreOffice interoperability -----
    if (!HaveSoffice()) {
        std::cout << "  [SKIP] soffice is not installed: interoperability not checked\n";
    } else {
        const fs::path dir = fs::temp_directory_path() / "uc-layouts-roundtrip";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "in", ec);
        const fs::path ourPptx = dir / "in" / "deck.pptx";
        const fs::path ourOdp = dir / "in" / "deck.odp";
        Check(WritePptxPresentation(deck, PathToUtf8(ourPptx), error) && WriteOdpPresentation(deck, PathToUtf8(ourOdp), error),
              "interop: our files saved " + error);

        // Our .pptx as LibreOffice reads it, saved as .odp.
        const std::string odpByLo = Convert(ourPptx, "odp", dir);
        Check(!odpByLo.empty(), "interop: LibreOffice opens our .pptx (converted to .odp)");
        PptxPresentation lo;
        if (!odpByLo.empty() && ReadOdpPresentation(odpByLo, lo, error) && lo.slides.size() == 3) {
            const PptxLayout* a = LayoutOf(lo, 0);
            const PptxLayout* b = LayoutOf(lo, 1);
            Check(a && HasKind(a->placeholders, PptxShapeKind::Title) && HasKind(a->placeholders, PptxShapeKind::Subtitle),
                  "interop pptx -> odp: slide 1 on a title-and-subtitle layout");
            Check(b && HasKind(b->placeholders, PptxShapeKind::Title) && HasKind(b->placeholders, PptxShapeKind::Body),
                  "interop pptx -> odp: slide 2 on a title-and-content layout");
            bool masterNamed = false;
            for (const PptxMaster& m : lo.masters) masterNamed = masterNamed || m.name == "Title and Content";
            Check(masterNamed, "interop pptx -> odp: our layout's name on LibreOffice's master page");
            Check(lo.slides[0].transition.kind == PptxTransitionKind::Fade && Near(lo.slides[0].transition.durationSec, 2.0) &&
                      Near(lo.slides[0].transition.advanceAfterSec, 5.0),
                  "interop pptx -> odp: the fade, its 2 s and the advance after 5 s (" +
                      std::to_string(lo.slides[0].transition.durationSec) + ", " +
                      std::to_string(lo.slides[0].transition.advanceAfterSec) + ")");
            Check(lo.slides[1].transition.kind == PptxTransitionKind::Push, "interop pptx -> odp: the push");
            Check(lo.slides[2].transition.kind == PptxTransitionKind::NoTransition, "interop pptx -> odp: no transition on 3");
        } else {
            Check(false, "interop pptx -> odp: read back " + error);
        }

        // Our .odp as LibreOffice reads it, saved as .pptx.
        fs::create_directories(dir / "fromodp", ec);
        const std::string pptxByLo = Convert(ourOdp, "pptx", dir / "fromodp");
        Check(!pptxByLo.empty(), "interop: LibreOffice opens our .odp (converted to .pptx)");
        PptxPresentation lo2;
        if (!pptxByLo.empty() && ReadPptxPresentation(pptxByLo, lo2, error) && lo2.slides.size() == 3) {
            const PptxLayout* a = LayoutOf(lo2, 0);
            const PptxLayout* b = LayoutOf(lo2, 1);
            Check(!lo2.masters.empty() && !lo2.layouts.empty(), "interop odp -> pptx: masters and layouts read");
            Check(a && a->type == "title", "interop odp -> pptx: slide 1 on a Title Slide layout (" + (a ? a->type : "") + ")");
            // LibreOffice writes its title-and-content autolayout as "tx" (Title and Text).
            Check(b && (b->type == "obj" || b->type == "tx") && HasKind(b->placeholders, PptxShapeKind::Body),
                  "interop odp -> pptx: slide 2 on a title-and-content layout (" + (b ? b->type : "") + ")");
            Check(lo2.slides[0].transition.kind == PptxTransitionKind::Fade && Near(lo2.slides[0].transition.durationSec, 2.0) &&
                      Near(lo2.slides[0].transition.advanceAfterSec, 5.0),
                  "interop odp -> pptx: the fade, its 2 s and the advance after 5 s (" +
                      std::to_string(lo2.slides[0].transition.durationSec) + ", " +
                      std::to_string(lo2.slides[0].transition.advanceAfterSec) + ")");
            Check(lo2.slides[1].transition.kind == PptxTransitionKind::Push, "interop odp -> pptx: the push");
            Check(lo2.slides[2].transition.kind == PptxTransitionKind::NoTransition, "interop odp -> pptx: no transition on 3");
        } else {
            Check(false, "interop odp -> pptx: read back " + error);
        }
        fs::remove_all(dir, ec);
    }

    std::cout << (g_failures == 0 ? "All checks passed\n" : std::to_string(g_failures) + " check(s) failed\n");
    return g_failures == 0 ? 0 : 1;
}
