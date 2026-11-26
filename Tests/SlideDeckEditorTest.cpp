// Tests/SlideDeckEditorTest.cpp
// The slide editor's core (UltraCanvasSlideDeckEditor.h) without a window:
// every command applied, undone and redone - slides inserted, duplicated,
// moved, hidden and deleted; shapes moved, nudged (one step for a run),
// resized, turned, reordered, duplicated, deleted, pasted, their text set;
// the selection put back by undo - and hit testing (turned shapes too),
// handles and snapping to the slide and to other shapes; groups (grouped,
// selected whole, moved, resized and turned with their shapes, entered,
// kept together in the z-order, copied, ungrouped, nested), align and
// distribute, picture crops, notes as paragraphs, a chart's data, several
// slides at once; groups and crops through .pptx, .odp and .fodp, and
// through LibreOffice when soffice is installed.
//
// Usage: SlideDeckEditorTest
// Exit code is the number of failed checks.

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasSlideDeckEditor.h"
#include "UltraCanvasZipPackage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

constexpr int64_t kInch = 914400;

const PptxShape& Shape(const UCSlideDeckEditor& editor, int i) {
    return editor.CurrentSlide()->shapes[static_cast<size_t>(i)];
}

std::string ReadZipEntryText(const std::vector<uint8_t>& package, const std::string& name) {
    UCZipPackageReader zip;
    std::string out;
    if (!zip.OpenFromMemory(package) || !zip.ReadEntry(name, out)) return "";
    return out;
}

// `package` with entry `name` holding `text` instead.
bool WriteZipEntryText(const std::vector<uint8_t>& package, const std::string& name, const std::string& text,
                       std::vector<uint8_t>& out) {
    UCZipPackageReader zip;
    UCZipPackageWriter writer;
    if (!zip.OpenFromMemory(package) || !writer.OpenInMemory()) return false;
    for (const std::string& entry : zip.EntryNames()) {
        if (entry == name) {
            if (!writer.AddEntry(entry, text)) return false;
            continue;
        }
        std::vector<uint8_t> bytes;
        if (!zip.ReadEntry(entry, bytes) || !writer.AddEntry(entry, bytes.data(), bytes.size(), entry != "mimetype")) return false;
    }
    if (!writer.Finalize()) return false;
    out = writer.TakeBytes();
    return true;
}

// A 40 x 30 pixel PNG (no resolution of its own: 96 to the inch).
std::vector<uint8_t> TestPng() {
    static const uint8_t kBytes[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x1e, 0x08, 0x02, 0x00, 0x00, 0x00, 0xd1, 0xbf, 0xcb,
    0x8a, 0x00, 0x00, 0x00, 0x3a, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed, 0xd0, 0x31, 0x11, 0x00,
    0x20, 0x0c, 0xc0, 0xc0, 0x94, 0x43, 0x56, 0x75, 0x30, 0x23, 0xb7, 0xb2, 0xaa, 0x80, 0xa5, 0x0b,
    0x4b, 0x62, 0xe0, 0xef, 0x12, 0x95, 0xc5, 0xb4, 0xc3, 0xbc, 0xc5, 0xa7, 0x84, 0x85, 0x85, 0x85,
    0x85, 0x85, 0x9f, 0x6d, 0xb8, 0xae, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x06, 0xa0, 0x01, 0x95,
    0x44, 0x02, 0xc0, 0x73, 0xc3, 0x46, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82,
    };
    return std::vector<uint8_t>(std::begin(kBytes), std::end(kBytes));
}

bool Near(double a, double b, double within = 0.01) { return std::fabs(a - b) <= within; }

// A slide with a group "Pair" (a rectangle, and a group of an ellipse and a
// triangle), a shape alone, and a picture cropped by a quarter at the left
// and a tenth at the top.
PptxPresentation GroupDeck() {
    PptxPresentation deck;
    deck.slideWidth = 12192000;
    deck.slideHeight = 6858000;
    PptxSlide slide;
    auto shape = [](const char* geometry, int64_t x, int64_t y, std::vector<int> path) {
        PptxShape s;
        s.kind = PptxShapeKind::Text;
        s.name = geometry;
        s.geometry = geometry;
        s.fill = "4472C4";
        s.x = x;
        s.y = y;
        s.width = kInch;
        s.height = kInch;
        s.groupPath = std::move(path);
        return s;
    };
    slide.shapes.push_back(shape("rect", kInch, kInch, {7}));
    slide.shapes.push_back(shape("ellipse", 3 * kInch, kInch, {7, 9}));
    slide.shapes.push_back(shape("triangle", 3 * kInch, 3 * kInch, {7, 9}));
    slide.shapes.push_back(shape("star5", 8 * kInch, kInch, {}));
    slide.groupNames[7] = "Pair";
    PptxShape picture;
    picture.kind = PptxShapeKind::Picture;
    picture.name = "Picture";
    picture.pictureTarget = "ppt/media/test.png";
    picture.x = 2 * kInch;
    picture.y = 4 * kInch;
    picture.width = 3 * kInch;
    picture.height = static_cast<int64_t>(1.8 * kInch);
    picture.cropLeft = 0.25;
    picture.cropTop = 0.1;
    slide.shapes.push_back(picture);
    deck.slides.push_back(slide);
    deck.pictures["ppt/media/test.png"] = TestPng();
    return deck;
}

