// Tests/VectorFileSizeTest.cpp
// The natural size and the scale of a drawing rasterized from a file in a
// format the Vector plugin reads (EMF, WMF, DXF, DWG, ODG, XAR, CDR...).
//
// A 150 x 75 pt drawing - a filled square and a frame round the page - is
// written in each format the plugin both writes and reads, and then:
//   - InspectVectorFile reports the page of the document read back as the
//     natural size (it reported 0 x 0: the plugin's GetFileInfo said none);
//   - RasterizeVectorFile at 300 px wide keeps the page's shape, and gives
//     the pixels RasterizeVectorDocument gives for the document read back
//     at that size (it made a square canvas and drew the drawing unscaled);
//   - the plugin's GetFileInfo reports the page too.
//
// Usage: VectorFileSizeTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFormatsPlugin.h"
#include "UltraCanvasCADConverters.h"
#include "UltraCanvasVectorRaster.h"
#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

std::shared_ptr<VectorDocument> Drawing() {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{150, 75};
    doc->ViewBox = Rect2Dd(0, 0, 150, 75);
    auto layer = doc->AddLayer("Drawing");
    auto frame = std::make_shared<VectorRect>();
    frame->Bounds = Rect2Dd(0, 0, 150, 75);
    StrokeData stroke;
    stroke.Fill = Color(0, 0, 0, 255);
    stroke.Width = 1;
    frame->Style.Stroke = stroke;
    layer->AddChild(frame);
    auto square = std::make_shared<VectorRect>();
    square->Bounds = Rect2Dd(10, 10, 40, 40);
    square->Style.Fill = Color(200, 0, 0, 255);
    layer->AddChild(square);
    return doc;
}

std::string Size(int w, int h) { return std::to_string(w) + " x " + std::to_string(h); }

// The largest difference in any channel, or 255 when the sizes differ.
int Difference(const UCRasterLayer& a, const UCRasterLayer& b) {
    if (a.GetWidth() != b.GetWidth() || a.GetHeight() != b.GetHeight()) return 255;
    int worst = 0;
    for (int y = 0; y < a.GetHeight(); ++y)
        for (int x = 0; x < a.GetWidth(); ++x) {
            const RasterPixel p = a.GetPixel(x, y), q = b.GetPixel(x, y);
            worst = std::max({worst, std::abs(p.r - q.r), std::abs(p.g - q.g), std::abs(p.b - q.b),
                              std::abs(p.a - q.a)});
        }
    return worst;
}

}   // namespace

int main() {
    UCImage::InitializeImageSubsysterm("VectorFileSizeTest");
    RegisterVectorFormatsPlugin();
    auto doc = Drawing();

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "uc-vector-file-size-test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    int tested = 0;
    for (const char* ext : {"emf", "wmf", "dxf", "dwg", "odg", "xar", "cdr"}) {
        std::printf("--- .%s ---\n", ext);
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(ext);
        if (!converter || !converter->CanExport() || !converter->CanImport()) {
            std::printf("  [SKIP] .%s is not both written and read in this build\n", ext);
            continue;
        }
        // DWG is written through LibreDWG's dxf2dwg, when it is installed.
        if (std::string(ext) == "dwg" && VectorConverter::DWGConverter::FindDxf2Dwg().empty()) {
            std::printf("  [SKIP] .dwg needs LibreDWG's dxf2dwg, not found\n");
            continue;
        }
        const std::string path = PathToUtf8(dir / (std::string("drawing.") + ext));
        if (!UltraCanvasVectorFormatsPlugin::SaveVectorDocument(*doc, path)) {
            Check(false, std::string(".") + ext + " written");
            continue;
        }
        auto back = UltraCanvasVectorFormatsPlugin::LoadVectorDocument(path);
        if (!back) {
            Check(false, std::string(".") + ext + " read back");
            continue;
        }
        ++tested;
        Rect2Dd page = back->ViewBox;
        if (!(page.width > 0) || !(page.height > 0)) page = Rect2Dd(0, 0, back->Size.width, back->Size.height);
        const int pw = static_cast<int>(std::lround(page.width)), ph = static_cast<int>(std::lround(page.height));

        const VectorSourceInfo info = InspectVectorFile(path);
        Check(info.ok && info.naturalWidth == pw && info.naturalHeight == ph,
              "natural size " + Size(info.naturalWidth, info.naturalHeight) + ", the page read back " + Size(pw, ph));

        const GraphicsFileInfo fileInfo = UltraCanvasVectorFormatsPlugin().GetFileInfo(path);
        Check(fileInfo.width == pw && fileInfo.height == ph,
              "GetFileInfo says " + Size(fileInfo.width, fileInfo.height));

        VectorRasterOptions o;
        o.width = 300;
        o.background = RasterPixel(255, 255, 255, 255);
        std::string error;
        auto fromFile = RasterizeVectorFile(path, o, error);
        auto fromDocument = RasterizeVectorDocument(*back, o, error);
        const int expectedH = static_cast<int>(std::lround(300.0 * page.height / page.width));
        Check(fromFile && fromFile->GetWidth() == 300 && std::abs(fromFile->GetHeight() - expectedH) <= 1,
              "300 px wide keeps the page's shape: " +
                      (fromFile ? Size(fromFile->GetWidth(), fromFile->GetHeight()) : error) + ", expected 300 x " +
                      std::to_string(expectedH));
        Check(fromFile && fromDocument && Difference(*fromFile, *fromDocument) <= 2,
              "and draws what the document read back draws at that size");
    }
    std::filesystem::remove_all(dir, ec);
    Check(tested > 0, "at least one format tested");
    std::printf("%d failure(s)\n", failures);
    return failures;
}
