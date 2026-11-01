// Tests/ImageFromPixelsTest.cpp
// UCImage::FromBgra / FromRgba: a picture made from rows in memory draws its
// own pixels (opaque B G R x taken as such, straight alpha premultiplied),
// scales to a box by every fit mode, knows whether it is transparent, and -
// with libvips - saves as PNG and reads back the same.
// Version: 0.1.0
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"

#include <cairo.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {
int g_failures = 0;

void Check(bool condition, const char* what) {
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++g_failures;
}

// The ARGB32 pixel at (x, y) of a pixmap, as cairo holds it
uint32_t PixelAt(const std::shared_ptr<UCPixmapCairo>& pm, int x, int y) {
    cairo_surface_t* s = pm->GetSurface();
    cairo_surface_flush(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    return reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * stride)[x];
}
} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("ImageFromPixelsTest");

    // ---- BGRx, opaque, padded rows: left half red, right half blue ----
    {
        const int w = 4, h = 2, stride = 4 * 4 + 8;
        std::vector<uint8_t> bgrx(static_cast<size_t>(stride) * h, 0x77);   // the padding is noise
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t* p = bgrx.data() + y * stride + x * 4;
                p[0] = x < 2 ? 0 : 255;      // B
                p[1] = 0;                    // G
                p[2] = x < 2 ? 255 : 0;      // R
                p[3] = 0;                    // x: ignored
            }
        auto image = UCImage::FromBgra(bgrx.data(), w, h, stride, true);
        Check(image != nullptr && image->IsValid(), "FromBgra: a valid image");
        Check(image->GetWidth() == 4 && image->GetHeight() == 2, "FromBgra: 4x2");
        Check(!image->HasTransparency(), "FromBgra: opaque says opaque (the x byte was 0)");
        auto pm = image->GetPixmap();
        Check(pm != nullptr && pm->GetWidth() == 4 && pm->GetHeight() == 2, "FromBgra: the natural-size pixmap");
        Check(pm && PixelAt(pm, 0, 0) == 0xFFFF0000u && PixelAt(pm, 3, 1) == 0xFF0000FFu, "FromBgra: red left, blue right");
        auto big = image->GetPixmap(8, 4, ImageFitMode::Fill);
        Check(big && big->GetWidth() == 8 && big->GetHeight() == 4, "FromBgra: scaled to fill 8x4");
        Check(big && PixelAt(big, 0, 0) == 0xFFFF0000u && PixelAt(big, 7, 3) == 0xFF0000FFu, "FromBgra: the scaled pixels keep their colours");
        auto contained = image->GetPixmap(8, 8, ImageFitMode::Contain);
        Check(contained && contained->GetWidth() == 8 && contained->GetHeight() == 4, "FromBgra: contained in 8x8 keeps 2:1");
        auto down = image->GetPixmap(100, 100, ImageFitMode::ScaleDown);
        Check(down && down->GetWidth() == 4 && down->GetHeight() == 2, "FromBgra: ScaleDown never enlarges");
        auto cover = image->GetPixmap(2, 2, ImageFitMode::Cover);
        Check(cover && cover->GetWidth() == 2 && cover->GetHeight() == 2, "FromBgra: Cover is cropped to the box");
    }
    // ---- RGBA with alpha: premultiplied on the way in ----
    {
        const uint8_t rgba[2 * 1 * 4] = {255, 0, 0, 255, 0, 0, 255, 128};
        auto image = UCImage::FromRgba(rgba, 2, 1);
        Check(image != nullptr && image->IsValid(), "FromRgba: a valid image");
        Check(image->HasTransparency(), "FromRgba: half-transparent says transparent");
        auto pm = image->GetPixmap();
        Check(pm && PixelAt(pm, 0, 0) == 0xFFFF0000u, "FromRgba: R G B A read as such");
        const uint32_t half = pm ? PixelAt(pm, 1, 0) : 0;
        Check(((half >> 24) == 128) && ((half & 0xFF) == 128) && ((half >> 16) & 0xFF) == 0,
              "FromRgba: alpha 128 premultiplied (blue 128, alpha 128)");
        const uint8_t solid[4] = {1, 2, 3, 255};
        auto one = UCImage::FromRgba(solid, 1, 1);
        Check(one && !one->HasTransparency(), "FromRgba: a fully opaque alpha is not transparency");
    }
    // ---- refused ----
    Check(UCImage::FromRgba(nullptr, 2, 2) == nullptr, "FromRgba: no pixels, no image");
    {
        const uint8_t px[4] = {0, 0, 0, 255};
        Check(UCImage::FromBgra(px, 1, 1, 2, true) == nullptr, "FromBgra: a stride shorter than a row is refused");
        Check(UCImage::FromRgba(px, 0, 1) == nullptr, "FromRgba: an empty size is refused");
    }
    // ---- saved and read back (libvips) ----
    {
        const int w = 3, h = 2;
        std::vector<uint8_t> rgba(w * h * 4);
        for (int i = 0; i < w * h; ++i) {
            rgba[i * 4 + 0] = static_cast<uint8_t>(10 * i);
            rgba[i * 4 + 1] = 200;
            rgba[i * 4 + 2] = static_cast<uint8_t>(255 - 10 * i);
            rgba[i * 4 + 3] = 255;
        }
        auto image = UCImage::FromRgba(rgba.data(), w, h);
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() /
                             ("image-from-pixels-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string png = PathToUtf8(dir / "pixels.png");
        UCImageSave::ImageExportOptions options;
        options.format = UCImageSaveFormat::PNG;
        const std::string error = image ? image->Save(png, options) : "no image";
        if (error.find("no libvips") != std::string::npos) {
            std::printf("  SKIP  Save: this build has no libvips\n");
        } else {
            Check(error.empty(), "Save: a pixel-made picture is written as PNG");
            auto back = UCImage::Load(png, false);
            Check(back && back->IsValid() && back->GetWidth() == w && back->GetHeight() == h, "Save: read back at 3x2");
            auto pm = back ? back->GetPixmap() : nullptr;
            Check(pm && PixelAt(pm, 0, 0) == 0xFF00C8FFu && PixelAt(pm, 2, 1) == 0xFF32C8CDu,
                  "Save: the pixels came back as they went in");
        }
        fs::remove_all(dir, ec);
    }

    UCImage::ShutdownImageSubsysterm();
    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All image-from-pixels checks passed\n");
    return 0;
}