// What a deck read back must have kept of GroupDeck's slide.
void CheckGroupDeck(const PptxPresentation& deck, const std::string& via, bool names = true) {
    if (deck.slides.empty()) { Check(false, via + ": a slide"); return; }
    const PptxSlide& slide = deck.slides[0];
    const PptxShape* rect = nullptr;
    const PptxShape* ellipse = nullptr;
    const PptxShape* triangle = nullptr;
    const PptxShape* star = nullptr;
    const PptxShape* picture = nullptr;
    for (const PptxShape& s : slide.shapes) {
        if (s.kind == PptxShapeKind::Picture) picture = &s;
        else if (std::llabs(s.y - kInch) < 2000 && std::llabs(s.x - kInch) < 2000) rect = &s;
        else if (std::llabs(s.y - kInch) < 2000 && std::llabs(s.x - 3 * kInch) < 2000) ellipse = &s;
        else if (std::llabs(s.y - 3 * kInch) < 2000) triangle = &s;
        else if (std::llabs(s.x - 8 * kInch) < 2000) star = &s;
    }
    Check(rect && ellipse && triangle && star && picture, via + ": every shape in its place");
    if (!rect || !ellipse || !triangle || !star || !picture) return;
    Check(rect->groupPath.size() == 1 && ellipse->groupPath.size() == 2 && triangle->groupPath == ellipse->groupPath &&
              ellipse->groupPath[0] == rect->groupPath[0] && star->groupPath.empty() && picture->groupPath.empty(),
          via + ": the group, the group inside it, the shapes in none");
    if (names && !rect->groupPath.empty()) {
        const auto name = slide.groupNames.find(rect->groupPath[0]);
        Check(name != slide.groupNames.end() && name->second == "Pair", via + ": the group's name");
    }
    Check(Near(picture->cropLeft, 0.25) && Near(picture->cropTop, 0.1) && Near(picture->cropRight, 0) &&
              Near(picture->cropBottom, 0),
          via + ": the picture's crop (" + std::to_string(picture->cropLeft) + ", " + std::to_string(picture->cropTop) + ")");
    Check(std::llabs(picture->width - 3 * kInch) < 4000 && std::llabs(picture->x - 2 * kInch) < 4000,
          via + ": the cropped picture's box");
}

bool HaveSoffice() {
#ifdef _WIN32
    return false;
#else
    return std::system("command -v soffice >/dev/null 2>&1") == 0;
#endif
}

// soffice converts `from` into `toExtension` in `dir`; the file made, or "".
std::string Convert(const std::filesystem::path& from, const std::string& toExtension, const std::filesystem::path& dir) {
    const std::filesystem::path profile = dir / "profile";
    const std::string command = "soffice -env:UserInstallation=file://" + PathToUtf8(profile) +
                                " --headless --convert-to " + toExtension + " --outdir \"" + PathToUtf8(dir) + "\" \"" +
                                PathToUtf8(from) + "\" >/dev/null 2>&1";
    if (std::system(command.c_str()) != 0) return "";
    std::filesystem::path out = dir / from.filename();
    out.replace_extension("." + toExtension);
    return std::filesystem::exists(out) ? PathToUtf8(out) : "";
}

void CheckRoundTrips() {
    const PptxPresentation deck = GroupDeck();
    std::string error;
    std::vector<uint8_t> pptx;
    Check(WritePptxPresentationToPackage(deck, pptx, error), "pptx: written " + error);
    const std::string xml = ReadZipEntryText(pptx, "ppt/slides/slide1.xml");
    Check(xml.find("<p:grpSp>") != std::string::npos && xml.find("name=\"Pair\"") != std::string::npos &&
              xml.find("<a:chOff") != std::string::npos && xml.find("<a:srcRect l=\"25000\" t=\"10000\"") != std::string::npos,
          "pptx: p:grpSp with its name, chOff / chExt and the crop's a:srcRect");
    PptxPresentation fromPptx;
    Check(ReadPptxPresentationFromPackage(pptx, fromPptx, error, true), "pptx: read back (with the pictures) " + error);
    CheckGroupDeck(fromPptx, "pptx");

    std::vector<uint8_t> odp;
    Check(WriteOdpPresentationToMemory(deck, odp, error), "odp: written " + error);
    const std::string content = ReadZipEntryText(odp, "content.xml");
    Check(content.find("<draw:g draw:name=\"Pair\">") != std::string::npos && content.find("fo:clip=\"rect(") != std::string::npos,
          "odp: draw:g with its name, the crop as fo:clip");
    PptxPresentation fromOdp;
    Check(ReadOdpPresentationFromMemory(odp, fromOdp, error), "odp: read back " + error);
    CheckGroupDeck(fromOdp, "odp");
    std::vector<uint8_t> flat;
    PptxPresentation fromFlat;
    Check(WriteOdpPresentationToMemory(deck, flat, error, "fodp") && ReadOdpPresentationFromMemory(flat, fromFlat, error, "", true),
          "fodp: written and read back " + error);
    CheckGroupDeck(fromFlat, "fodp");

    // A group read is written again as it was.
    std::vector<uint8_t> again;
    PptxPresentation twice;
    Check(WritePptxPresentationToPackage(fromPptx, again, error) && ReadPptxPresentationFromPackage(again, twice, error),
          "pptx: written again " + error);
    CheckGroupDeck(twice, "pptx twice");

    // A turned group turns its shapes with it.
    {
        std::string turnedXml = xml;
        const size_t at = turnedXml.find("<p:grpSpPr><a:xfrm>", turnedXml.find("name=\"Pair\""));
        if (at != std::string::npos) turnedXml.replace(at, 19, "<p:grpSpPr><a:xfrm rot=\"5400000\">");
        std::vector<uint8_t> turnedPackage;
        PptxPresentation turned;
        if (!WriteZipEntryText(pptx, "ppt/slides/slide1.xml", turnedXml, turnedPackage) ||
            !ReadPptxPresentationFromPackage(turnedPackage, turned, error) || turned.slides.empty()) {
            Check(false, "pptx: a turned group read " + error);
        } else {
            const PptxShape& rect = turned.slides[0].shapes[0];
            // The group's box: 1 .. 4 inches across, 1 .. 4 down; its centre (2.5, 2.5).
            // The rectangle's centre (1.5, 1.5) turned by 90 degrees: (3.5, 1.5).
            Check(Near(rect.rotationDeg, 90) && std::llabs(rect.x + rect.width / 2 - static_cast<int64_t>(3.5 * kInch)) < 2000 &&
                      std::llabs(rect.y + rect.height / 2 - static_cast<int64_t>(1.5 * kInch)) < 2000,
                  "pptx: a turned group's shapes turned about its centre");
        }
    }

    if (!HaveSoffice()) {
        std::printf("  [SKIP] soffice is not installed: LibreOffice not cross-checked\n");
        return;
    }
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "uc-slide-groups-roundtrip";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "in", ec);
    fs::create_directories(dir / "fromodp", ec);
    const fs::path ourPptx = dir / "in" / "groups.pptx";
    const fs::path ourOdp = dir / "in" / "groups.odp";
    Check(WritePptxPresentation(deck, PathToUtf8(ourPptx), error) && WriteOdpPresentation(deck, PathToUtf8(ourOdp), error),
          "interop: our files saved " + error);
    const std::string odpByLo = Convert(ourPptx, "odp", dir);
    PptxPresentation lo;
    Check(!odpByLo.empty() && ReadOdpPresentation(odpByLo, lo, error, "", true), "interop: LibreOffice opens our .pptx " + error);
    CheckGroupDeck(lo, "interop pptx -> LibreOffice -> odp", false);
    const std::string pptxByLo = Convert(ourOdp, "pptx", dir / "fromodp");
    PptxPresentation lo2;
    Check(!pptxByLo.empty() && ReadPptxPresentation(pptxByLo, lo2, error, "", true), "interop: LibreOffice opens our .odp " + error);
    CheckGroupDeck(lo2, "interop odp -> LibreOffice -> pptx", false);
    fs::remove_all(dir, ec);
}

} // namespace

