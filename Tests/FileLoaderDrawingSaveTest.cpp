// Tests/FileLoaderDrawingSaveTest.cpp
// UltraCanvasFileLoader saving a drawing as a bitmap - what an application's
// Export of a drawing calls, so every type this build writes is on offer:
//
//   - GetVectorDocumentPixelSize: the page at 96 DPI (the model is in points);
//   - CanSaveImage: a bitmap format this build writes, never a vector one;
//   - SaveVectorDocument to a bitmap type: the page drawn at the size asked
//     for (sharp: one hard step at an edge), at the page's own size without
//     one, a JPEG on white, an icon at its sizes;
//   - AddVectorDocumentSaveTypes: the bitmap types it writes, and the page's
//     size as a drawing for the Save dialog's Settings;
//   - AddImageSaveTypes: the bitmap types alone, PNG first - what an
//     application's Save of a picture offers.
//
// Headless. The vector writers come from the Vector plugin, which this test
// does not register: only the bitmap half is checked.
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#ifdef HAS_LIBVIPS
#include <vips/vips8>
#endif

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool HasFilter(const FileDialogOptions& opts, const std::string& ext) {
    for (const FileFilter& filter : opts.filters)
        for (const std::string& e : filter.extensions)
            if (e == ext) return true;
    return false;
}

} // namespace

int main() {
    std::cout << "FileLoaderDrawingSaveTest" << std::endl;
    UCImage::InitializeImageSubsysterm("FileLoaderDrawingSaveTest");

    // A 72 x 36 point page, its left half black: 96 x 48 pixels at 96 DPI.
    VectorStorage::VectorDocument doc;
    doc.Size = Size2Dd{72, 36};
    doc.ViewBox = Rect2Dd{0, 0, 72, 36};
    auto layer = doc.AddLayer("Layer 1");
    auto black = std::make_shared<VectorStorage::VectorRect>();
    black->Bounds = Rect2Dd(0, 0, 36, 36);
    black->Style.Fill = Color(0, 0, 0, 255);
    layer->AddChild(black);

    const Size2Di page = UltraCanvasFileLoader::GetVectorDocumentPixelSize(doc);
    Check(page.width == 96 && page.height == 48, "the page at 96 DPI: 96 x 48 (got " +
          std::to_string(page.width) + " x " + std::to_string(page.height) + ")");
    Check(UltraCanvasFileLoader::CanSaveImage("png") && UltraCanvasFileLoader::CanSaveImage("photo.JPEG") &&
          !UltraCanvasFileLoader::CanSaveImage("pdf") && !UltraCanvasFileLoader::CanSaveImage("svg") &&
          !UltraCanvasFileLoader::CanSaveImage(""),
          "CanSaveImage: bitmap formats this build writes, not vector ones");

    FileDialogOptions opts;
    UltraCanvasFileLoader::AddVectorDocumentSaveTypes(opts, doc);
    Check(HasFilter(opts, "png") && HasFilter(opts, "jpg") && HasFilter(opts, "ico") && HasFilter(opts, "qoi"),
          "AddVectorDocumentSaveTypes offers the bitmap types it writes, ICO and QOI included");
    Check(opts.imageIsDrawing && opts.imageWidth == 96 && opts.imageHeight == 48,
          "... and the page's size as a drawing, for the Settings' Size row");
    FileDialogOptions images;
    UltraCanvasFileLoader::AddImageSaveTypes(images);
    Check(!images.filters.empty() && !images.filters.front().extensions.empty() &&
          images.filters.front().extensions.front() == "png" && HasFilter(images, "ico") && HasFilter(images, "qoi") &&
          !HasFilter(images, "svg") && !images.imageIsDrawing,
          "AddImageSaveTypes: every bitmap type it writes, PNG first, nothing else");
    FileDialogOptions vectorOnly;
    UltraCanvasFileLoader::AddVectorDocumentSaveTypes(vectorOnly, doc, false);
    Check(!HasFilter(vectorOnly, "png") && !vectorOnly.imageIsDrawing, "... none of that without bitmaps");

#ifdef HAS_LIBVIPS
    const fs::path dir = fs::temp_directory_path() / "uc-fileloader-drawing-save";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::string error;

    FileSaveImageSize size;
    size.width = 960;
    size.height = 480;
    const std::string big = PathToUtf8(dir / "big.png");
    Check(UltraCanvasFileLoader::SaveVectorDocument(doc, big, size, error), "saved as a 960 x 480 PNG: " + error);
    try {
        vips::VImage png = vips::VImage::new_from_file(big.c_str());
        Check(png.width() == 960 && png.height() == 480, "... at that size");
        int soft = 0;
        for (int x = 0; x < 960; ++x) {
            const double a = png.getpoint(x, 240)[3];
            if (a > 20 && a < 235) ++soft;
        }
        Check(soft <= 2, "... drawn at it, not stretched: the edge is one hard step");
        Check(png.getpoint(200, 240)[3] == 255 && png.getpoint(700, 240)[3] == 0,
              "... the drawn half opaque, the empty half transparent");
    } catch (const std::exception& e) {
        Check(false, std::string("reading the PNG: ") + e.what());
    }

    const std::string own = PathToUtf8(dir / "own.jpg");
    Check(UltraCanvasFileLoader::SaveVectorDocument(doc, own, error), "saved as a JPEG without a size: " + error);
    try {
        vips::VImage jpg = vips::VImage::new_from_file(own.c_str());
        Check(jpg.width() == 96 && jpg.height() == 48, "... at the page's own size, 96 x 48");
        Check(jpg.getpoint(80, 24)[0] > 240 && jpg.getpoint(10, 24)[0] < 15, "... laid on white");
    } catch (const std::exception& e) {
        Check(false, std::string("reading the JPEG: ") + e.what());
    }

    const std::string icon = PathToUtf8(dir / "icon.ico");
    size.width = 512;
    size.height = 256;
    Check(UltraCanvasFileLoader::SaveVectorDocument(doc, icon, size, error), "saved as an icon: " + error);
    try {
        vips::VImage ico = vips::VImage::new_from_file(icon.c_str());
        Check(ico.width() == 256 && ico.height() == 256, "... whose largest picture is 256 x 256");
    } catch (const std::exception& e) {
        Check(false, std::string("reading the icon: ") + e.what());
    }

    Check(!UltraCanvasFileLoader::SaveVectorDocument(doc, PathToUtf8(dir / "nothing"), error) && !error.empty(),
          "a name without an extension is refused with a reason");
    fs::remove_all(dir, ec);
#else
    std::cout << "  SKIP: writing bitmaps needs libvips\n";
#endif

    std::cout << (g_failures == 0 ? "All drawing save checks passed.\n" : "Drawing save checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
}
