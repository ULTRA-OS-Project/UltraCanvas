// Tests/VectorBitmapFillTest.cpp
// Bitmap fills (VectorStorage::MakeBitmapFill): a picture as a repeating
// pattern fill. A 2 x 2 picture - red, green over blue, yellow - fills a
// square and is checked where it lands:
//   - an upright tile repeats every tile width in both directions;
//   - a tile given as corners (turned a quarter) turns the picture with it;
//   - moving the shape (its Transform) carries the fill along;
//   - an ObjectBoundingBox pattern stretches over the shape's box;
//   - SVG writes it as <pattern><image/></pattern> and reads it back as a
//     bitmap fill that draws the same;
//   - XAR writes it as TAG_BITMAPFILL and a picture as TAG_NODE_BITMAP, and
//     both read back where they were.
//
// Usage: VectorBitmapFillTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-28
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFormatsPlugin.h"
#include "UltraCanvasVectorRaster.h"
#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "DataFormats/UltraCanvasVectorEdit.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

std::string PictureSource() {
    ClipboardDib::RgbaImage image;
    image.width = 2;
    image.height = 2;
    image.pixels = {255, 0, 0, 255,   0, 255, 0, 255,
                    0, 0, 255, 255,   255, 255, 0, 255};
    std::vector<uint8_t> png;
    ClipboardDib::EncodePng(image, png);
    return "data:image/png;base64," + Base64Encode(png, false);
}

enum class Hue { Red, Green, Blue, Yellow, Other };

const char* Name(Hue h) {
    switch (h) {
        case Hue::Red: return "red";
        case Hue::Green: return "green";
        case Hue::Blue: return "blue";
        case Hue::Yellow: return "yellow";
        default: return "other";
    }
}

struct Drawn {
    std::shared_ptr<UCRasterLayer> layer;
    Hue At(int x, int y) const {
        if (!layer) return Hue::Other;
        const RasterPixel p = layer->GetPixel(x, y);
        const bool r = p.r > 160, g = p.g > 160, b = p.b > 160;
        if (r && !g && !b) return Hue::Red;
        if (!r && g && !b) return Hue::Green;
        if (!r && !g && b) return Hue::Blue;
        if (r && g && !b) return Hue::Yellow;
        return Hue::Other;
    }
};

// The document at a pixel a unit, 100 x 100, on white.
Drawn Draw(const VectorDocument& doc) {
    VectorRasterOptions o;
    o.width = 100;
    o.height = 100;
    o.background = RasterPixel(255, 255, 255, 255);
    std::string error;
    return {RasterizeVectorDocument(doc, o, error)};
}

std::shared_ptr<VectorDocument> Square(const FillData& fill, std::shared_ptr<VectorRect>* out = nullptr) {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{100, 100};
    doc->ViewBox = Rect2Dd(0, 0, 100, 100);
    auto layer = doc->AddLayer("Layer 1");
    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd(0, 0, 100, 100);
    rect->Style.Fill = fill;
    layer->AddChild(rect);
    if (out) *out = rect;
    return doc;
}

void Expect(const Drawn& d, int x, int y, Hue want, const std::string& what) {
    const Hue got = d.At(x, y);
    Check(got == want, what + " (" + std::to_string(x) + ", " + std::to_string(y) + "): " + Name(got) +
                           ", want " + Name(want));
}

void CheckUpright(const Drawn& d, const std::string& what) {
    Expect(d, 12, 12, Hue::Red, what + ": the picture's top left");
    Expect(d, 37, 12, Hue::Green, what + ": its top right");
    Expect(d, 12, 37, Hue::Blue, what + ": its bottom left");
    Expect(d, 37, 37, Hue::Yellow, what + ": its bottom right");
    Expect(d, 62, 12, Hue::Red, what + ": repeated to the right");
    Expect(d, 87, 87, Hue::Yellow, what + ": repeated down and right");
}

}  // namespace

