// Tests/ImageHeifTest.cpp
// HEIF (.heic) and AVIF (.avif) through the framework's own image pipeline,
// end to end - the libheif counterpart of ImageJxlTest:
//
//   - what this build can do is asked the way the framework asks it
//     (VipsCanDecode / VipsCanEncode: libheif decodes and encodes each
//     format only through a plug-in for it - libde265 / x265 for HEIC,
//     aom / dav1d for AVIF - so one libheif can read AVIF and not HEIC);
//   - each format it decodes: the repository's sample (media/images/dice.*)
//     opens through UCImageRaster at the size libvips reports, and decodes
//     to a picture rather than an empty frame;
//   - each format it encodes: the export encoder (ExportVImage) writes a
//     lossy file, quality 75, that reads back close to the picture with its
//     transparent strip still transparent, a lossless one that reads back
//     closer still, and UCImageRaster::Save writes one from a loaded PNG.
//
// What a platform must be able to do is set by UC_HEIF_EXPECT, which CI
// gives each row, e.g. "decode=heic,avif encode=avif no-encode=heic": a
// capability named under decode= / encode= that is missing fails, and one
// under no-encode= that is present fails - the macOS row says no-encode=heic,
// so x265 (GPL) cannot slip into the core every app links unnoticed.
// Without it the test checks what the build has, and reports SKIP where
// libvips has no HEIF loader at all.
//
// Headless.
// Version: 1.0.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"

#ifdef HAS_LIBVIPS
#include <vips/vips8>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
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

// An ISO BMFF file whose "ftyp" box names the brand: "heic" (or "heix",
// "mif1") for HEIC, "avif" for AVIF.
std::string Brand(const std::vector<uint8_t>& b) {
    if (b.size() < 12 || b[4] != 'f' || b[5] != 't' || b[6] != 'y' || b[7] != 'p') return "";
    return std::string(b.begin() + 8, b.begin() + 12);
}

// UC_HEIF_EXPECT: "decode=heic,avif encode=avif no-encode=heic".
std::map<std::string, std::set<std::string>> Expectations() {
    std::map<std::string, std::set<std::string>> out;
    const char* env = std::getenv("UC_HEIF_EXPECT");
    if (!env) return out;
    std::istringstream words(env);
    std::string word;
    while (words >> word) {
        const size_t eq = word.find('=');
        if (eq == std::string::npos) continue;
        std::istringstream list(word.substr(eq + 1));
        std::string item;
        while (std::getline(list, item, ',')) if (!item.empty()) out[word.substr(0, eq)].insert(item);
    }
    return out;
}

// The test picture, 96 x 64: a smooth gradient (no hard edges for a lossy
// coder to smear) with an 8-pixel transparent strip on the left - as in
// ImageJxlTest.
constexpr int PicW = 96, PicH = 64, Strip = 8;
uint8_t Red(int x) { return static_cast<uint8_t>(x * 255 / (PicW - 1)); }
uint8_t Green(int y) { return static_cast<uint8_t>(y * 255 / (PicH - 1)); }
uint8_t Blue(int x, int y) { return static_cast<uint8_t>((x + y) * 255 / (PicW + PicH - 2)); }

} // namespace

