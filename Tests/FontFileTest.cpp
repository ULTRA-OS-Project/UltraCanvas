// Tests/FontFileTest.cpp
// Reading and previewing font definition files - what makes a folder of
// fonts browsable in the filer without any of them being installed.
//
// Checks the extension gate, the name records read out of the framework's
// own bundled Ubuntu faces, the specimen rasterizer at several sizes, that
// a non-font fails as "no preview" rather than throwing, and that a font
// is classified as a previewable Font everywhere the filer asks.
// Version: 1.0.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework

#include "UltraCanvasFontFile.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasSupportedFormats.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

fs::path BundledFont(const std::string& name) {
    return fs::path(UC_MEDIA_DIR) / "fonts" / name;
}

// Ink is anything that is not the specimen's white background. A specimen
// that is blank, or one that is a solid block, is not letterforms - both are
// the shapes a broken rasterizer produces.
double InkFraction(const std::shared_ptr<UCPixmap>& pm) {
    const uint32_t* pixels = pm->GetPixelData();
    if (!pixels) return 0.0;
    const int w = pm->GetRawWidth(), h = pm->GetRawHeight();
    long inked = 0;
    for (long i = 0; i < static_cast<long>(w) * h; ++i) {
        if ((pixels[i] & 0x00FFFFFFu) != 0x00FFFFFFu) ++inked;
    }
    return static_cast<double>(inked) / (static_cast<double>(w) * h);
}

void TestExtensionGate() {
    std::cout << "\nExtension recognition\n";
    Check(IsFontFileExtension("Ubuntu-R.ttf"), "ttf by file name");
    Check(IsFontFileExtension("/usr/share/fonts/X.OTF"), "OTF by path, upper case");
    Check(IsFontFileExtension("woff2"), "bare extension");
    Check(IsFontFileExtension(".pfb"), "extension with leading dot");
    Check(!IsFontFileExtension("photo.png"), "png is not a font");
    Check(!IsFontFileExtension("Makefile"), "extensionless file is not a font");
    Check(!IsFontFileExtension(""), "empty string is not a font");

    Check(FontFormatForExtension("a.ttc") == FontFileFormat::TrueType,
          "ttc is TrueType");
    Check(FontFormatForExtension("a.otf") == FontFileFormat::OpenType,
          "otf is OpenType");
    Check(FontFormatForExtension("a.pcf") == FontFileFormat::BitmapFont,
          "pcf is a bitmap font");
    Check(std::string(FontFormatName(FontFileFormat::WOFF2)) == "WOFF2",
          "format names are readable");
}

void TestMetadata() {
    std::cout << "\nName records\n";
    const fs::path regular = BundledFont("Ubuntu-R.ttf");
    if (!fs::exists(regular)) {
        std::cout << "  [SKIP] " << regular.string() << " not present\n";
        return;
    }

    FontFileInfo info;
    Check(ReadFontFileInfo(regular.string(), info), "Ubuntu-R.ttf reads");
    Check(info.format == FontFileFormat::TrueType, "reported as TrueType");
    Check(info.faceCount == 1, "one face in a plain .ttf");
    Check(info.fileSize > 0, "file size filled in");
    Check(!info.faces.empty(), "a face was read");
    if (info.faces.empty()) return;

    const FontFaceInfo& face = info.faces.front();
    std::cout << "    family='" << face.family << "' subfamily='" << face.subfamily
              << "' glyphs=" << face.glyphCount << " upem=" << face.unitsPerEM << "\n";
    Check(face.family == "Ubuntu", "family name is Ubuntu");
    Check(!face.subfamily.empty(), "subfamily name read");
    Check(!face.copyright.empty(), "copyright record read");
    Check(face.glyphCount > 100, "glyph count is a real font's");
    Check(face.unitsPerEM > 0, "units per em read");
    Check(face.scalable, "outline face reports scalable");
    Check(face.hasUnicodeCharmap, "Unicode charmap found");
    Check(!face.fixedWidth, "the proportional face is not monospaced");

    // The bold face must differ from the regular one, or the style flags are
    // not being read at all.
    const fs::path bold = BundledFont("Ubuntu-B.ttf");
    if (fs::exists(bold)) {
        FontFileInfo boldInfo;
        Check(ReadFontFileInfo(bold.string(), boldInfo), "Ubuntu-B.ttf reads");
        Check(!boldInfo.faces.empty() && boldInfo.faces[0].bold,
              "the bold face reports bold");
    }

    // The monospaced family is where fixedWidth must come back true.
    const fs::path mono = BundledFont("UbuntuMono-R.ttf");
    if (fs::exists(mono)) {
        FontFileInfo monoInfo;
        Check(ReadFontFileInfo(mono.string(), monoInfo), "UbuntuMono-R.ttf reads");
        Check(!monoInfo.faces.empty() && monoInfo.faces[0].fixedWidth,
              "the mono face reports fixed width");
    }
}

