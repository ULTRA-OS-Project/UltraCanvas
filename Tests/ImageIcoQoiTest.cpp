// Tests/ImageIcoQoiTest.cpp
// The framework's own libvips loaders and savers for Windows icons
// (VipsIcoPlugin) and QOI (VipsQoiLoader): both written without ImageMagick,
// which many builds lack - the export encoder sent them there, so an ICO or a
// QOI file could not be written at all on such a build.
//
//   - libvips knows a saver for .ico and .qoi (what a format list asks);
//   - a QOI file round-trips its pixels, alpha included;
//   - an icon holds a square PNG picture per size (16, 32, 48, 256), none
//     larger than the picture, the picture fitted on transparency, and
//     reads back as its largest;
//   - a bitmap icon written by hand (32-bit with alpha, 24-bit with the AND
//     mask, 4-bit with a palette) reads back pixel for pixel;
//   - UCRasterDocument::SaveToFile writes both through them.
//
// Headless; needs libvips (reports SKIP without it).
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasPathUtf8.h"

#ifdef HAS_LIBVIPS
#include <vips/vips8>
#endif

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

std::vector<uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void Put16(std::vector<uint8_t>& b, int v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
void Put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); }

// An icon file of one bitmap picture: `dib` is the BITMAPINFOHEADER onwards.
std::vector<uint8_t> IconOf(int w, int h, int bpp, const std::vector<uint8_t>& dib) {
    std::vector<uint8_t> f;
    Put16(f, 0); Put16(f, 1); Put16(f, 1);
    f.push_back(static_cast<uint8_t>(w)); f.push_back(static_cast<uint8_t>(h)); f.push_back(0); f.push_back(0);
    Put16(f, 1); Put16(f, bpp); Put32(f, static_cast<uint32_t>(dib.size())); Put32(f, 22);
    f.insert(f.end(), dib.begin(), dib.end());
    return f;
}

std::vector<uint8_t> InfoHeader(int w, int h, int bpp, uint32_t colours = 0) {
    std::vector<uint8_t> b;
    Put32(b, 40); Put32(b, static_cast<uint32_t>(w)); Put32(b, static_cast<uint32_t>(h * 2));
    Put16(b, 1); Put16(b, bpp); Put32(b, 0); Put32(b, 0); Put32(b, 0); Put32(b, 0); Put32(b, colours); Put32(b, 0);
    return b;
}

#ifdef HAS_LIBVIPS
std::vector<double> At(const vips::VImage& image, int x, int y) { return image.getpoint(x, y); }

bool Is(const std::vector<double>& p, double r, double g, double b, double a) {
    return p.size() == 4 && p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}
#endif

} // namespace

