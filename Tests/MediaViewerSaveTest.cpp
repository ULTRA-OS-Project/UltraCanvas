// Tests/MediaViewerSaveTest.cpp
// The media viewer's Save image as at another size and with the type's save
// settings (UltraCanvasMediaSurface::GetSavePicture / SaveProcessed).
//
// The viewer never told the Save dialog how big its picture was, so a bitmap
// type's Settings had no Size row there, and it wrote every file with the
// writer's defaults whatever the Settings held. An SVG has no pixel size of
// its own at all: it is a drawing, and one written larger must be drawn again
// at that size - sharp - not the picture read at its page size stretched.
//
// Runs headless: the surface loads and saves without a window. The save
// settings go to a scratch settings folder, never the real one.
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFileSaveSettings.h"
#include "UltraCanvasPathUtf8.h"

#include <cairo/cairo.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// The UltraCanvas settings folder moved into `root`, so the stored save
// settings never touch a real installation's SaveSettings.conf.
void RedirectSettingsFolder(const fs::path& root) {
    const std::string path = PathToUtf8(root);
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s("APPDATA", path.c_str());
#elif defined(__APPLE__)
    setenv("HOME", path.c_str(), 1);
#else
    setenv("XDG_CONFIG_HOME", path.c_str(), 1);
#endif
}

// A w x h picture: a horizontal grey ramp with a red diagonal, so a JPEG of
// it has detail for its quality to keep or lose.
bool WritePng(const fs::path& path, int w, int h) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_pattern_t* ramp = cairo_pattern_create_linear(0, 0, w, 0);
    cairo_pattern_add_color_stop_rgb(ramp, 0, 0, 0, 0);
    cairo_pattern_add_color_stop_rgb(ramp, 1, 1, 1, 1);
    cairo_set_source(cr, ramp);
    cairo_paint(cr);
    cairo_pattern_destroy(ramp);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, w, h);
    cairo_stroke(cr);
    cairo_destroy(cr);
    const bool ok = cairo_surface_write_to_png(s, PathToUtf8(path).c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(s);
    return ok;
}

struct Png {
    int width = 0, height = 0;
    cairo_surface_t* surface = nullptr;
    explicit Png(const fs::path& path) {
        surface = cairo_image_surface_create_from_png(PathToUtf8(path).c_str());
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) return;
        width = cairo_image_surface_get_width(surface);
        height = cairo_image_surface_get_height(surface);
    }
    ~Png() { if (surface) cairo_surface_destroy(surface); }
    // The grey level at (x, y), 0 black .. 255 white, over white.
    int Grey(int x, int y) const {
        const unsigned char* data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        const auto px = *reinterpret_cast<const uint32_t*>(data + y * stride + x * 4);
        const int g = static_cast<int>((px >> 8) & 0xFF);   // premultiplied
        if (cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32) return g;
        const int a = static_cast<int>((px >> 24) & 0xFF);
        return g + (255 - a);
    }
};

std::shared_ptr<UltraCanvasMediaSurface> Showing(const fs::path& file) {
    auto surface = std::make_shared<UltraCanvasMediaSurface>("surface");
    surface->ShowImage(UCImage::GetFresh(PathToUtf8(file)), MediaTransition::NoTransition, 0, false);
    return surface;
}

FileSaveImageSize Size(int w, int h, bool keep = true) {
    FileSaveImageSize size;
    size.width = w;
    size.height = h;
    size.keepProportions = keep;
    return size;
}

} // namespace

