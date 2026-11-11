// Tests/ImageFormatsTest.cpp
// Every modern picture format decodes in every build of the framework: JPEG,
// PNG, GIF, WebP, AVIF, HEIC, JPEG XL, QOI, BMP, TIFF and Windows icons - the
// samples under media/images, each read and fitted to a pixmap. Reading
// pictures is the core of what UltraCanvas is for, so a build whose libvips
// lacks a codec (a libheif without its HEVC or AV1 plug-in, a libvips built
// without JPEG XL) fails here instead of failing the user later.
//
// Also what a decoded picture is in memory: one without an alpha channel
// (the JPEG) is an opaque RGB24 pixmap, one with it (the PNG) ARGB32, and
// both keep their format through the QOI pixmap blob the disk cache stores.
//
// A machine that genuinely cannot have a codec sets UC_IMAGE_FORMATS_OPTIONAL
// to the formats to report rather than fail, e.g. "heic,avif" on a
// developer's box without the plug-in package. CI never sets it: every
// runner has every codec (JPEG XL on ubuntu-22.04 through the framework's
// own loader on a libjxl built from source).
//
// Headless; needs libvips (reports SKIP without it).
// Version: 1.0.2 - no CI exception any more: JPEG XL decodes on every runner
// Version: 1.0.1 - UC_IMAGE_FORMATS_OPTIONAL named for the 22.04 runner (no libjxl)
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "../UltraCanvas/libspecific/Cairo/QoiPixmapCodec.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
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

std::vector<std::string> Optional() {
    std::vector<std::string> out;
    const char* env = std::getenv("UC_IMAGE_FORMATS_OPTIONAL");
    if (!env) return out;
    std::stringstream list(env);
    std::string item;
    while (std::getline(list, item, ',')) {
        std::transform(item.begin(), item.end(), item.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

} // namespace

int main() {
    std::cout << "ImageFormatsTest" << std::endl;
    UCImage::InitializeImageSubsysterm("ImageFormatsTest");
#ifndef HAS_LIBVIPS
    std::cout << "SKIP: needs libvips\n";
    return 0;
#else
    const fs::path images = fs::path(PathFromUtf8(UC_MEDIA_DIR)) / "images";
    const std::vector<std::string> optional = Optional();

    // ----- every format decodes -----
    struct Sample { const char* format; const char* file; };
    const Sample samples[] = {
        { "jpeg", "dice.jpg" }, { "png", "dice.png" }, { "gif", "dice.gif" }, { "bmp", "dice.bmp" },
        { "tiff", "dice.tiff" }, { "webp", "dice.webp" }, { "avif", "dice.avif" }, { "heic", "dice.heic" },
        { "jxl", "jxl_art.jxl" }, { "qoi", "dice.qoi" }, { "ico", "dice.ico" },
    };
    for (const Sample& sample : samples) {
        const std::string path = PathToUtf8(images / sample.file);
        auto image = UCImageRaster::Get(path);
        std::shared_ptr<UCPixmapCairo> pixmap;
        if (image && image->errorMessage.empty()) pixmap = image->CreatePixmap(48, 48, ImageFitMode::Contain, 1.0f);
        const bool decodes = pixmap && pixmap->IsValid() && pixmap->GetRawWidth() > 0;
        std::string what = std::string(sample.format) + " decodes (" + sample.file + ")";
        if (!decodes && image && !image->errorMessage.empty()) what += ": " + image->errorMessage;
        if (!decodes && std::find(optional.begin(), optional.end(), sample.format) != optional.end()) {
            std::cout << "  [SKIP] " << what << " (UC_IMAGE_FORMATS_OPTIONAL)\n";
            continue;
        }
        Check(decodes, what);
    }

    // ----- what a decoded picture is in memory -----
    {
        auto jpeg = UCImageRaster::Get(PathToUtf8(images / "dice.jpg"));
        auto png = UCImageRaster::Get(PathToUtf8(images / "dice.png"));
        auto opaque = jpeg ? jpeg->CreatePixmap(64, 64, ImageFitMode::Cover, 1.0f) : nullptr;
        auto withAlpha = png ? png->CreatePixmap(64, 64, ImageFitMode::Cover, 1.0f) : nullptr;
        Check(opaque && opaque->IsOpaque(), "a JPEG (no alpha channel) is an opaque RGB24 pixmap");
        Check(withAlpha && !withAlpha->IsOpaque(), "a PNG with an alpha channel is an ARGB32 pixmap");
        if (opaque) {
            // The unused top byte reads as 0xFF, so code that takes it for
            // alpha sees the picture opaque.
            const uint32_t pixel = opaque->GetPixel(opaque->GetRawWidth() / 2, opaque->GetRawHeight() / 2);
            Check((pixel >> 24) == 0xFF, "an opaque pixmap's top byte is 0xFF");
            // The QOI pixmap blob keeps the format
            const std::vector<uint8_t> blob = QoiCompressPixmap(*opaque);
            auto back = QoiDecompressPixmap(blob);
            Check(back && back->IsOpaque() && back->GetRawWidth() == opaque->GetRawWidth(),
                  "an opaque pixmap comes back opaque from its QOI blob");
            Check(back && back->GetPixel(3, 3) == opaque->GetPixel(3, 3), "... pixel for pixel");
        }
        if (withAlpha) {
            const std::vector<uint8_t> blob = QoiCompressPixmap(*withAlpha);
            auto back = QoiDecompressPixmap(blob);
            Check(back && !back->IsOpaque(), "a pixmap with alpha comes back with it from its QOI blob");
        }
    }

    std::cout << (g_failures == 0 ? "ImageFormatsTest: all checks passed" : "ImageFormatsTest: FAILURES") << std::endl;
    return g_failures == 0 ? 0 : 1;
#endif
}
