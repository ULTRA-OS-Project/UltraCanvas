// Tests/ImageGifSaverTest.cpp
// The framework's GIF saver for libvips (VipsGifPlugin): libvips writes GIF
// only when it was built with cgif, which the MSYS2 package and the macOS
// libraries are not, so those builds could not save a GIF through libvips and
// FileLoader's list left GIF out.
//
//   - the saver is registered, and libvips picks it for a .gif only where it
//     has no gifsave of its own (vips_foreign_gif_saver names the one to use);
//   - a picture it writes reads back pixel for pixel: four colours, a
//     transparent corner, interlaced or not; a grey picture as grey;
//   - "bitdepth" caps the palette; "dither" is accepted;
//   - the buffer saver writes the same GIF to memory;
//   - every build saves GIF: write_to_file, PixelFX::FileIO::SaveGif, and
//     FileLoader's save list.
//
// Headless; needs libvips (reports SKIP without it).
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"

#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#include "../UltraCanvas/libspecific/Cairo/VipsGifPlugin.h"
#include <vips/vips8>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
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

#ifdef HAS_LIBVIPS
// 40 x 30, RGBA: red, green, blue and white quarters, the top-left 6 x 6
// transparent.
vips::VImage Quarters() {
    std::vector<uint8_t> px(40 * 30 * 4);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x) {
            uint8_t* p = &px[(y * 40 + x) * 4];
            const bool right = x >= 20, low = y >= 15;
            p[0] = (!right && !low) || (right && low) ? 255 : 0;
            p[1] = (right && !low) || (right && low) ? 255 : 0;
            p[2] = (!right && low) || (right && low) ? 255 : 0;
            p[3] = x < 6 && y < 6 ? 0 : 255;
        }
    return vips::VImage::new_from_memory_copy(px.data(), px.size(), 40, 30, 4, VIPS_FORMAT_UCHAR)
            .copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
}

// The picture at `path` shows the four quarters and the transparent corner.
bool ShowsQuarters(const std::string& path) {
    try {
        vips::VImage back = vips::VImage::new_from_file(path.c_str(), vips::VImage::option()->set("access", VIPS_ACCESS_SEQUENTIAL));
        back = back.copy_memory();
        if (back.width() != 40 || back.height() != 30 || back.bands() < 3) return false;
        auto at = [&](int x, int y) { return back.getpoint(x, y); };
        auto is = [](const std::vector<double>& p, double r, double g, double b) {
            return p[0] == r && p[1] == g && p[2] == b && (p.size() < 4 || p[3] == 255);
        };
        const bool clear = back.bands() == 4 && at(2, 2)[3] == 0;
        return clear && is(at(10, 10), 255, 0, 0) && is(at(30, 10), 0, 255, 0) && is(at(10, 25), 0, 0, 255) &&
               is(at(30, 25), 255, 255, 255);
    } catch (const vips::VError&) {
        return false;
    }
}

bool IsGif(const std::string& path) {
    FILE* f = OpenFileUtf8(path, "rb");
    if (!f) return false;
    char sig[6] = {};
    const size_t n = std::fread(sig, 1, 6, f);
    std::fclose(f);
    return n == 6 && std::memcmp(sig, "GIF89a", 6) == 0;
}
#endif

} // namespace

