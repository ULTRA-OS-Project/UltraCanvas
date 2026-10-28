// Tests/ImageJxlTest.cpp
// JPEG XL (.jxl) through the framework's own image pipeline, end to end:
//
//   - the format inventory offers .jxl to load and to save, with save
//     settings (UltraCanvasFileLoader);
//   - the export encoder (ExportVImage) writes a lossless .jxl that reads
//     back through UCImageRaster - the loader every viewer and Get() use -
//     pixel for pixel, its transparent strip still transparent;
//   - a lossy .jxl reads back close to the picture, and its quality option
//     reaches the encoder (quality 90 writes a bigger file than 30);
//   - UCImageRaster::Save writes a .jxl from a loaded PNG, pixel for pixel;
//   - the JXL-art samples in media/images (DemoApp's JPEG XL page) - a
//     1024 x 1024 picture at 16 bits a channel in 62 bytes, and a 2160 x 3840
//     one in 82, both drawn by a Modular predictor tree - open through
//     libvips' jxlload at their size and depth, decode through UCImageRaster
//     to a real picture (opaque, thousands of colours, not an empty frame),
//     and come back identical from a lossless .jxl that UCImageRaster::Save
//     writes.
//
// Headless. Needs a libvips built with libjxl: reports SKIP without one -
// Ubuntu 22.04's libvips (CI's Linux runner) has none, 24.04's has.
// Version: 1.1.0 - the JXL-art samples in media/images
// Version: 1.0.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"

#ifdef HAS_LIBVIPS
#include <vips/vips8>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    // Flushed: a crash in the next step still leaves every check before it
    // in the log, so CI shows which step it was in.
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << std::endl;
    if (!condition) ++g_failures;
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A bare JPEG XL codestream (FF 0A) or the ISO BMFF container ("JXL " box).
bool IsJxl(const std::vector<uint8_t>& b) {
    const uint8_t box[] = {0, 0, 0, 0x0C, 'J', 'X', 'L', ' '};
    return (b.size() > 2 && b[0] == 0xFF && b[1] == 0x0A) ||
           (b.size() > 8 && std::equal(box, box + 8, b.begin()));
}

bool Lists(std::vector<std::string> extensions, const std::string& wanted) {
    for (std::string& e : extensions) {
        if (!e.empty() && e[0] == '.') e.erase(0, 1);
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
        if (e == wanted) return true;
    }
    return false;
}

// The test picture, 96 x 64: a smooth gradient (no hard edges for a lossy
// coder to smear) with an 8-pixel transparent strip on the left.
constexpr int PicW = 96, PicH = 64, Strip = 8;
uint8_t Red(int x) { return static_cast<uint8_t>(x * 255 / (PicW - 1)); }
uint8_t Green(int y) { return static_cast<uint8_t>(y * 255 / (PicH - 1)); }
uint8_t Blue(int x, int y) { return static_cast<uint8_t>((x + y) * 255 / (PicW + PicH - 2)); }

} // namespace