int main() {
    std::printf("=== Slide editor core ===\n");
    auto deck = std::make_shared<PptxPresentation>();
    deck->slideWidth = 12192000;   // 13.33 x 7.5 in
    deck->slideHeight = 6858000;
    UCSlideDeckEditor editor(deck);
    int changes = 0;
    editor.onChange = [&changes]() { ++changes; };
    Check(editor.GetSlideCount() == 0 && editor.GetCurrentSlide() == -1 && !editor.CanUndo(), "an empty deck");

    std::printf("\n-- slides --\n");
    editor.NewSlide(-1);
    Check(editor.GetSlideCount() == 1 && editor.GetCurrentSlide() == 0 && editor.CurrentSlide()->shapes.size() == 2,
          "New slide: Title and Content");
    editor.SetShapeText(0, "First");
    Check(editor.CurrentSlide()->title == "First", "the title placeholder's text is the slide's title");
    editor.NewSlide(-1);
    editor.SetShapeText(0, "Second");
    editor.DuplicateSlide(1);
    Check(editor.GetSlideCount() == 3 && editor.GetCurrentSlide() == 2 && editor.CurrentSlide()->title == "Second",
          "Duplicate slide: after it, made current");
    editor.MoveSlide(2, 0);
    Check(deck->slides[0].title == "Second" && deck->slides[0].number == 1 && editor.GetCurrentSlide() == 0,
          "Move slide 3 to the front, renumbered");
    editor.Undo();
    Check(deck->slides[0].title == "First" && deck->slides[2].title == "Second", "undone");
    editor.SetSlideHidden(1, true);
    Check(deck->slides[1].hidden, "Hide slide");
    editor.DeleteSlide(2);
    Check(editor.GetSlideCount() == 2 && editor.GetCurrentSlide() == 1, "Delete slide: the one before is current");
    editor.Undo();
    Check(editor.GetSlideCount() == 3 && deck->slides[2].title == "Second", "undone: the slide is back");
    editor.SetSlideNotes(0, "Say hello\nThen go on");
    Check(deck->slides[0].notes.size() == 2 && UCSlideDeckEditor::SlideNotesText(deck->slides[0]) == "Say hello\nThen go on",
          "notes, a paragraph a line");

    std::printf("\n-- shapes --\n");
    editor.SetCurrentSlide(0);
    const int box = editor.InsertTextBox(SlideBox{kInch, kInch, 2 * kInch, kInch}, "Hello");
    Check(box == 2 && editor.GetSelection() == std::vector<int>{2} && UCSlideDeckEditor::ShapeText(Shape(editor, 2)) == "Hello",
          "a text box, selected");
    editor.MoveSelection(kInch, 0);
    Check(Shape(editor, 2).x == 2 * kInch, "moved an inch right");
    const std::string before = editor.UndoDescription();
    editor.NudgeSelection(12700, 0);
    editor.NudgeSelection(12700, 0);
    editor.NudgeSelection(0, 12700);
    Check(Shape(editor, 2).x == 2 * kInch + 25400 && Shape(editor, 2).y == kInch + 12700, "three nudges");
    editor.Undo();
    Check(Shape(editor, 2).x == 2 * kInch && Shape(editor, 2).y == kInch, "one undo takes back all three");
    editor.SetShapeBox(2, SlideBox{0, 0, 3 * kInch, 2 * kInch});
    Check(Shape(editor, 2).width == 3 * kInch && Shape(editor, 2).height == 2 * kInch, "resized");
    editor.SetShapeRotation(2, -30);
    Check(Shape(editor, 2).rotationDeg == 330.0, "turned, kept within 0..360");

    std::printf("\n-- finding --\n");
    editor.SetShapeRotation(2, 0);
    Check(editor.ShapeAt(kInch, kInch) == 2, "the topmost shape at a point");
    editor.SetShapeRotation(2, 90);   // 3 x 2 in turned: now 2 wide, 3 tall about its centre (1.5, 1)
    Check(editor.ShapeAt(static_cast<int64_t>(1.5 * kInch), static_cast<int64_t>(2.3 * kInch)) == 2 &&
              editor.ShapeAt(static_cast<int64_t>(2.8 * kInch), static_cast<int64_t>(1.0 * kInch)) != 2,
          "a turned shape is found in its own frame");
    editor.SetShapeRotation(2, 0);
    editor.Select({2});
    const double emuPerPixel = 12700;   // a point a pixel
    Check(editor.HandleAt(3 * kInch, 2 * kInch, emuPerPixel) == SlideHandle::BottomRight, "the bottom-right handle");
    Check(editor.HandleAt(static_cast<int64_t>(1.5 * kInch), -24 * 12700, emuPerPixel) == SlideHandle::Rotate,
          "the turning handle above the frame");
    Check(editor.HandleAt(static_cast<int64_t>(1.5 * kInch), kInch, emuPerPixel) == SlideHandle::NoHandle, "none inside");

    std::printf("\n-- snapping --\n");
    SlideSnap snap = editor.Snap(SlideBox{40000, 3 * kInch, kInch, kInch}, 50000, {2}, true);
    Check(snap.box.x == 0 && snap.verticalGuides == std::vector<int64_t>{0}, "the left edge to the slide's");
    const int64_t centre = editor.SlideWidth() / 2;
    snap = editor.Snap(SlideBox{centre - kInch / 2 + 30000, 4 * kInch, kInch, kInch}, 50000, {2}, true);
    Check(snap.box.x == centre - kInch / 2, "the centre to the slide's centre");
    // The placeholders left out, so the text box (0, 0, 3 x 2 in) is the only shape to line up with.
    snap = editor.Snap(SlideBox{5 * kInch, 2 * kInch + 20000, kInch, kInch}, 50000, {0, 1}, true);
    Check(snap.box.y == 2 * kInch && snap.horizontalGuides == std::vector<int64_t>{2 * kInch},
          "the top edge to another shape's bottom");
    snap = editor.Snap(SlideBox{kInch, 5 * kInch, 2 * kInch - 30000, kInch}, 50000, {0, 1}, false, SlideHandle::Right);
    Check(snap.box.width == 2 * kInch && snap.box.x == kInch, "a resized right edge to the shape's, the left one stays");
    snap = editor.Snap(SlideBox{5 * kInch, 2 * kInch + 20000, kInch, kInch}, 50000, {}, true);
    Check(snap.box.y == editor.CurrentSlide()->shapes[1].y, "the nearest edge wins: the content placeholder's top");

    std::printf("\n-- order, duplicate, delete, paste --\n");
    editor.Select({0});
    editor.BringToFront();
    Check(Shape(editor, 2).placeholder == "title" && editor.GetSelection() == std::vector<int>{2}, "Bring to front");
    editor.SendBackward();
    Check(Shape(editor, 1).placeholder == "title" && editor.GetSelection() == std::vector<int>{1}, "Send backward");
    editor.Undo();
    editor.Undo();
    Check(Shape(editor, 0).placeholder == "title" && editor.GetSelection() == std::vector<int>{0}, "both undone, the selection back");
    editor.Select({2});
    editor.DuplicateSelection();
    Check(editor.CurrentSlide()->shapes.size() == 4 && editor.GetSelection() == std::vector<int>{3} &&
              Shape(editor, 3).x == Shape(editor, 2).x + 152400,
          "Duplicate: offset, the copy selected");
    editor.DeleteSelection();
    Check(editor.CurrentSlide()->shapes.size() == 3 && editor.GetSelection().empty(), "Delete");
    editor.Undo();
    Check(editor.CurrentSlide()->shapes.size() == 4 && editor.GetSelection() == std::vector<int>{3},
          "undone: the shape back, and selected");
    editor.CopySelection();
    editor.SetCurrentSlide(1);
    editor.Paste();
    Check(editor.CurrentSlide()->shapes.size() == 3 && Shape(editor, 2).x == deck->slides[0].shapes[3].x,
          "pasted on another slide in the same place");
    editor.Paste();
    Check(Shape(editor, 3).x == Shape(editor, 2).x + 152400, "pasted again on it: offset");

    std::printf("\n-- a picture --\n");
    const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G'};
    const int picture = editor.InsertPicture(png, "png", 800, 600, "A photo");
    Check(picture >= 0 && Shape(editor, picture).kind == PptxShapeKind::Picture &&
              deck->pictures.count(Shape(editor, picture).pictureTarget) == 1,
          "its bytes kept with the deck");
    Check(Shape(editor, picture).width * 3 == Shape(editor, picture).height * 4, "its proportions kept");

    std::printf("\n-- modified, redo --\n");
    Check(editor.IsModified() && changes > 10, "the deck is modified, and every change was reported");
    editor.Undo();
    Check(editor.CanRedo(), "undo makes redo possible");
    editor.Redo();
    Check(editor.CurrentSlide()->shapes.size() == 5, "redo does it again");

    std::printf("\n-- a shape's text and the rich document --\n");
    {
        std::vector<PptxParagraph> paragraphs(3);
        PptxRun look;
        look.sizePt = 24;
        look.font = "Georgia";
        look.color = "1F4E79";
        PptxRun bold = look;
        bold.text = "Bold";
        bold.bold = true;
        PptxRun plain = look;
        plain.text = " and plain";
        PptxRun lineBreak = look;
        lineBreak.text = "\n";
        PptxRun after = look;
        after.text = "next line";
        paragraphs[0].runs = {bold, plain, lineBreak, after};
        paragraphs[0].align = PptxAlign::Center;
        paragraphs[1].bullet = true;
        paragraphs[1].bulletChar = "\xC2\xA7";   // Wingdings' square
        paragraphs[1].bulletFont = "Wingdings";
        paragraphs[1].level = 1;
        PptxRun item = look;
        item.text = "Item";
        paragraphs[1].runs = {item};
        paragraphs[2].bullet = paragraphs[2].numbered = true;
        paragraphs[2].numberScheme = "alphaLcParenR";
        paragraphs[2].numberStartAt = 3;
        PptxRun third = look;
        third.text = "Third";
        paragraphs[2].runs = {third};

        const UCRichDocument rich = PptxParagraphsToRichDocument(paragraphs);
        Check(rich.blocks.size() == 3 && rich.blocks[0].runs.size() == 3 && rich.blocks[0].runs[0].bold &&
                  rich.blocks[0].runs[2].lineBreakBefore && rich.blocks[0].runs[0].color == "#1F4E79" &&
                  rich.blocks[0].align == RichTextAlign::Center,
              "runs, look, a line break and the alignment in the document");
        Check(rich.blocks[1].type == RichBlockType::ListItem && rich.blocks[1].listLevel == 1 &&
                  rich.blocks[1].bulletText == "\xE2\x96\xAA",
              "a bullet at its level, Wingdings' square as the square [" + rich.blocks[1].bulletText + "]");
        Check(rich.blocks[2].orderedList && rich.blocks[2].numberFormat == RichNumberFormat::LowerLetter &&
                  rich.blocks[2].numberTemplate == "%1)" && rich.blocks[2].listStartNumber == 3,
              "numbering: c) as lower letters, a parenthesis, from 3");

        UCRichDocument edited = rich;
        RichDocBlock added;
        added.type = RichBlockType::ListItem;
        added.orderedList = true;
        added.numberFormat = RichNumberFormat::LowerLetter;
        added.numberTemplate = "%1)";
        RichTextRun fourth;
        fourth.text = "Fourth";
        added.runs = {fourth};
        edited.blocks.push_back(added);
        edited.blocks.emplace_back();   // an empty paragraph
        const std::vector<PptxParagraph> back = PptxParagraphsFromRichDocument(edited, paragraphs);
        Check(back.size() == 5 && back[0].runs.size() == 4 && back[0].runs[2].text == "\n" && back[0].runs[0].bold &&
                  back[0].runs[1].font == "Georgia" && back[0].runs[0].color == "1F4E79",
              "back: the runs, the line break, the look");
        Check(back[1].bulletChar == "\xC2\xA7" && back[1].bulletFont == "Wingdings", "the Wingdings bullet kept as it was");
        Check(back[3].numbered && back[3].numberScheme == "alphaLcParenR" && back[3].runs[0].sizePt == 24,
              "a new numbered item: the scheme, the size of the text before");
        Check(back[4].runs.size() == 1 && back[4].runs[0].text.empty() && back[4].runs[0].sizePt == 24,
              "an empty paragraph keeps its look");
    }

    std::printf("\n-- formatting the selected shapes --\n");
    editor.SetCurrentSlide(0);
    editor.Select({2});
    Check(!editor.AllSelectedText([](const PptxRun& r) { return r.bold; }), "not all bold");
    editor.FormatSelectedText("Bold", [](PptxRun& r) { r.bold = true; });
    Check(editor.AllSelectedText([](const PptxRun& r) { return r.bold; }) && editor.UndoDescription() == "Bold",
          "made bold, one step");
    editor.Undo();

    std::printf("\n-- tables --\n");
    const int table = editor.InsertTable(SlideBox{kInch, kInch, 6 * kInch, 3 * kInch}, 3, 3);
    const PptxShape& t = Shape(editor, table);
    Check(t.kind == PptxShapeKind::Table && t.rows.size() == 3 && t.rows[0].size() == 3 && t.cellStyles[0][0].fill == "4472C4" &&
              t.cellStyles[0][0].bold,
          "a 3 x 3 table, its header row coloured and bold");
    SlideBox cell = UCSlideDeckEditor::TableCellBox(Shape(editor, table), 1, 2);
    Check(cell.x == 5 * kInch && cell.y == 2 * kInch && cell.width == 2 * kInch && cell.height == kInch, "a cell's box");
    int row = -1, col = -1;
    Check(editor.TableCellAt(table, static_cast<int64_t>(5.5 * kInch), static_cast<int64_t>(3.5 * kInch), row, col) && row == 2 && col == 2,
          "the cell at a point");
    editor.InsertTableRow(table, 0, true);
    Check(Shape(editor, table).rows.size() == 4 && Shape(editor, table).cellStyles[1][0].fill != "4472C4" &&
              Shape(editor, table).height == 4 * kInch,
          "a row under the header: a body row, the table taller");
    editor.InsertTableColumn(table, 2, true);
    Check(Shape(editor, table).rows[0].size() == 4 && Shape(editor, table).columnWidths.size() == 4, "a column to the right");
    editor.DeleteTableColumn(table, 0);
    editor.DeleteTableRow(table, 3);
    Check(Shape(editor, table).rows.size() == 3 && Shape(editor, table).rows[0].size() == 3, "a row and a column deleted");
    editor.Undo();
    editor.Undo();
    editor.Undo();
    editor.Undo();
    Check(Shape(editor, table).rows.size() == 3 && Shape(editor, table).rows[0].size() == 3 &&
              Shape(editor, table).height == 3 * kInch,
          "all four undone");


    std::printf("\n-- groups --\n");
    {
        auto groupDeck = std::make_shared<PptxPresentation>();
        groupDeck->slideWidth = 12192000;
        groupDeck->slideHeight = 6858000;
        UCSlideDeckEditor g(groupDeck);
        g.InsertSlide(0, PptxSlide());
        const int a = g.InsertGeometry(SlideBox{kInch, kInch, kInch, kInch}, "rect");
        const int b = g.InsertGeometry(SlideBox{3 * kInch, 2 * kInch, kInch, kInch}, "ellipse");
        const int c = g.InsertGeometry(SlideBox{6 * kInch, kInch, kInch, kInch}, "triangle");
        g.Select({a, c});
        Check(g.CanGroup() && !g.CanUngroup(), "two shapes: can be grouped");
        g.GroupSelection();
        const PptxSlide* slide = g.CurrentSlide();
        // The group comes together where its frontmost member was: b, then a and c.
        Check(slide->shapes.size() == 3 && slide->shapes[0].geometry == "ellipse" && slide->shapes[1].geometry == "rect" &&
                  slide->shapes[2].geometry == "triangle",
              "Group: its shapes together in the z-order, at the frontmost's place");
        Check(slide->shapes[1].groupPath.size() == 1 && slide->shapes[1].groupPath == slide->shapes[2].groupPath &&
                  slide->shapes[0].groupPath.empty() && slide->groupNames.count(slide->shapes[1].groupPath[0]) == 1,
              "the two in one named group, the other in none");
        Check(g.GetSelection() == std::vector<int>({1, 2}) && g.SelectionIsOneGroup(), "the group stays selected");
        Check(g.UndoDescription() == "Group", "one step");
        g.ClearSelection();
        g.Select({2});
        Check(g.GetSelection() == std::vector<int>({1, 2}), "a click on one of its shapes selects the group");
        g.ToggleSelected(1);
        Check(g.GetSelection().empty(), "taking one away takes the whole group away");
        g.Select({0});
        g.ToggleSelected(2);
        Check(g.GetSelection() == std::vector<int>({0, 1, 2}), "adding one adds the whole group");
        Check(g.ShapesInside(SlideBox{0, 0, 5 * kInch, 5 * kInch}) == std::vector<int>({0}),
              "a band takes a group only when it covers all of it");

        // Moved and resized with its shapes.
        g.Select({1});
        g.MoveSelection(kInch, kInch / 2);
        Check(Shape(g, 1).x == 2 * kInch && Shape(g, 2).x == 7 * kInch && Shape(g, 2).y == kInch + kInch / 2,
              "the group moves as one");
        const SlideBox from = g.SelectionBox();
        Check(from.x == 2 * kInch && from.width == 6 * kInch && from.height == kInch, "its box is its shapes'");
        g.SetSelectionBox(from, SlideBox{from.x, from.y, from.width / 2, from.height * 2});
        Check(Shape(g, 1).width == kInch / 2 && Shape(g, 1).height == 2 * kInch && Shape(g, 2).x == 2 * kInch + 5 * kInch / 2 &&
                  Shape(g, 2).width == kInch / 2,
              "resized: its shapes scale with it");
        g.Undo();
        Check(Shape(g, 2).x == 7 * kInch && Shape(g, 2).width == kInch, "the resize undone");

        // Turned as one about its centre.
        g.RotateSelection(90);
        const SlideBox turned = g.SelectionBox();
        Check(std::abs(Shape(g, 1).rotationDeg - 90) < 1e-6 && std::abs(Shape(g, 2).rotationDeg - 90) < 1e-6 &&
                  std::abs((Shape(g, 1).x + Shape(g, 1).width / 2) - (from.x + from.width / 2)) < 2 &&
                  turned.height > 5 * kInch,
              "turned by 90 degrees: each shape turns, and goes round the group's centre");
        g.Undo();

        // A double-click enters it: one of its shapes alone.
        Check(g.EnterGroup(2) && g.GetSelection() == std::vector<int>({2}) && g.GetEnteredGroup().size() == 1,
              "EnterGroup: a shape of the group alone selected");
        g.MoveSelection(kInch, 0);
        Check(Shape(g, 2).x == 8 * kInch && Shape(g, 1).x == 2 * kInch, "inside, a shape moves alone");
        g.Select({1});
        Check(g.GetSelection() == std::vector<int>({1}), "inside, a click selects one shape");
        g.ExitGroup();
        Check(g.GetEnteredGroup().empty() && g.GetSelection() == std::vector<int>({1, 2}), "Escape: the group again");

        // The z-order keeps a group together.
        g.Select({0});
        g.BringForward();
        Check(Shape(g, 2).geometry == "ellipse" && Shape(g, 0).geometry == "rect" && Shape(g, 1).geometry == "triangle",
              "Bring forward steps over the whole group");
        g.Undo();

        // A copy of a group is a group of its own.
        g.Select({1});
        g.DuplicateSelection();
        slide = g.CurrentSlide();
        Check(slide->shapes.size() == 5 && slide->shapes[3].groupPath.size() == 1 &&
                  slide->shapes[3].groupPath == slide->shapes[4].groupPath &&
                  slide->shapes[3].groupPath != slide->shapes[1].groupPath,
              "Duplicate: the copy is a new group");
        g.Undo();

        // Ungroup, undone.
        g.Select({1});
        g.UngroupSelection();
        slide = g.CurrentSlide();
        Check(slide->shapes[1].groupPath.empty() && slide->shapes[2].groupPath.empty() && slide->groupNames.empty() &&
                  g.GetSelection() == std::vector<int>({1, 2}) && !g.SelectionIsOneGroup(),
              "Ungroup: the shapes in no group, still selected");
        g.Undo();
        Check(g.CurrentSlide()->shapes[1].groupPath.size() == 1 && g.SelectionIsOneGroup(), "Ungroup undone: the group again");
        g.Undo();   // the shape moved inside the group
        g.Undo();   // the group's move
        g.Undo();   // the group itself
        Check(g.CurrentSlide()->shapes[0].groupPath.empty() && g.CurrentSlide()->shapes[2].groupPath.empty() &&
                  g.CurrentSlide()->shapes[0].geometry == "rect",
              "Group undone: the order and the shapes as they were");
        g.Redo();
        Check(g.CurrentSlide()->shapes[2].groupPath.size() == 1, "and redone");

        // A group in a group.
        g.Select({0, 1});
        g.GroupSelection();
        slide = g.CurrentSlide();
        Check(slide->shapes[0].groupPath.size() == 1 && slide->shapes[1].groupPath.size() == 2 &&
                  slide->shapes[1].groupPath[0] == slide->shapes[0].groupPath[0] && g.GetSelection().size() == 3,
              "a group grouped with a shape: nested");
        g.ClearSelection();
        g.EnterGroup(2);
        Check(g.GetSelection() == std::vector<int>({1, 2}), "entering the outer group selects the inner one whole");
        g.EnterGroup(2);
        Check(g.GetSelection() == std::vector<int>({2}) && g.GetEnteredGroup().size() == 2, "and again: its shape");
    }

    std::printf("\n-- align and distribute --\n");
    {
        auto alignDeck = std::make_shared<PptxPresentation>();
        alignDeck->slideWidth = 10 * kInch;
        alignDeck->slideHeight = 5 * kInch;
        UCSlideDeckEditor e(alignDeck);
        e.InsertSlide(0, PptxSlide());
        e.InsertGeometry(SlideBox{kInch, kInch, kInch, kInch}, "rect");
        e.InsertGeometry(SlideBox{4 * kInch, 2 * kInch, 2 * kInch, kInch / 2}, "rect");
        e.InsertGeometry(SlideBox{8 * kInch, 3 * kInch, kInch / 2, kInch}, "rect");
        e.SelectAll();
        e.AlignSelection(SlideAlignment::Left);
        Check(Shape(e, 0).x == kInch && Shape(e, 1).x == kInch && Shape(e, 2).x == kInch && e.UndoDescription() == "Align",
              "Align left: every left edge on the leftmost");
        e.Undo();
        e.AlignSelection(SlideAlignment::Right);
        Check(Shape(e, 0).x == 7 * kInch + kInch / 2 && Shape(e, 1).x == 6 * kInch + kInch / 2 && Shape(e, 2).x == 8 * kInch,
              "Align right: on the rightmost edge");
        e.Undo();
        e.AlignSelection(SlideAlignment::Middle);
        const int64_t middle = (kInch + 4 * kInch) / 2;
        Check(Shape(e, 0).y + Shape(e, 0).height / 2 == middle && Shape(e, 1).y + Shape(e, 1).height / 2 == middle,
              "Align middles: on the selection's middle");
        e.Undo();
        e.AlignSelection(SlideAlignment::Bottom);
        Check(Shape(e, 0).y == 3 * kInch && Shape(e, 1).y == 3 * kInch + kInch / 2 && Shape(e, 2).y == 3 * kInch,
              "Align bottoms");
        e.Undo();
        e.DistributeSelection(true);
        // Widths 1, 2 and 0.5 between 1 and 8.5: 4 inches of gaps, 2 each.
        Check(Shape(e, 0).x == kInch && Shape(e, 1).x == 4 * kInch && Shape(e, 2).x == 8 * kInch &&
                  e.UndoDescription() != "Distribute",
              "Distribute across: already evenly spread, no step");
        e.MoveSelection(0, 0);
        e.Select({1});
        e.MoveSelection(kInch, 0);
        e.SelectAll();
        e.DistributeSelection(true);
        Check(Shape(e, 0).x == kInch && Shape(e, 1).x == 4 * kInch && Shape(e, 2).x == 8 * kInch &&
                  e.UndoDescription() == "Distribute",
              "Distribute across: the same gap between each two, the outer two staying");
        e.DistributeSelection(false);
        // Heights 1, 0.5 and 1 between 1 and 4: 0.5 inches of gaps, a quarter each, ordered by their middles.
        Check(Shape(e, 0).y == kInch && Shape(e, 1).y == 2 * kInch + kInch / 4 && Shape(e, 2).y == 3 * kInch,
              "Distribute down");
        e.Select({1});
        e.AlignSelection(SlideAlignment::Centre);
        Check(Shape(e, 1).x == 4 * kInch, "one shape: centred on the slide");
        e.AlignSelection(SlideAlignment::Top);
        Check(Shape(e, 1).y == 0, "one shape: to the slide's top");
        const int steps = static_cast<int>(e.CanUndo());
        e.Undo();
        Check(steps == 1 && Shape(e, 1).y == 2 * kInch + kInch / 4, "each one step");
    }

    std::printf("\n-- picture crop --\n");
    {
        auto cropDeck = std::make_shared<PptxPresentation>();
        cropDeck->slideWidth = 12192000;
        cropDeck->slideHeight = 6858000;
        UCSlideDeckEditor e(cropDeck);
        e.InsertSlide(0, PptxSlide());
        const int pic = e.InsertPicture(TestPng(), "png", 40, 30, "test");
        const SlideBox full = e.SelectionBox();
        SlideBox kept = full;
        kept.x += full.width / 4;
        kept.width -= full.width / 4;
        kept.height -= full.height / 2;
        e.SetPictureCropBox(pic, kept);
        const PptxShape& cropped = Shape(e, pic);
        Check(std::abs(cropped.cropLeft - 0.25) < 1e-3 && std::abs(cropped.cropBottom - 0.5) < 1e-3 && cropped.cropRight == 0 &&
                  cropped.cropTop == 0 && cropped.x == kept.x && cropped.width == kept.width && e.UndoDescription() == "Crop",
              "a crop handle dragged: the share cropped from each side, the box the part kept");
        const SlideBox whole = UCSlideDeckEditor::PictureFullBox(Shape(e, pic));
        Check(std::llabs(whole.x - full.x) <= 1 && std::llabs(whole.width - full.width) <= 2 &&
                  std::llabs(whole.height - full.height) <= 2,
              "PictureFullBox: the whole picture where it was, its scale kept");
        e.ResetPictureCrop(pic);
        Check(Shape(e, pic).cropLeft == 0 && Shape(e, pic).cropBottom == 0 && std::llabs(Shape(e, pic).width - full.width) <= 2,
              "Reset crop: the whole picture again");
        e.Undo();
        Check(std::abs(Shape(e, pic).cropLeft - 0.25) < 1e-3, "Reset undone: the crop back");
        e.Undo();
        Check(Shape(e, pic).cropLeft == 0 && Shape(e, pic).width == full.width, "the crop undone");
    }

    std::printf("\n-- notes --\n");
    {
        PptxParagraph p;
        PptxRun run;
        run.text = "Mention the ";
        p.runs.push_back(run);
        run.text = "numbers";
        run.bold = true;
        p.runs.push_back(run);
        editor.SetCurrentSlide(0);
        const int shown = editor.GetCurrentSlide();
        editor.SetSlideNotes(1, std::vector<PptxParagraph>{p});
        Check(deck->slides[1].notes.size() == 1 && deck->slides[1].notes[0].runs.size() == 2 &&
                  deck->slides[1].notes[0].runs[1].bold && editor.GetCurrentSlide() == shown &&
                  editor.UndoDescription() == "Notes",
              "notes as paragraphs, their look kept, one step, the slide shown left as it is");
        const UCRichDocument rich = PptxParagraphsToRichDocument(deck->slides[1].notes);
        const std::vector<PptxParagraph> back = PptxParagraphsFromRichDocument(rich, deck->slides[1].notes);
        Check(back.size() == 1 && back[0].Text() == "Mention the numbers" && back.back().runs.back().bold,
              "the notes through the rich text editor's document and back");
        editor.Undo();
        Check(deck->slides[1].notes.empty() && editor.GetCurrentSlide() == 1, "undone: no notes, on their slide");
        editor.Redo();
        Check(UCSlideDeckEditor::SlideNotesText(deck->slides[1]) == "Mention the numbers", "redone");
        std::vector<uint8_t> package;
        std::string error;
        PptxPresentation read;
        Check(WritePptxPresentationToPackage(*deck, package, error) && ReadPptxPresentationFromPackage(package, read, error) &&
                  read.slides.size() == deck->slides.size() &&
                  UCSlideDeckEditor::SlideNotesText(read.slides[1]) == "Mention the numbers",
              "the notes kept in the .pptx " + error);
        std::vector<uint8_t> odp;
        PptxPresentation readOdp;
        Check(WriteOdpPresentationToMemory(*deck, odp, error) && ReadOdpPresentationFromMemory(odp, readOdp, error) &&
                  UCSlideDeckEditor::SlideNotesText(readOdp.slides[1]) == "Mention the numbers",
              "and in the .odp " + error);
    }

    std::printf("\n-- chart data --\n");
    {
        RichChart chart;
        chart.kind = RichChart::Kind::Bar;
        chart.title = "Sales";
        chart.categories = {"North", "South"};
        RichChartSeries s1;
        s1.name = "2025";
        s1.color = "#C00000";
        s1.values = {10, 20};
        chart.series = {s1};
        const auto rows = UCSlideDeckEditor::ChartDataRows(chart);
        Check(rows.size() == 3 && rows[0].size() == 2 && rows[0][1] == "2025" && rows[1][0] == "North" && rows[2][1] == "20",
              "a chart's data as cells: the series' names along the top, a row per category");
        std::vector<std::vector<std::string>> edited = rows;
        edited[1][1] = "12.5";
        edited.push_back({"East", "7"});
        for (auto& row : edited) row.push_back("");
        edited[0][2] = "2026";
        edited[1][2] = "11";
        edited[2][2] = "21";
        edited[3][2] = "8";
        edited.push_back({"", "", ""});   // an empty row at the end
        const RichChart changed = UCSlideDeckEditor::ChartWithDataRows(chart, edited);
        Check(changed.kind == RichChart::Kind::Bar && changed.title == "Sales" && changed.categories.size() == 3 &&
                  changed.categories[2] == "East" && changed.series.size() == 2 && changed.series[0].color == "#C00000" &&
                  std::abs(changed.series[0].values[0] - 12.5) < 1e-9 && changed.series[1].name == "2026" &&
                  changed.series[1].values.size() == 3 && changed.series[1].values[2] == 8,
              "the cells back as data: kind, title and colours kept, a category and a series added");
        auto chartDeck = std::make_shared<PptxPresentation>();
        UCSlideDeckEditor e(chartDeck);
        e.InsertSlide(0, PptxSlide());
        const int shape = e.InsertChart(SlideBox{kInch, kInch, 4 * kInch, 3 * kInch}, chart);
        e.SetShapeChart(shape, changed);
        Check(Shape(e, shape).chart.series.size() == 2 && Shape(e, shape).chart.kind == RichChart::Kind::Bar &&
                  e.UndoDescription() == "Chart data",
              "applied to the chart shape as one step, its kind kept");
        e.Undo();
        Check(Shape(e, shape).chart.series.size() == 1 && Shape(e, shape).chart.series[0].values[0] == 10, "undone");
        RichChart scatter;
        scatter.kind = RichChart::Kind::Scatter;
        RichChartSeries points;
        points.name = "y";
        points.xValues = {1, 2, 4};
        points.values = {3, 5, 9};
        scatter.series = {points};
        const auto scatterRows = UCSlideDeckEditor::ChartDataRows(scatter);
        Check(scatterRows.size() == 4 && scatterRows[3][0] == "4" && scatterRows[3][1] == "9", "a scatter chart's x values first");
        const RichChart scatterBack = UCSlideDeckEditor::ChartWithDataRows(scatter, scatterRows);
        Check(scatterBack.series.size() == 1 && scatterBack.series[0].xValues == std::vector<double>({1, 2, 4}),
              "and back as x values");
    }

    std::printf("\n-- several slides --\n");
    {
        auto many = std::make_shared<PptxPresentation>();
        UCSlideDeckEditor e(many);
        for (int i = 0; i < 5; ++i) {
            PptxSlide s;
            s.title = std::string(1, static_cast<char>('A' + i));
            e.InsertSlide(-1, s);
        }
        auto titles = [&many]() {
            std::string t;
            for (const PptxSlide& s : many->slides) t += s.title;
            return t;
        };
        e.MoveSlides({0, 2}, 4);
        Check(titles() == "BDACE" && e.GetCurrentSlide() == 2 && e.UndoDescription() == "Move slides",
              "two slides moved together before the fifth, one step");
        e.Undo();
        Check(titles() == "ABCDE", "undone");
        e.MoveSlides({3, 4}, 0);
        Check(titles() == "DEABC", "two moved to the front");
        e.Undo();
        e.DuplicateSlides({1, 3});
        Check(titles() == "ABCDBDE", "two duplicated after the last of them");
        e.Undo();
        e.SetSlidesHidden({1, 2}, true);
        Check(many->slides[1].hidden && many->slides[2].hidden && !many->slides[0].hidden, "two hidden, one step");
        e.Undo();
        Check(!many->slides[1].hidden, "shown again");
        e.DeleteSlides({0, 2, 4});
        Check(titles() == "BD" && e.UndoDescription() == "Delete slides", "three deleted, one step");
        e.Undo();
        Check(titles() == "ABCDE", "and back");
    }

    std::printf("\n-- groups and crops in .pptx and .odp --\n");
    CheckRoundTrips();

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