int main() {
    std::cout << "ImageGifSaverTest" << std::endl;
#ifndef HAS_LIBVIPS
    std::cout << "SKIP: needs libvips\n";
    return 0;
#else
    UCImage::InitializeImageSubsysterm("ImageGifSaverTest");
    const fs::path dir = fs::temp_directory_path() / "uc-gif-saver-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    auto path = [&](const char* name) { return PathToUtf8(dir / name); };

    // ----- registered, and picked only where libvips has no GIF writer -----
    const bool cgif = vips_type_find("VipsOperation", "gifsave") != 0;
    Check(vips_type_find("VipsOperation", "ucgifsave") != 0 && vips_type_find("VipsOperation", "ucgifsave_buffer") != 0,
          "the framework's GIF saver is registered with libvips");
    const char* picked = vips_foreign_find_save("picture.gif");
    Check(picked && std::string(picked) == (cgif ? "VipsForeignSaveCgifFile" : "VipsForeignSaveUcGifFile"),
          std::string("libvips picks ") + (picked ? picked : "nothing") + " for a .gif (" +
          (cgif ? "it has cgif's gifsave" : "it has no GIF writer of its own") + ")");
    Check(std::string(vips_foreign_gif_saver()) == (cgif ? "gifsave" : "ucgifsave"),
          "vips_foreign_gif_saver names the same one");

    // ----- the framework's saver: pixels, transparency, options -----
    const vips::VImage quarters = Quarters();
    Check(vips_ucgifsave(quarters.get_image(), path("plain.gif").c_str(), nullptr) == 0 && IsGif(path("plain.gif")) &&
          ShowsQuarters(path("plain.gif")),
          "ucgifsave: four colours and a transparent corner read back as they were");
    Check(vips_ucgifsave(quarters.get_image(), path("interlaced.gif").c_str(), "interlace", TRUE, "dither", 0.0,
                         nullptr) == 0 && ShowsQuarters(path("interlaced.gif")),
          "... interlaced too, with \"dither\" accepted");
    bool fewColours = false;
    if (vips_ucgifsave(quarters.get_image(), path("twocolours.gif").c_str(), "bitdepth", 1, nullptr) == 0) {
        vips::VImage back = vips::VImage::new_from_file(path("twocolours.gif").c_str()).copy_memory();
        std::set<uint32_t> colours;
        const uint8_t* p = static_cast<const uint8_t*>(back.data());
        for (size_t i = 0, n = static_cast<size_t>(back.width()) * back.height(); i < n; ++i, p += back.bands())
            if (back.bands() < 4 || p[3] == 255) colours.insert((p[0] << 16) | (p[1] << 8) | p[2]);
        fewColours = colours.size() <= 2;
    }
    Check(fewColours, "\"bitdepth\" 1: at most two colours");
    const vips::VImage grey = vips::VImage::black(16, 8).linear(1.0, 128.0).cast(VIPS_FORMAT_UCHAR);
    bool greyBack = false;
    if (vips_ucgifsave(grey.get_image(), path("grey.gif").c_str(), nullptr) == 0) {
        const std::vector<double> p = vips::VImage::new_from_file(path("grey.gif").c_str()).getpoint(3, 3);
        greyBack = p.size() >= 3 && p[0] == 128 && p[1] == 128 && p[2] == 128;
    }
    Check(greyBack, "a grey picture reads back grey");
    bool buffered = false;
    try {
        VipsBlob* blob = nullptr;
        vips::VImage::call("ucgifsave_buffer", vips::VImage::option()->set("in", quarters)->set("buffer", &blob));
        size_t length = 0;
        const void* data = blob ? vips_blob_get(blob, &length) : nullptr;
        buffered = data && length > 6 && std::memcmp(data, "GIF89a", 6) == 0;
        if (blob) vips_area_unref(VIPS_AREA(blob));
    } catch (const vips::VError&) {
    }
    Check(buffered, "ucgifsave_buffer writes the GIF to memory");

    // ----- every build saves GIF -----
    bool written = false;
    try {
        quarters.write_to_file(path("any.gif").c_str());
        written = IsGif(path("any.gif")) && ShowsQuarters(path("any.gif"));
    } catch (const vips::VError&) {
    }
    Check(written, "write_to_file writes a .gif");
    bool pixelFx = false;
    try {
        pixelFx = PixelFX::FileIO::SaveGif(PixelFX::PFXImage(quarters), path("pixelfx-named-png.png")) &&
                  IsGif(path("pixelfx-named-png.png"));
    } catch (const std::exception&) {
    }
    Check(pixelFx, "PixelFX::FileIO::SaveGif writes a GIF whatever the file is called");
    const std::vector<std::string> saves = UltraCanvasFileLoader::GetSupportedSaveExtensions(MediaFormatCategory::Bitmap);
    Check(std::find(saves.begin(), saves.end(), "gif") != saves.end(), "FileLoader lists GIF as a type it writes");

    fs::remove_all(dir, ec);
    std::cout << (g_failures == 0 ? "All GIF saver checks passed.\n" : "GIF saver checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
#endif
}