int main() {
    std::cout << "ImageJxlTest" << std::endl;
    UCImage::InitializeImageSubsysterm("ImageJxlTest");
#ifndef HAS_LIBVIPS
    std::cout << "SKIP: needs libvips\n";
    return 0;
#else
    if (!vips_type_find("VipsOperation", "jxlload") || !VipsCanSave(".jxl")) {
        std::cout << "SKIP: this libvips has no JPEG XL loader / saver (built without libjxl)\n";
        return 0;
    }
    const fs::path dir = fs::temp_directory_path() / "uc-jxl-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // ----- the inventory -----
    Check(UltraCanvasFileLoader::CanSaveImage(".jxl"), "FileLoader can save an image as .jxl");
    Check(Lists(UltraCanvasFileLoader::GetSupportedLoadExtensions(MediaFormatCategory::Bitmap), "jxl"),
          "... lists .jxl among the bitmaps it loads");
    Check(Lists(UltraCanvasFileLoader::GetSupportedSaveExtensions(MediaFormatCategory::Bitmap), "jxl"),
          "... and among those it saves");
    Check(UltraCanvasFileLoader::HasSaveSettings(".jxl"), "... with JPEG XL save settings");

    std::vector<uint8_t> rgba(PicW * PicH * 4);
    for (int y = 0; y < PicH; ++y)
        for (int x = 0; x < PicW; ++x) {
            uint8_t* p = &rgba[(y * PicW + x) * 4];
            const bool clear = x < Strip;
            p[0] = clear ? 0 : Red(x);
            p[1] = clear ? 0 : Green(y);
            p[2] = clear ? 0 : Blue(x, y);
            p[3] = clear ? 0 : 255;
        }
    vips::VImage picture = vips::VImage::new_from_memory(rgba.data(), rgba.size(), PicW, PicH, 4, VIPS_FORMAT_UCHAR)
                                   .copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));

    // Reads `path` through UCImageRaster and compares its pixmap (cairo
    // ARGB32, premultiplied - exact for the opaque pixels) with the picture.
    auto readsBack = [&](const fs::path& path, int tolerance, const std::string& label) {
        const std::string file = PathToUtf8(path);
        UCImageRaster::RemoveFromCache(file);
        auto raster = UCImageRaster::Load(file, false);
        Check(raster && raster->errorMessage.empty() && raster->GetWidth() == PicW && raster->GetHeight() == PicH,
              label + ": UCImageRaster reads it back 96 x 64 " + (raster ? raster->errorMessage : ""));
        auto pixmap = raster ? raster->GetPixmap(PicW, PicH, ImageFitMode::Fill) : nullptr;
        Check(pixmap && pixmap->IsValid(), label + ": ... and decodes it");
        if (!pixmap || !pixmap->IsValid()) return;
        bool stripClear = true;
        int worst = 0;
        for (int y = 0; y < PicH; ++y)
            for (int x = 0; x < PicW; ++x) {
                const uint32_t got = pixmap->GetPixel(x, y);
                if (x < Strip) {
                    stripClear = stripClear && (got >> 24) == 0;
                    continue;
                }
                const int want[4] = {Blue(x, y), Green(y), Red(x), 255};
                for (int c = 0; c < 4; ++c)
                    worst = std::max(worst, std::abs(static_cast<int>((got >> (8 * c)) & 0xFF) - want[c]));
            }
        Check(stripClear, label + ": the transparent strip stays transparent");
        Check(worst <= tolerance, label + ": every opaque pixel within " + std::to_string(tolerance) +
                                      " (worst channel off by " + std::to_string(worst) + ")");
    };

    // ----- lossless, through the export encoder -----
    UCImageSave::ImageExportOptions options;
    options.format = UCImageSaveFormat::JXL;
    options.jxl.lossless = true;
    std::string error = ExportVImage(picture, PathToUtf8(dir / "lossless.jxl"), options);
    Check(error.empty() && IsJxl(ReadBytes(dir / "lossless.jxl")),
          "the export encoder writes a lossless .jxl " + error);
    readsBack(dir / "lossless.jxl", 0, "lossless");

    // ----- lossy -----
    options.jxl.lossless = false;
    options.jxl.quality = 75;
    error = ExportVImage(picture, PathToUtf8(dir / "lossy.jxl"), options);
    Check(error.empty() && IsJxl(ReadBytes(dir / "lossy.jxl")), "... and a lossy one, quality 75 " + error);
    // Measured 26 on libjxl 0.7, at the edge of the transparent strip; the
    // average is under 2.
    readsBack(dir / "lossy.jxl", 40, "lossy");

    auto sizeAt = [&](int quality) {
        options.jxl.quality = quality;
        const fs::path path = dir / ("q" + std::to_string(quality) + ".jxl");
        ExportVImage(picture, PathToUtf8(path), options);
        return ReadBytes(path).size();
    };
    const size_t low = sizeAt(30), high = sizeAt(90);
    Check(low > 0 && high > low, "its quality reaches the encoder: quality 30 writes " + std::to_string(low) +
                                     " bytes, 90 writes " + std::to_string(high));

    // ----- UCImageRaster::Save: a PNG in, a .jxl out -----
    picture.write_to_file(PathToUtf8(dir / "source.png").c_str());
    auto source = UCImageRaster::Load(PathToUtf8(dir / "source.png"), false);
    UCImageSave::ImageExportOptions saveOptions;
    saveOptions.format = UCImageSaveFormat::JXL;
    saveOptions.jxl.lossless = true;
    error = source ? source->Save(PathToUtf8(dir / "saved.jxl"), saveOptions) : "the PNG did not load";
    Check(error.empty() && IsJxl(ReadBytes(dir / "saved.jxl")), "UCImageRaster::Save writes a loaded PNG as .jxl " + error);
    readsBack(dir / "saved.jxl", 0, "PNG to JXL");

    // ----- the JXL-art samples in media/images -----
    // Exact pixels are not compared: jxl_art.jxl is 16 bits a channel, and
    // how that reaches the 8-bit pixmap is libvips' to decide. What is held
    // fixed is that each opens as what it is, decodes to a picture rather
    // than an empty frame, and survives a lossless round trip unchanged.
    struct Sample { const char* file; int width; int height; VipsBandFormat format; };
    const Sample samples[] = {
            {"jxl_art.jxl", 1024, 1024, VIPS_FORMAT_USHORT},
            {"jxl_art2.jxl", 2160, 3840, VIPS_FORMAT_UCHAR},
    };
    const fs::path mediaImages = fs::path(PathFromUtf8(UC_MEDIA_DIR)) / "images";
    for (const Sample& sample : samples) {
        const std::string label = sample.file;
        const std::string file = PathToUtf8(mediaImages / sample.file);
        Check(IsJxl(ReadBytes(mediaImages / sample.file)), label + ": is in media/images, a JPEG XL file");
        try {
            vips::VImage v = vips::VImage::new_from_file(file.c_str());
            Check(std::string(v.get_string("vips-loader")) == "jxlload" && v.width() == sample.width &&
                          v.height() == sample.height && v.bands() == 3 && v.format() == sample.format,
                  label + ": libvips' jxlload opens it, " + std::to_string(v.width()) + " x " +
                          std::to_string(v.height()) + ", " + vips_enum_nick(VIPS_TYPE_BAND_FORMAT, v.format()));
        } catch (const std::exception& e) {
            Check(false, label + ": libvips cannot open it: " + e.what());
            continue;
        }
        UCImageRaster::RemoveFromCache(file);
        auto raster = UCImageRaster::Load(file, false);
        auto pixmap = raster && raster->errorMessage.empty()
                              ? raster->GetPixmap(sample.width, sample.height, ImageFitMode::Fill) : nullptr;
        Check(pixmap && pixmap->IsValid() && raster->GetWidth() == sample.width &&
                      raster->GetHeight() == sample.height,
              label + ": UCImageRaster decodes it at its size " + (raster ? raster->errorMessage : ""));
        if (!pixmap || !pixmap->IsValid()) continue;
        std::unordered_set<uint32_t> colours;
        bool opaque = true;
        for (int y = 0; y < sample.height; ++y)
            for (int x = 0; x < sample.width; ++x) {
                const uint32_t p = pixmap->GetPixel(x, y);
                opaque = opaque && (p >> 24) == 0xFF;
                colours.insert(p);
            }
        Check(opaque && colours.size() > 10000,
              label + ": ... to a picture: opaque, " + std::to_string(colours.size()) + " distinct colours");

        std::cout << "  ... " << label << ": lossless round trip (UCImageRaster::Save, RemoveFromCache, Load)"
                  << std::endl;
        UCImageSave::ImageExportOptions lossless;
        lossless.format = UCImageSaveFormat::JXL;
        lossless.jxl.lossless = true;
        // Lossless is exact at any effort; 1 keeps the 8-megapixel sample's
        // encode to a second or two instead of twenty at the default 7.
        lossless.jxl.effort = 1;
        const fs::path again = dir / ("again_" + label);
        error = raster->Save(PathToUtf8(again), lossless);
        UCImageRaster::RemoveFromCache(PathToUtf8(again));
        auto back = error.empty() ? UCImageRaster::Load(PathToUtf8(again), false) : nullptr;
        auto backPixmap = back && back->errorMessage.empty()
                                  ? back->GetPixmap(sample.width, sample.height, ImageFitMode::Fill) : nullptr;
        int worst = backPixmap && backPixmap->IsValid() ? 0 : 255;
        for (int y = 0; worst == 0 && y < sample.height; ++y)
            for (int x = 0; x < sample.width; ++x)
                if (pixmap->GetPixel(x, y) != backPixmap->GetPixel(x, y)) { worst = 1; break; }
        Check(error.empty() && worst == 0,
              label + ": saved as a lossless .jxl and read back, every pixel the same " + error);
    }

    fs::remove_all(dir, ec);
    std::cout << (g_failures == 0 ? "All JPEG XL checks passed.\n" : "JPEG XL checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
#endif
}
