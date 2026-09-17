// Tests/FilerFormatColorTest.cpp
// The file-type palette of the file display: UltraCanvasFilerWidget's
// EntryColorOf / EntryCaptionInkOf / FormatColorOf.
//
// The rules this guards, all three of them things a colour is being asked to
// say about a file:
//
//   * A FAMILY IS A HUE. Applications are not libraries, and a .dll drawn in
//     the colour of a .exe is how a folder of system plumbing came to look
//     like a folder of programs.
//   * BRIGHTNESS IS EFFICIENCY. Inside a family the modern format takes the
//     brightest rung and the legacy one the darkest (AVIF over JPEG over GIF,
//     Opus over MP3), so the ladder has to stay monotonic in luminance and
//     every rung has to be visibly apart from its neighbour.
//   * ONE INK PER FAMILY. The TreeMap draws file names on top of these
//     colours. The ink is a property of the family, so no ramp may switch ink
//     halfway down itself, and every rung must clear 3.2:1 against the ink
//     its family uses.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

std::string HexOf(const Color& c) {
    std::ostringstream os;
    os << "#" << std::uppercase << std::hex << std::setfill('0')
       << std::setw(2) << int(c.r) << std::setw(2) << int(c.g)
       << std::setw(2) << int(c.b);
    return os.str();
}