void TestBadInput() {
    std::cout << "\nFiles that are not fonts\n";
    FontFileInfo info;
    Check(!ReadFontFileInfo("/no/such/directory/nothing.ttf", info),
          "a missing file fails cleanly");
    Check(!ReadFontFileInfo("", info), "an empty path fails cleanly");

    // A real, substantial file whose contents are not a font: the reader must
    // decline it rather than produce a half-filled record. A PNG is the
    // interesting case - it is exactly what a thumbnail worker hands this
    // module by mistake if the extension table ever mis-classifies one.
    const fs::path notAFont = fs::path(UC_MEDIA_DIR) / "Logo_Texter.png";
    if (!fs::exists(notAFont)) {
        std::cout << "  [SKIP] " << notAFont.string() << " not present\n";
        return;
    }
    Check(!ReadFontFileInfo(notAFont.string(), info),
          notAFont.filename().string() + " is not read as a font");
    Check(RenderFontSpecimenPixmap(notAFont.string(), 64, 64, 1.0f) == nullptr,
          notAFont.filename().string() + " produces no specimen");

    // A font file truncated mid-table: FreeType must refuse it and nothing
    // may be left half-initialised behind.
    const fs::path source = BundledFont("Ubuntu-R.ttf");
    if (fs::exists(source)) {
        const fs::path truncated =
                fs::temp_directory_path() / "ultracanvas-truncated-font.ttf";
        std::ifstream in(source, std::ios::binary);
        std::ofstream out(truncated, std::ios::binary);
        std::vector<char> head(2048);
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        out.write(head.data(), in.gcount());
        out.close();
        in.close();
        FontFileInfo truncatedInfo;
        const bool read = ReadFontFileInfo(truncated.string(), truncatedInfo);
        const bool drew = RenderFontSpecimenPixmap(truncated.string(), 64, 64,
                                                   1.0f) != nullptr;
        std::error_code rmEc;
        fs::remove(truncated, rmEc);
        // FreeType may open the header of a truncated font; what must never
        // happen is a crash, and a specimen must not come out of one.
        Check(!drew, "a truncated font produces no specimen");
        if (read) {
            std::cout << "    (FreeType accepted the truncated header - "
                         "glyph data is still gone)\n";
        }
    }
}

void TestSpecimen() {
    std::cout << "\nSpecimen rasterization\n";
    const fs::path regular = BundledFont("Ubuntu-R.ttf");
    if (!fs::exists(regular)) {
        std::cout << "  [SKIP] " << regular.string() << " not present\n";
        return;
    }
    const std::string path = regular.string();

    for (int edge : { 40, 64, 128, 256 }) {
        auto pm = RenderFontSpecimenPixmap(path, edge, edge, 1.0f);
        Check(pm != nullptr, "specimen at " + std::to_string(edge) + "px");
        if (!pm) continue;
        Check(pm->GetRawWidth() == edge && pm->GetRawHeight() == edge,
              "specimen is the requested size at " + std::to_string(edge) + "px");
        const double ink = InkFraction(pm);
        std::cout << "    " << edge << "x" << edge << ": ink "
                  << static_cast<int>(ink * 100) << "%\n";
        Check(ink > 0.01, "specimen has ink at " + std::to_string(edge) + "px");
        Check(ink < 0.5, "specimen is not a solid block at " + std::to_string(edge) + "px");
    }

    // Non-square boxes are the normal case in a details row, and HiDPI
    // thumbnails ask for device pixels the same way every other producer does.
    auto wide = RenderFontSpecimenPixmap(path, 160, 40, 1.0f);
    Check(wide && wide->GetRawWidth() == 160 && wide->GetRawHeight() == 40,
          "a wide box renders at its own aspect");
    auto hidpi = RenderFontSpecimenPixmap(path, 96, 48, 2.0f);
    Check(hidpi && hidpi->GetRawWidth() == 192 && hidpi->GetRawHeight() == 96,
          "scale 2 renders twice the device pixels");

    // The default sample follows the box: a square tile gets two glyphs so
    // they fill it, a wide one the full line. Same height, so more glyphs
    // means measurably more ink - which is what proves the rule is live
    // rather than that one shape simply renders bigger.
    auto square = RenderFontSpecimenPixmap(path, 80, 80, 1.0f);
    auto banner = RenderFontSpecimenPixmap(path, 320, 80, 1.0f);
    Check(square && banner, "square and wide specimens both render");
    if (square && banner) {
        const long squareInk = static_cast<long>(InkFraction(square) * 80 * 80);
        const long bannerInk = static_cast<long>(InkFraction(banner) * 320 * 80);
        std::cout << "    square ink " << squareInk << " px, wide ink "
                  << bannerInk << " px\n";
        Check(bannerInk > squareInk,
              "a wide box draws the longer default sample");
    }

    FontSpecimenOptions options;
    options.text = "Hamburgefonstiv";
    auto custom = RenderFontSpecimenPixmap(path, 240, 60, 1.0f, options);
    Check(custom != nullptr, "caller-supplied specimen text renders");
    Check(custom && InkFraction(custom) > 0.01, "custom specimen has ink");

    // A long string has to shrink to fit rather than run off the card.
    options.text = std::string(120, 'M');
    auto crowded = RenderFontSpecimenPixmap(path, 64, 64, 1.0f, options);
    Check(crowded != nullptr, "an over-long specimen still renders");

    Check(RenderFontSpecimenPixmap(path, 0, 32, 1.0f) == nullptr,
          "a zero-width box renders nothing");
    Check(RenderFontSpecimenPixmap(path, 32, -1, 1.0f) == nullptr,
          "a negative height renders nothing");
}