int main() {
    std::cout << "ImageIcoQoiTest" << std::endl;
    UCImage::InitializeImageSubsysterm("ImageIcoQoiTest");
#ifndef HAS_LIBVIPS
    std::cout << "SKIP: needs libvips\n";
    return 0;
#else
    const fs::path dir = fs::temp_directory_path() / "uc-ico-qoi-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    Check(VipsCanSave(".ico") && VipsCanSave(".qoi"), "libvips has a saver for .ico and .qoi");

    // A 300 x 150 picture: opaque red, a transparent column at x 10.
    vips::VImage red = vips::VImage::black(300, 150, vips::VImage::option()->set("bands", 4))
                               .linear({0, 0, 0, 0}, {255, 0, 0, 255}).cast(VIPS_FORMAT_UCHAR);
    vips::VImage hole = vips::VImage::black(1, 150, vips::VImage::option()->set("bands", 4)).cast(VIPS_FORMAT_UCHAR);
    vips::VImage picture = red.insert(hole, 10, 0);

    // ----- QOI -----
    const std::string qoi = PathToUtf8(dir / "picture.qoi");
    try {
        picture.write_to_file(qoi.c_str());
        const std::vector<uint8_t> bytes = ReadBytes(dir / "picture.qoi");
        Check(bytes.size() > 14 && bytes[0] == 'q' && bytes[1] == 'o' && bytes[2] == 'i' && bytes[3] == 'f',
              "a .qoi file is written, QOI's magic first");
        vips::VImage back = vips::VImage::new_from_file(qoi.c_str(), vips::VImage::option()->set("access", "sequential"));
        Check(back.width() == 300 && back.height() == 150 && back.bands() == 4, "... and reads back 300 x 150 RGBA");
        Check(Is(At(back, 100, 50), 255, 0, 0, 255) && Is(At(back, 10, 50), 0, 0, 0, 0),
              "... pixel for pixel, the transparent column included");
    } catch (const std::exception& e) {
        Check(false, std::string("QOI round trip: ") + e.what());
    }

    // ----- ICO, written -----
    const std::string ico = PathToUtf8(dir / "picture.ico");
    try {
        picture.write_to_file(ico.c_str());
        const std::vector<uint8_t> bytes = ReadBytes(dir / "picture.ico");
        Check(bytes.size() > 6 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1 && bytes[3] == 0,
              "a .ico file is written, an icon's header first");
        const int count = bytes.size() > 6 ? bytes[4] | (bytes[5] << 8) : 0;
        std::vector<int> sizes;
        bool allPng = true;
        for (int i = 0; i < count && 6 + 16 * (i + 1) <= static_cast<int>(bytes.size()); ++i) {
            const uint8_t* e = bytes.data() + 6 + 16 * i;
            sizes.push_back(e[0] ? e[0] : 256);
            const uint32_t offset = e[12] | (e[13] << 8) | (e[14] << 16) | (static_cast<uint32_t>(e[15]) << 24);
            allPng = allPng && offset + 8 <= bytes.size() && bytes[offset] == 0x89 && bytes[offset + 1] == 'P';
        }
        Check(sizes == std::vector<int>({16, 32, 48, 256}), "it holds the usual sizes: 16, 32, 48 and 256");
        Check(allPng, "... each a PNG picture");
        vips::VImage back = vips::VImage::new_from_file(ico.c_str());
        Check(back.get_string("vips-loader") == std::string("icoload"), "it is read by the framework's own loader");
        Check(back.width() == 256 && back.height() == 256 && back.bands() == 4, "it reads back as its largest, 256 x 256");
        Check(Is(At(back, 128, 128), 255, 0, 0, 255) && At(back, 128, 10)[3] == 0,
              "... the picture fitted in the middle, transparency above it");

        vips::VImage small = vips::VImage::black(20, 20, vips::VImage::option()->set("bands", 4))
                                     .linear({0, 0, 0, 0}, {0, 0, 255, 255}).cast(VIPS_FORMAT_UCHAR);
        small.write_to_file(PathToUtf8(dir / "small.ico").c_str());
        const std::vector<uint8_t> smallBytes = ReadBytes(dir / "small.ico");
        Check(smallBytes.size() > 22 && smallBytes[4] == 1 && smallBytes[6] == 16,
              "a 20-pixel picture is not blown up: only its 16-pixel icon");
    } catch (const std::exception& e) {
        Check(false, std::string("ICO writing: ") + e.what());
    }

    // ----- The export encoder: the sizes it is told -----
    {
        UCImageSave::ImageExportOptions options;
        options.format = UCImageSaveFormat::ICO;
        options.ico.sizes = {24, 64};
        const std::string err = ExportVImage(picture, PathToUtf8(dir / "export.ico"), options);
        const std::vector<uint8_t> bytes = ReadBytes(dir / "export.ico");
        Check(err.empty() && bytes.size() > 38 && bytes[4] == 2 && bytes[6] == 24 && bytes[22] == 64,
              "the export encoder writes an icon of the sizes it is given: 24 and 64 " + err);
        options.format = UCImageSaveFormat::QOI;
        const std::string qerr = ExportVImage(picture, PathToUtf8(dir / "export.qoi"), options);
        Check(qerr.empty() && fs::file_size(dir / "export.qoi", ec) > 14, "... and a QOI file " + qerr);
    }

    // ----- ICO, bitmap pictures written by hand -----
    try {
        // 2 x 2, 32-bit BGRA: red, green / blue, half-transparent white.
        std::vector<uint8_t> dib = InfoHeader(2, 2, 32);
        const uint8_t rows[] = {255, 0, 0, 255, 255, 255, 255, 128,     // bottom row: blue, white 50 %
                                0, 0, 255, 255, 0, 255, 0, 255};        // top row: red, green
        dib.insert(dib.end(), rows, rows + sizeof(rows));
        for (int i = 0; i < 8; ++i) dib.push_back(0);                   // AND mask, 2 rows of 4 bytes
        WriteBytes(dir / "argb.ico", IconOf(2, 2, 32, dib));
        vips::VImage back = vips::VImage::new_from_file(PathToUtf8(dir / "argb.ico").c_str());
        Check(back.width() == 2 && back.height() == 2 && Is(At(back, 0, 0), 255, 0, 0, 255) &&
              Is(At(back, 1, 0), 0, 255, 0, 255) && Is(At(back, 0, 1), 0, 0, 255, 255) &&
              Is(At(back, 1, 1), 255, 255, 255, 128),
              "a 32-bit bitmap icon reads back with its alpha, top row first");

        // 2 x 2, 24-bit: the AND mask makes the top-left pixel transparent.
        std::vector<uint8_t> rgb = InfoHeader(2, 2, 24);
        const uint8_t rgbRows[] = {0, 0, 0, 0, 0, 0, 0, 0,               // bottom row: black, black (+2 padding)
                                   0, 0, 255, 0, 255, 0, 0, 0};          // top row: red, green (+2 padding)
        rgb.insert(rgb.end(), rgbRows, rgbRows + sizeof(rgbRows));
        const uint8_t mask[] = {0x00, 0, 0, 0, 0x80, 0, 0, 0};           // bottom row clear; top row: first pixel masked
        rgb.insert(rgb.end(), mask, mask + sizeof(mask));
        WriteBytes(dir / "rgb.ico", IconOf(2, 2, 24, rgb));
        back = vips::VImage::new_from_file(PathToUtf8(dir / "rgb.ico").c_str());
        Check(At(back, 0, 0)[3] == 0 && Is(At(back, 1, 0), 0, 255, 0, 255) && Is(At(back, 0, 1), 0, 0, 0, 255),
              "a 24-bit bitmap icon takes its transparency from the AND mask");

        // 2 x 1, 4-bit with a palette: index 1 (yellow), index 0 (black).
        std::vector<uint8_t> pal = InfoHeader(2, 1, 4, 2);
        const uint8_t palette[] = {0, 0, 0, 0, 0, 255, 255, 0};          // 0: black, 1: yellow (BGRX)
        pal.insert(pal.end(), palette, palette + sizeof(palette));
        const uint8_t nibbles[] = {0x10, 0, 0, 0};                        // one row, padded to 4 bytes
        pal.insert(pal.end(), nibbles, nibbles + sizeof(nibbles));
        const uint8_t palMask[] = {0, 0, 0, 0};
        pal.insert(pal.end(), palMask, palMask + sizeof(palMask));
        WriteBytes(dir / "pal.ico", IconOf(2, 1, 4, pal));
        back = vips::VImage::new_from_file(PathToUtf8(dir / "pal.ico").c_str());
        Check(back.width() == 2 && back.height() == 1 && Is(At(back, 0, 0), 255, 255, 0, 255) &&
              Is(At(back, 1, 0), 0, 0, 0, 255),
              "a 4-bit palette icon reads back through its palette");
    } catch (const std::exception& e) {
        Check(false, std::string("bitmap icons: ") + e.what());
    }

    // ----- through UCRasterDocument -----
    {
        UCRasterDocument doc(64, 32, RasterPixel(0, 128, 255, 255));
        std::string error;
        Check(doc.SaveToFile(PathToUtf8(dir / "doc.qoi"), error), "a raster document saves as QOI: " + error);
        Check(doc.SaveToFile(PathToUtf8(dir / "doc.ico"), error), "... and as an icon: " + error);
        try {
            vips::VImage q = vips::VImage::new_from_file(PathToUtf8(dir / "doc.qoi").c_str());
            vips::VImage i = vips::VImage::new_from_file(PathToUtf8(dir / "doc.ico").c_str());
            Check(q.width() == 64 && q.height() == 32 && i.width() == 48 && i.height() == 48,
                  "... the QOI at its size, the icon's largest 48 (no larger than the picture)");
        } catch (const std::exception& e) {
            Check(false, std::string("reading the document's files: ") + e.what());
        }
    }

    fs::remove_all(dir, ec);
    std::cout << (g_failures == 0 ? "All ICO / QOI checks passed.\n" : "ICO / QOI checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
#endif
}