int main() {
    std::cout << "ImageHeifTest" << std::endl;
    UCImage::InitializeImageSubsysterm("ImageHeifTest");
    const auto expect = Expectations();
#ifndef HAS_LIBVIPS
    std::cout << "SKIP: needs libvips\n";
    return expect.empty() ? 0 : 1;
#else
    struct Format {
        const char* name;          // as UC_HEIF_EXPECT names it
        const char* extension;
        UCImageSaveFormat save;
        const char* brand;         // the ftyp brand its files carry
    };
    const Format formats[] = {
            {"heic", ".heic", UCImageSaveFormat::HEIF, "heic"},
            {"avif", ".avif", UCImageSaveFormat::AVIF, "avif"},
    };

    if (!VipsCanLoad(".heic") && !VipsCanLoad(".avif")) {
        std::cout << "SKIP: this libvips has no HEIF loader (built without libheif)\n";
        if (!expect.empty()) {
            std::cout << "  [FAIL] UC_HEIF_EXPECT is set, so this build had to read HEIF\n";
            return 1;
        }
        return 0;
    }

    std::map<std::string, bool> decodes, encodes;
    for (const Format& f : formats) {
        decodes[f.name] = VipsCanDecode(f.extension);
        encodes[f.name] = VipsCanEncode(f.extension);
    }
    std::cout << "  This build: decodes HEIC " << (decodes["heic"] ? "yes" : "no") << ", AVIF "
              << (decodes["avif"] ? "yes" : "no") << "; encodes HEIC " << (encodes["heic"] ? "yes" : "no")
              << ", AVIF " << (encodes["avif"] ? "yes" : "no") << "\n";
    auto expected = [&](const char* key, const std::string& name) {
        auto it = expect.find(key);
        return it != expect.end() && it->second.count(name) > 0;
    };
    for (const Format& f : formats) {
        if (expected("decode", f.name))
            Check(decodes[f.name], std::string("UC_HEIF_EXPECT: this build decodes ") + f.name);
        if (expected("encode", f.name))
            Check(encodes[f.name], std::string("UC_HEIF_EXPECT: this build encodes ") + f.name);
        if (expected("no-encode", f.name))
            Check(!encodes[f.name], std::string("UC_HEIF_EXPECT: this build does NOT encode ") + f.name +
                                            " (its encoder is kept out of this build)");
    }

    const fs::path dir = fs::temp_directory_path() / "uc-heif-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const fs::path mediaImages = fs::path(PathFromUtf8(UC_MEDIA_DIR)) / "images";

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

    picture.write_to_file(PathToUtf8(dir / "source.png").c_str());

    for (const Format& f : formats) {
        const std::string name = f.name;

        // ----- decoding: the repository's sample -----
        if (decodes[name]) {
            const fs::path sample = mediaImages / (std::string("dice") + f.extension);
            const std::string file = PathToUtf8(sample);
            int width = 0, height = 0;
            try {
                vips::VImage v = vips::VImage::new_from_file(file.c_str());
                width = v.width();
                height = v.height();
            } catch (const std::exception& e) {
                Check(false, name + ": libvips cannot open dice" + f.extension + ": " + e.what());
                continue;
            }
            UCImageRaster::RemoveFromCache(file);
            auto raster = UCImageRaster::Load(file, false);
            auto pixmap = raster && raster->errorMessage.empty() ? raster->GetPixmap(width, height, ImageFitMode::Fill)
                                                                 : nullptr;
            Check(pixmap && pixmap->IsValid() && raster->GetWidth() == width && raster->GetHeight() == height,
                  name + ": UCImageRaster decodes dice" + f.extension + " at its size, " + std::to_string(width) +
                          " x " + std::to_string(height) + " " + (raster ? raster->errorMessage : ""));
            if (pixmap && pixmap->IsValid()) {
                std::unordered_set<uint32_t> colours;
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x) colours.insert(pixmap->GetPixel(x, y));
                Check(colours.size() > 1000,
                      name + ": ... to a picture, " + std::to_string(colours.size()) + " distinct colours");
            }
        }

        // ----- encoding: the export encoder and UCImageRaster::Save -----
        if (!encodes[name]) continue;
        UCImageSave::ImageExportOptions options;
        options.format = f.save;
        options.avif.quality = options.heif.quality = 75;
        const fs::path lossy = dir / (std::string("lossy") + f.extension);
        std::string error = ExportVImage(picture, PathToUtf8(lossy), options);
        Check(error.empty() && Brand(ReadBytes(lossy)) == f.brand,
              name + ": the export encoder writes a lossy file, quality 75, brand " + Brand(ReadBytes(lossy)) +
                      " " + error);

        options.avif.lossless = options.heif.lossless = true;
        const fs::path lossless = dir / (std::string("lossless") + f.extension);
        error = ExportVImage(picture, PathToUtf8(lossless), options);
        Check(error.empty() && Brand(ReadBytes(lossless)) == f.brand, name + ": ... and a lossless one " + error);

        auto source = UCImageRaster::Load(PathToUtf8(dir / "source.png"), false);
        UCImageSave::ImageExportOptions saveOptions;
        saveOptions.format = f.save;
        saveOptions.avif.quality = saveOptions.heif.quality = 75;
        const fs::path saved = dir / (std::string("saved") + f.extension);
        error = source ? source->Save(PathToUtf8(saved), saveOptions) : "the PNG did not load";
        Check(error.empty() && Brand(ReadBytes(saved)) == f.brand,
              name + ": UCImageRaster::Save writes a loaded PNG as " + f.extension + " " + error);

        if (!decodes[name]) {
            std::cout << "  (" << name << " files are written but not read back: this build has no decoder for them)\n";
            continue;
        }
        readsBack(lossy, 40, name + " lossy");
        readsBack(lossless, 8, name + " lossless");
        readsBack(saved, 40, name + " PNG to " + f.extension);
    }

    fs::remove_all(dir, ec);
    std::cout << (g_failures == 0 ? "All HEIF / AVIF checks passed.\n" : "HEIF / AVIF checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
#endif
}