void TestClassification() {
    std::cout << "\nClassification\n";
    auto ttf = UltraCanvasSupportedFormats::FindByExtension("ttf");
    Check(ttf.has_value(), "ttf is in the format inventory");
    Check(ttf && ttf->category == MediaFormatCategory::Font, "ttf is a Font format");
    Check(ttf && ttf->canLoad && !ttf->canSave, "fonts load but do not save");
    Check(ttf && ttf->MatchesExtension("TTC"), "ttc is an alias of the ttf entry");

    auto woff2 = UltraCanvasSupportedFormats::FindByExtension(".woff2");
    Check(woff2 && woff2->category == MediaFormatCategory::Font,
          "woff2 is a Font format");

    // Loadable as a font is not loadable as an image: feeding a font to the
    // image pipeline is exactly the mis-dispatch that gate exists to prevent.
    Check(!UltraCanvasSupportedFormats::CanImagePipelineLoad("ttf"),
          "the image pipeline does not claim ttf");
    Check(UltraCanvasSupportedFormats::GetCategoryName(MediaFormatCategory::Font)
                  == "Fonts",
          "the Font category has a name");
    Check(!UltraCanvasSupportedFormats::GetLoadExtensions(
                  MediaFormatCategory::Font).empty(),
          "font load extensions are listed for file dialogs");

    // A font entry must reach the filer as a previewable kind, and the
    // all-kinds default must contain that kind.
    FilerEntry entry;
    entry.name = "Ubuntu-R.ttf";
    entry.extension = "ttf";
    entry.category = FilerFileCategory::Font;
    Check(UltraCanvasFilerWidget::PreviewTypeOf(entry) == FilerPreviewType::Fonts,
          "a font entry previews as Fonts");
    Check((kFilerAllPreviewTypes &
           static_cast<uint32_t>(FilerPreviewType::Fonts)) != 0,
          "Fonts is on by default");

    FilerEntry folder;
    folder.isDirectory = true;
    Check(UltraCanvasFilerWidget::PreviewTypeOf(folder) == FilerPreviewType::NonePreview,
          "a folder still previews as nothing");

    // The list of files behind Display > Thumbnails / Detail view must carry
    // the font formats, and must say honestly which of them this build can
    // actually rasterize: FreeType is always there, its zlib / Brotli support
    // is not, so the two web formats are the ones that may report false.
    std::map<std::string, FilerFormatInfo> formats;
    for (const FilerFormatInfo& f : UltraCanvasFilerWidget::GetPreviewableFormats())
        formats.emplace(f.extension, f);
    Check(formats.count("ttf") && formats["ttf"].kind == FilerPreviewType::Fonts,
          "ttf is in the list of files, filed under Fonts");
    Check(formats.count("ttf") && formats["ttf"].thumbnailSupported,
          "ttf advertises a thumbnail producer");
    Check(formats.count("ttf") && !formats["ttf"].label.empty(),
          "ttf carries a readable name");
    Check(formats.count("woff2") &&
                  !formats["woff2"].thumbnailSupported,
          "woff2 does not advertise one it may not have");
    Check(std::string(UltraCanvasFilerWidget::PreviewTypeLabel(
                  FilerPreviewType::Fonts)) == "Fonts",
          "the Fonts kind has a menu label");
    const auto& kinds = UltraCanvasFilerWidget::AllPreviewTypes();
    Check(std::find(kinds.begin(), kinds.end(), FilerPreviewType::Fonts)
                  != kinds.end(),
          "Fonts is one of the kinds the menus and settings enumerate");
}

} // namespace

int main() {
    std::cout << "===== Font file reading and preview =====\n";
    TestExtensionGate();
    TestMetadata();
    TestBadInput();
    TestSpecimen();
    TestClassification();
    std::cout << "\n" << (g_failures ? "FAILED" : "PASSED") << " ("
              << g_failures << " failure" << (g_failures == 1 ? "" : "s") << ")\n";
    return g_failures == 0 ? 0 : 1;
}