int main() {
    std::cout << "MediaViewerSaveTest" << std::endl;
    // An application does this on start; a headless test has to.
    UCImage::InitializeImageSubsysterm("MediaViewerSaveTest");
#ifndef HAS_LIBVIPS
    std::cout << "SKIP: saving needs libvips\n";
    return 0;
#else
    const fs::path scratch = fs::temp_directory_path() / "uc-mediaviewer-save-test";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch / "config");
    RedirectSettingsFolder(scratch / "config");

    // ----- A bitmap -----
    const fs::path photo = scratch / "photo.png";
    Check(WritePng(photo, 40, 20), "a 40 x 20 picture to show");
    auto surface = Showing(photo);
    FileSavePicture picture = surface->GetSavePicture();
    Check(picture.width == 40 && picture.height == 20 && !picture.drawing,
          "A bitmap is offered at its own size, as a bitmap");
    surface->RotateBy(1);
    picture = surface->GetSavePicture();
    Check(picture.width == 20 && picture.height == 40, "... a quarter turn swaps its sides");
    surface->RotateBy(-1);

    std::string error;
    const fs::path bigger = scratch / "bigger.png";
    Check(surface->SaveProcessed(PathToUtf8(bigger), Size(80, 40), error), "Saved at 80 x 40: " + error);
    {
        Png out(bigger);
        Check(out.width == 80 && out.height == 40, "... the file is 80 x 40");
    }
    const fs::path squashed = scratch / "squashed.png";
    Check(surface->SaveProcessed(PathToUtf8(squashed), Size(30, 30, false), error), "Saved at 30 x 30");
    {
        Png out(squashed);
        Check(out.width == 30 && out.height == 30, "... in another shape when proportions are free");
    }
    const fs::path same = scratch / "same.png";
    Check(surface->SaveProcessed(PathToUtf8(same), FileSaveImageSize(), error), "Saved at its own size");
    {
        Png out(same);
        Check(out.width == 40 && out.height == 20, "... the file is the picture's size");
    }

    // ----- FileLoader's types: an icon and a QOI too -----
    const fs::path icon = scratch / "photo.ico", quite = scratch / "photo.qoi";
    Check(surface->SaveProcessed(PathToUtf8(icon), FileSaveImageSize(), error) && fs::file_size(icon, ec) > 22,
          "Saved as an icon, a type FileLoader offers: " + error);
    Check(surface->SaveProcessed(PathToUtf8(quite), FileSaveImageSize(), error) && fs::file_size(quite, ec) > 14,
          "... and as QOI: " + error);

    // ----- The type's save settings -----
    auto storeQuality = [](int q) {
        FileSaveSettings settings = UltraCanvasFileLoader::GetSaveSettings("jpg");
        settings.SetInt("quality", q);
        return UltraCanvasFileLoader::SetSaveSettings(settings);
    };
    const fs::path big = scratch / "big.png";
    WritePng(big, 400, 300);
    auto large = Showing(big);
    const fs::path poor = scratch / "poor.jpg", fine = scratch / "fine.jpg";
    Check(storeQuality(5), "JPEG quality 5 stored");
    Check(large->SaveProcessed(PathToUtf8(poor), error), "Saved as JPEG at quality 5: " + error);
    Check(storeQuality(98), "JPEG quality 98 stored");
    Check(large->SaveProcessed(PathToUtf8(fine), error), "Saved as JPEG at quality 98");
    const auto poorBytes = fs::file_size(poor, ec), fineBytes = fs::file_size(fine, ec);
    std::cout << "         quality 5: " << poorBytes << " bytes, quality 98: " << fineBytes << " bytes\n";
    Check(poorBytes > 0 && fineBytes > poorBytes * 2, "The stored JPEG quality is what the file is written at");
    UltraCanvasFileLoader::ResetSaveSettings("jpg");

    // ----- A drawing -----
    // 20 x 10 on the page, its left half black: drawn again at 400 x 200 the
    // edge in the middle is sharp; the 20 x 10 picture stretched would fade
    // across many pixels.
    const fs::path svg = scratch / "drawing.svg";
    {
        std::ofstream out(svg, std::ios::binary);
        out << "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='10'>"
               "<rect x='0' y='0' width='10' height='10' fill='black'/></svg>";
    }
    auto drawing = Showing(svg);
    picture = drawing->GetSavePicture();
    std::cout << "         drawing offered at " << picture.width << " x " << picture.height << "\n";
    if (!drawing->GetImage() || !drawing->GetImage()->IsValid()) {
        std::cout << "  SKIP: this build does not read SVG\n";
    } else {
        Check(picture.drawing, "An SVG is offered as a drawing");
        Check(picture.width > 0 && picture.height == picture.width / 2, "... at its size on the page");
        const fs::path drawn = scratch / "drawn.png";
        Check(drawing->SaveProcessed(PathToUtf8(drawn), Size(400, 200), error), "Drawing saved at 400 x 200: " + error);
        Png out(drawn);
        Check(out.width == 400 && out.height == 200, "... the file is 400 x 200");
        if (out.width == 400 && out.height == 200) {
            int between = 0;
            for (int x = 0; x < 400; ++x) {
                const int g = out.Grey(x, 100);
                if (g > 30 && g < 225) ++between;
            }
            std::cout << "         pixels between black and white across the edge: " << between << "\n";
            Check(between <= 3, "... drawn again at that size: its edge is sharp, not stretched");
            Check(out.Grey(100, 100) < 30 && out.Grey(300, 100) > 225, "... black on the left, clear on the right");
        }
    }

    fs::remove_all(scratch, ec);
    std::cout << (g_failures == 0 ? "All save checks passed.\n" : "Save checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
#endif
}