int main() {
    UCImage::InitializeImageSubsysterm("VectorBitmapFillTest");
    const std::string source = PictureSource();

    std::printf("--- an upright tile ---\n");
    const PatternData upright = MakeBitmapFill(source, "image/png", 2, 2, Rect2Dd(0, 0, 50, 50));
    Check(BitmapFillImage(upright) != nullptr, "the pattern holds its picture");
    CheckUpright(Draw(*Square(upright)), "upright");

    std::printf("--- a tile given as corners, a quarter turn ---\n");
    // The picture's top edge runs down the page from (50, 0), its left edge
    // to the left: red is at the top right of each tile, blue at the top left.
    const PatternData turned = MakeBitmapFill(source, "image/png", 2, 2, Point2Dd(50, 0), Point2Dd(50, 50), Point2Dd(0, 0));
    {
        const Drawn d = Draw(*Square(turned));
        Expect(d, 37, 12, Hue::Red, "turned: red at the turned top left");
        Expect(d, 12, 12, Hue::Blue, "turned: blue beside it");
        Expect(d, 37, 37, Hue::Green, "turned: green below red");
    }
    Point2Dd tl, tr, bl;
    Check(PatternTileCorners(turned, Rect2Dd(0, 0, 100, 100), tl, tr, bl) &&
          std::abs(tl.x - 50) < 1e-9 && std::abs(tr.y - 50) < 1e-9 && std::abs(bl.x) < 1e-9,
          "the corners read back as they were set");

    std::printf("--- the fill follows its shape ---\n");
    {
        std::shared_ptr<VectorRect> rect;
        auto doc = Square(upright, &rect);
        VectorEdit::TranslateElements({rect}, 25, 0);
        const Drawn d = Draw(*doc);
        Expect(d, 37, 12, Hue::Red, "moved by 25: red moved with it");
        Expect(d, 62, 12, Hue::Green, "moved by 25: green beside it");
    }

    std::printf("--- stretched over the box ---\n");
    {
        PatternData stretched = MakeBitmapFill(source, "image/png", 2, 2, Rect2Dd(0, 0, 1, 1));
        stretched.Units = GradientUnits::ObjectBoundingBox;
        const Drawn d = Draw(*Square(stretched));
        Expect(d, 25, 25, Hue::Red, "stretched: one picture over the whole square");
        Expect(d, 75, 75, Hue::Yellow, "stretched: yellow in the far corner");
    }

    std::printf("--- SVG ---\n");
    auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension("svg");
    if (!converter || !converter->CanExport() || !converter->CanImport()) {
        std::printf("  [SKIP] no SVG converter in this build\n");
    } else {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "uc-bitmap-fill-test";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        for (const auto& [name, fill] : std::vector<std::pair<std::string, PatternData>>{{"upright", upright}, {"turned", turned}}) {
            const std::string path = PathToUtf8(dir / (name + ".svg"));
            Check(UltraCanvasVectorFormatsPlugin::SaveVectorDocument(*Square(fill), path), name + ": written");
            auto back = UltraCanvasVectorFormatsPlugin::LoadVectorDocument(path);
            Check(back != nullptr, name + ": read back");
            if (!back) continue;
            const VectorElement* first = nullptr;
            for (const auto& l : back->Layers)
                if (l && !l->Children.empty()) { first = l->Children.front().get(); break; }
            const PatternData* p = first ? PatternOf(first->Style.Fill) : nullptr;
            Check(p && BitmapFillImage(*p), name + ": still a bitmap fill");
            const Drawn d = Draw(*back);
            if (name == "upright") CheckUpright(d, "SVG upright");
            else Expect(d, 37, 12, Hue::Red, "SVG turned: red at the turned top left");
        }
    }

    std::printf("--- XAR ---\n");
    auto xar = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension("xar");
    if (!xar || !xar->CanExport() || !xar->CanImport()) {
        std::printf("  [SKIP] no XAR converter in this build\n");
    } else {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "uc-bitmap-fill-test";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        // The turned fill, and the picture itself as an object over the
        // square's bottom-left quarter.
        auto doc = Square(turned);
        auto image = std::make_shared<VectorImage>();
        image->Bounds = Rect2Dd(0, 50, 50, 50);
        image->Source = source;
        image->MimeType = "image/png";
        doc->Layers.front()->AddChild(image);
        const std::string path = PathToUtf8(dir / "fill.xar");
        Check(UltraCanvasVectorFormatsPlugin::SaveVectorDocument(*doc, path), "XAR: written");
        auto back = UltraCanvasVectorFormatsPlugin::LoadVectorDocument(path);
        Check(back != nullptr, "XAR: read back");
        if (back) {
            const Drawn d = Draw(*back);
            Expect(d, 37, 12, Hue::Red, "XAR: the turned fill's red");
            Expect(d, 12, 12, Hue::Blue, "XAR: the turned fill's blue");
            Expect(d, 12, 62, Hue::Red, "XAR: the picture object's top left");
            Expect(d, 37, 87, Hue::Yellow, "XAR: the picture object's bottom right");
        }
    }

    std::printf("%d failure(s)\n", failures);
    return failures;
}