bool SameColor(const Color& a, const Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// WCAG relative luminance and contrast — the same arithmetic the palette was
// laid out with, so the test measures what the design promised.
double Channel(uint8_t v) {
    const double x = v / 255.0;
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}
double Luminance(const Color& c) {
    return 0.2126 * Channel(c.r) + 0.7152 * Channel(c.g) + 0.0722 * Channel(c.b);
}
double Contrast(const Color& a, const Color& b) {
    const double la = Luminance(a), lb = Luminance(b);
    const double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

FilerEntry FileEntry(const std::string& name, const std::string& ext,
                     FilerFileCategory category) {
    FilerEntry e;
    e.name = name;
    e.path = "/tmp/" + name;
    e.extension = ext;
    e.category = category;
    return e;
}

Color ColorOfFile(const std::string& ext, FilerFileCategory category) {
    return UltraCanvasFilerWidget::EntryColorOf(
            FileEntry("sample." + ext, ext, category));
}
Color InkOfFile(const std::string& ext, FilerFileCategory category) {
    return UltraCanvasFilerWidget::EntryCaptionInkOf(
            FileEntry("sample." + ext, ext, category));
}

// A family as the palette defines it: its extensions in rank order (rank 0
// first), the category they carry, and which way the ladder runs.
struct Family {
    std::string name;
    FilerFileCategory category;
    std::vector<std::string> byRank;  // one extension per rung, brightest-first
    bool mirrored;                    // true: rank 0 is the LIGHTEST rung
};

const std::vector<Family>& Families() {
    static const std::vector<Family> f = {
        {"Images (lossy)",    FilerFileCategory::Image,
         {"avif", "heic", "webp", "jpg", "gif"}, true},
        {"Images (lossless)", FilerFileCategory::Image,
         {"png", "qoi", "tif", "ico", "bmp"}, true},
        {"Video",             FilerFileCategory::Video,
         {"webm", "mkv", "mp4", "mov", "avi", "wmv"}, true},
        {"Audio (lossy)",     FilerFileCategory::Audio,
         {"opus", "aac", "m4b", "ogg", "mp3"}, true},
        {"Audio (lossless)",  FilerFileCategory::Audio,
         {"flac", "wav", "aiff"}, true},
        {"Vector",            FilerFileCategory::Vector,
         {"svgz", "svg", "ai", "eps", "dxf"}, true},
        {"3D models",         FilerFileCategory::Model3D,
         {"glb", "3mf", "stl", "ply", "gltf", "obj"}, true},
        {"Documents",         FilerFileCategory::Document,
         {"pdf", "epub", "odt", "docx", "doc", "rtf", "md"}, true},
        {"Applications",      FilerFileCategory::Executable,
         {"exe", "appimage", "msi", "deb"}, true},
        {"Spreadsheets",      FilerFileCategory::Spreadsheet,
         {"xlsx", "ods", "xls"}, false},
        {"Text and code",     FilerFileCategory::Text,
         {"cpp", "json", "txt", "log"}, false},
        {"Libraries",         FilerFileCategory::Library,
         {"so", "dll", "dylib", "a"}, false},
        {"Archives",          FilerFileCategory::Archive,
         {"7z", "xz", "rar", "zip", "tar"}, false},
        {"Fonts",             FilerFileCategory::Font,
         {"woff2", "woff", "otf", "ttf", "ttc", "pfb"}, false},
    };
    return f;
}

// ===== A FAMILY IS A HUE =====
void TestFamiliesAreDistinct() {
    std::cout << "\n=== Families do not share colours ===\n";

    // The split this whole change exists for.
    const Color exe = ColorOfFile("exe", FilerFileCategory::Executable);
    const Color dll = ColorOfFile("dll", FilerFileCategory::Library);
    Check(!SameColor(exe, dll),
          "a program and a library are different colours (" + HexOf(exe) +
                  " against " + HexOf(dll) + ")");
    // Red against steel grey: not a neighbouring shade, a different hue.
    Check(exe.r > 180 && exe.g < 100 && exe.b < 100,
          "applications are red (" + HexOf(exe) + ")");
    Check(std::abs(int(dll.r) - int(dll.b)) < 60 && dll.g < 140,
          "libraries stay a desaturated steel grey (" + HexOf(dll) + ")");

    // Every rung of every family, all distinct: 45 colours that never collide.
    std::set<std::string> seen;
    int collisions = 0;
    for (const Family& fam : Families()) {
        for (const std::string& ext : fam.byRank) {
            const std::string hex = HexOf(ColorOfFile(ext, fam.category));
            if (!seen.insert(hex).second) {
                std::cout << "         " << ext << " reuses " << hex << "\n";
                ++collisions;
            }
        }
    }
    Check(collisions == 0,
          "no two rungs anywhere in the palette are the same colour");

    // The four library greys the proposal asked for, all apart.
    std::set<std::string> greys;
    for (const char* ext : {"so", "dll", "dylib", "a"})
        greys.insert(HexOf(ColorOfFile(ext, FilerFileCategory::Library)));
    Check(greys.size() == 4, "the library ramp has four distinct greys");
}

// ===== BRIGHTNESS IS EFFICIENCY =====
void TestLaddersRunTheRightWay() {
    std::cout << "\n=== Efficiency ladders ===\n";
    for (const Family& fam : Families()) {
        bool monotonic = true, separated = true;
        double previous = -1.0;
        for (size_t i = 0; i < fam.byRank.size(); ++i) {
            const Color c = ColorOfFile(fam.byRank[i], fam.category);
            const double l = Luminance(c);
            if (i > 0) {
                // Mirrored families run bright to dark, the rest dark to
                // bright; either way every step has to move.
                if (fam.mirrored ? l >= previous : l <= previous) monotonic = false;
                if (Contrast(c, ColorOfFile(fam.byRank[i - 1], fam.category)) < 1.1)
                    separated = false;
            }
            previous = l;
        }
        Check(monotonic, fam.name + ": the ladder runs " +
                                 (fam.mirrored ? "brightest rung first"
                                               : "darkest rung first"));
        Check(separated, fam.name + ": neighbouring rungs are visibly apart");
    }

    // The three rankings the palette was specified by, spelled out.
    const Color avif = ColorOfFile("avif", FilerFileCategory::Image);
    const Color jpg  = ColorOfFile("jpg",  FilerFileCategory::Image);
    const Color gif  = ColorOfFile("gif",  FilerFileCategory::Image);
    Check(Luminance(avif) > Luminance(jpg) && Luminance(jpg) > Luminance(gif),
          "AVIF outranks JPEG outranks GIF");
    const Color opus = ColorOfFile("opus", FilerFileCategory::Audio);
    const Color mp3  = ColorOfFile("mp3",  FilerFileCategory::Audio);
    Check(Luminance(opus) > Luminance(mp3), "Opus outranks MP3");
    const Color webm = ColorOfFile("webm", FilerFileCategory::Video);
    const Color avi  = ColorOfFile("avi",  FilerFileCategory::Video);
    Check(Luminance(webm) > Luminance(avi), "WebM outranks AVI");

    // Formats that share a compressor share a rung: inventing a difference
    // between zip and jar would be inventing one between deflate and deflate.
    Check(SameColor(ColorOfFile("zip", FilerFileCategory::Archive),
                    ColorOfFile("jar", FilerFileCategory::Archive)),
          "zip and jar share a rung (both deflate)");
    Check(SameColor(ColorOfFile("jpg", FilerFileCategory::Image),
                    ColorOfFile("jpeg", FilerFileCategory::Image)),
          "jpg and jpeg are the same format and the same colour");

    // Lossless is a family beside its lossy sibling, not a shade of it.
    Check(!SameColor(ColorOfFile("webp", FilerFileCategory::Image),
                     ColorOfFile("png", FilerFileCategory::Image)),
          "lossless images are their own ramp");
    Check(!SameColor(ColorOfFile("mp3", FilerFileCategory::Audio),
                     ColorOfFile("flac", FilerFileCategory::Audio)),
          "lossless audio is its own ramp");
}

// ===== ONE INK PER FAMILY =====
void TestInkIsAFamilyProperty() {
    std::cout << "\n=== Caption ink ===\n";
    for (const Family& fam : Families()) {
        const Color ink = InkOfFile(fam.byRank.front(), fam.category);
        bool constant = true;
        double worst = 21.0;
        for (const std::string& ext : fam.byRank) {
            if (!SameColor(InkOfFile(ext, fam.category), ink)) constant = false;
            const double c = Contrast(ColorOfFile(ext, fam.category), ink);
            if (c < worst) worst = c;
        }
        Check(constant, fam.name + ": one ink for every rung");
        std::ostringstream os;
        os << fam.name << ": the ink clears 3.2:1 on every rung (worst "
           << std::fixed << std::setprecision(1) << worst << ":1)";
        Check(worst >= 3.2, os.str());
    }

    // The light half of the palette takes dark captions - that is the whole
    // reason the ink is computed rather than assumed to be white.
    const Color dark(28, 28, 34, 255), white(255, 255, 255, 255);
    Check(SameColor(InkOfFile("mp3", FilerFileCategory::Audio), dark),
          "audio captions are dark");
    Check(SameColor(InkOfFile("txt", FilerFileCategory::Text), dark),
          "text captions are dark");
    Check(SameColor(InkOfFile("mp4", FilerFileCategory::Video), white),
          "video captions are white");
    Check(SameColor(InkOfFile("exe", FilerFileCategory::Executable), white),
          "application captions are white");
}

// ===== WHAT THE COLOUR MUST NOT SAY =====
void TestFallbacks() {
    std::cout << "\n=== Folders, shortcuts and unranked formats ===\n";

    // A dot in a folder name is not a file type.
    FilerEntry folder = FileEntry("render.mp4", "mp4", FilerFileCategory::Folder);
    folder.isDirectory = true;
    const Color folderColor = UltraCanvasFilerWidget::EntryColorOf(folder);
    Check(SameColor(folderColor, Color(247, 190, 80, 255)),
          "a folder called \"render.mp4\" keeps the folder amber (" +
                  HexOf(folderColor) + ")");
    Check(!SameColor(folderColor, ColorOfFile("mp4", FilerFileCategory::Video)),
          "and is not drawn as a video");

    // A bundle is a directory, but it is an application.
    FilerEntry bundle = FileEntry("UltraFiler.app", "app",
                                  FilerFileCategory::Executable);
    bundle.isDirectory = true;
    bundle.isBundle = true;
    Check(SameColor(UltraCanvasFilerWidget::EntryColorOf(bundle),
                    ColorOfFile("exe", FilerFileCategory::Executable)),
          "a .app bundle is coloured as the application it is");

    // A format no ladder ranks still belongs to its family: a plugin's vector
    // format lands on the vector colour, never in the unrecognised grey.
    const Color unranked = UltraCanvasFilerWidget::FormatColorOf(
            "madeupvectorformat", FilerFileCategory::Vector);
    const Color unknown = UltraCanvasFilerWidget::FormatColorOf(
            "madeupanything", FilerFileCategory::Other);
    Check(!SameColor(unranked, unknown),
          "an unranked vector format is coloured as a vector, not as unknown");
    Check(Contrast(unranked, ColorOfFile("svg", FilerFileCategory::Vector)) < 2.0,
          "and lands inside the vector ramp rather than beside it");

    // A shortcut carries its target's category and its own extension, which
    // nothing ranks - so it takes the target's family colour.
    FilerEntry shortcut = FileEntry("Firefox.lnk", "lnk",
                                    FilerFileCategory::Executable);
    shortcut.isShortcut = true;
    Check(SameColor(UltraCanvasFilerWidget::EntryColorOf(shortcut),
                    UltraCanvasFilerWidget::FormatColorOf(
                            "lnk", FilerFileCategory::Executable)),
          "a shortcut to a program is coloured as a program");
}

}  // namespace

int main() {
    std::cout << "===== Filer format colour test =====\n";
    TestFamiliesAreDistinct();
    TestLaddersRunTheRightWay();
    TestInkIsAFamilyProperty();
    TestFallbacks();

    std::cout << "\n===== " << (g_failures == 0 ? "ALL CHECKS PASSED"
                                                : "FAILURES PRESENT")
              << " =====\n";
    return g_failures == 0 ? 0 : 1;
}
