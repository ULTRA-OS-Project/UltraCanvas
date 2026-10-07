// Tests/ClipboardDibTest.cpp
// UltraCanvasClipboardDib: the CF_DIB / CF_DIBV5 <-> PNG conversion the
// Windows clipboard backend puts every image through.
//
// Before it existed the backend wrote PNG bytes under CF_DIB, which no other
// program can read, and handed the raw DIB of a picture copied elsewhere to
// the framework as "image/bmp", which no decoder can read either - so images
// went neither into nor out of an UltraCanvas application on Windows. The
// conversion is plain byte work, so it is tested on every platform:
//   * a picture with transparency survives CF_DIBV5 exactly;
//   * CF_DIB is 24-bit with transparency flattened onto white;
//   * PNG round-trips through cairo (exactly where alpha is 0 or 255);
//   * what other programs write decodes: an 8-bit palette, 16-bit 5-6-5
//     bit fields with the masks after a plain header, 32-bit BI_RGB, rows
//     stored top-down or bottom-up, a 12-byte core header;
//   * a 32-bit DIB whose alpha byte is zero everywhere reads as opaque, and
//     a BI_RGB one's fourth byte is never taken for alpha;
//   * a PNG in a padded memory block is measured to its IEND;
//   * truncated and absurd headers are refused instead of read past.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasClipboardDib.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace UltraCanvas::ClipboardDib;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

void PutU16(std::vector<uint8_t>& v, size_t at, uint16_t x) {
    v[at] = static_cast<uint8_t>(x);
    v[at + 1] = static_cast<uint8_t>(x >> 8);
}

void PutU32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
}

uint32_t GetU32(const std::vector<uint8_t>& v, size_t at) {
    return v[at] | (v[at + 1] << 8) | (v[at + 2] << 16) | (static_cast<uint32_t>(v[at + 3]) << 24);
}

// A BITMAPINFOHEADER with the fields the tests vary.
std::vector<uint8_t> InfoHeader(int width, int height, int bpp, uint32_t compression, uint32_t colorsUsed = 0) {
    std::vector<uint8_t> h(40, 0);
    PutU32(h, 0, 40);
    PutU32(h, 4, static_cast<uint32_t>(width));
    PutU32(h, 8, static_cast<uint32_t>(height));
    PutU16(h, 12, 1);
    PutU16(h, 14, static_cast<uint16_t>(bpp));
    PutU32(h, 16, compression);
    PutU32(h, 32, colorsUsed);
    return h;
}

// A 3 x 2 picture: opaque red, half-transparent green, invisible blue on the
// top row; white, black and a quarter-opaque grey below.
RgbaImage Sample() {
    RgbaImage img;
    img.width = 3;
    img.height = 2;
    img.pixels = {
        255, 0, 0, 255,    0, 255, 0, 128,    0, 0, 255, 0,
        255, 255, 255, 255, 0, 0, 0, 255,     100, 100, 100, 64,
    };
    return img;
}

const uint8_t* Pixel(const RgbaImage& img, int x, int y) {
    return img.pixels.data() + (static_cast<size_t>(y) * img.width + x) * 4;
}

bool Near(int a, int b, int tolerance) { return std::abs(a - b) <= tolerance; }

void TestDibV5RoundTrip() {
    const RgbaImage src = Sample();
    const std::vector<uint8_t> dib = EncodeDibV5(src);
    Check(dib.size() == 124 + 3 * 2 * 4, "DIBV5 is a 124-byte header and 32-bit pixels");
    Check(GetU32(dib, 0) == 124 && GetU32(dib, 16) == 3, "DIBV5 header is V5 with BI_BITFIELDS");
    Check(GetU32(dib, 52) == 0xFF000000u, "DIBV5 names an alpha mask");
    Check(GetU32(dib, 8) == 2, "DIBV5 rows are bottom-up (positive height)");
    // the first stored row is the image's bottom row: white first, in B, G, R, A
    Check(dib[124] == 255 && dib[127] == 255, "DIBV5 stores the bottom row first");

    RgbaImage back;
    Check(DecodeDib(dib.data(), dib.size(), back), "DIBV5 decodes");
    Check(back.width == 3 && back.height == 2 && back.pixels == src.pixels, "DIBV5 round-trips exactly, alpha included");
}

void TestDib24() {
    const RgbaImage src = Sample();
    const std::vector<uint8_t> dib = EncodeDib24(src);
    Check(dib.size() == 40 + 12 * 2, "24-bit DIB rows are padded to four bytes (9 -> 12)");
    RgbaImage back;
    Check(DecodeDib(dib.data(), dib.size(), back), "24-bit DIB decodes");
    Check(back.width == 3 && back.height == 2, "24-bit DIB keeps its size");
    const uint8_t* red = Pixel(back, 0, 0);
    Check(red[0] == 255 && red[1] == 0 && red[2] == 0 && red[3] == 255, "opaque red stays red");
    const uint8_t* green = Pixel(back, 1, 0);
    Check(Near(green[0], 127, 1) && green[1] == 255 && Near(green[2], 127, 1) && green[3] == 255,
          "half-transparent green is flattened onto white");
    const uint8_t* blue = Pixel(back, 2, 0);
    Check(blue[0] == 255 && blue[1] == 255 && blue[2] == 255 && blue[3] == 255,
          "an invisible pixel becomes white, not black");
}

void TestPngRoundTrip() {
    const RgbaImage src = Sample();
    std::vector<uint8_t> png;
    if (!EncodePng(src, png)) {
        std::cout << "cairo has no PNG support here; PNG checks skipped\n";
        return;
    }
    Check(IsPng(png.data(), png.size()), "EncodePng writes a PNG");
    Check(PngStreamLength(png.data(), png.size()) == png.size(), "a whole PNG measures to its own length");

    std::vector<uint8_t> padded = png;
    padded.resize(png.size() + 13, 0xCD);   // what GlobalSize can add
    Check(PngStreamLength(padded.data(), padded.size()) == png.size(), "padding after IEND is not part of the PNG");
    Check(PngStreamLength(png.data(), png.size() - 1) == 0, "a PNG cut short is not complete");

    RgbaImage back;
    Check(DecodePng(png.data(), png.size(), back), "the PNG decodes");
    Check(back.width == 3 && back.height == 2, "the PNG keeps its size");
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            const uint8_t* a = Pixel(src, x, y);
            const uint8_t* b = Pixel(back, x, y);
            Check(a[3] == b[3], "alpha survives the PNG at " + std::to_string(x) + "," + std::to_string(y));
            if (a[3] == 0) continue;   // an invisible pixel has no colour to keep
            const int tolerance = a[3] == 255 ? 0 : 4;   // cairo premultiplies on the way
            Check(Near(a[0], b[0], tolerance) && Near(a[1], b[1], tolerance) && Near(a[2], b[2], tolerance),
                  "colour survives the PNG at " + std::to_string(x) + "," + std::to_string(y));
        }
    }

    // An opaque picture is written as an RGB PNG (colour type 2).
    RgbaImage opaque;
    opaque.width = 2;
    opaque.height = 1;
    opaque.pixels = {10, 20, 30, 255, 40, 50, 60, 255};
    std::vector<uint8_t> rgb;
    Check(EncodePng(opaque, rgb) && rgb.size() > 25 && rgb[25] == 2, "an opaque picture is an RGB PNG");
    RgbaImage rgbBack;
    Check(DecodePng(rgb.data(), rgb.size(), rgbBack) && rgbBack.pixels == opaque.pixels, "an opaque PNG round-trips exactly");

    // A DIB as the clipboard hands it over, to the PNG the framework wants.
    std::vector<uint8_t> fromDib;
    Check(DibToPng(EncodeDibV5(opaque).data(), EncodeDibV5(opaque).size(), fromDib), "DibToPng converts");
    RgbaImage dibBack;
    Check(DecodePng(fromDib.data(), fromDib.size(), dibBack) && dibBack.pixels == opaque.pixels,
          "DibToPng keeps every pixel");
}

void TestPalette8TopDown() {
    // 2 x 2, 8-bit, three palette entries, rows top-down.
    std::vector<uint8_t> dib = InfoHeader(2, -2, 8, 0, 3);
    const uint8_t palette[] = {0, 0, 255, 0,   0, 255, 0, 0,   255, 0, 0, 0};   // B, G, R, 0: red, green, blue
    dib.insert(dib.end(), palette, palette + sizeof(palette));
    const uint8_t rows[] = {0, 1, 0, 0,   2, 0, 0, 0};   // two pixels a row, padded to four bytes
    dib.insert(dib.end(), rows, rows + sizeof(rows));
    RgbaImage img;
    Check(DecodeDib(dib.data(), dib.size(), img), "8-bit palette DIB decodes");
    Check(img.width == 2 && img.height == 2, "8-bit palette DIB size");
    const uint8_t* topLeft = Pixel(img, 0, 0);
    const uint8_t* topRight = Pixel(img, 1, 0);
    const uint8_t* bottomLeft = Pixel(img, 0, 1);
    Check(topLeft[0] == 255 && topLeft[1] == 0 && topLeft[3] == 255, "top-down: the first row is the top (red)");
    Check(topRight[1] == 255 && topRight[0] == 0, "palette index 1 is green");
    Check(bottomLeft[2] == 255 && bottomLeft[0] == 0, "palette index 2 is blue");
}

void TestPalette1BottomUp() {
    // 9 x 1, 1-bit: the ninth pixel is in the second byte.
    std::vector<uint8_t> dib = InfoHeader(9, 1, 1, 0);
    const uint8_t palette[] = {0, 0, 0, 0,   255, 255, 255, 0};
    dib.insert(dib.end(), palette, palette + sizeof(palette));
    const uint8_t row[] = {0x81, 0x80, 0, 0};   // 1 0 0 0 0 0 0 1 | 1
    dib.insert(dib.end(), row, row + sizeof(row));
    RgbaImage img;
    Check(DecodeDib(dib.data(), dib.size(), img), "1-bit DIB decodes");
    Check(Pixel(img, 0, 0)[0] == 255 && Pixel(img, 1, 0)[0] == 0 && Pixel(img, 7, 0)[0] == 255 && Pixel(img, 8, 0)[0] == 255,
          "1-bit pixels are read most significant bit first");
}

void TestBitfields565() {
    // 2 x 1, 16-bit 5-6-5 with the masks after a plain info header.
    std::vector<uint8_t> dib = InfoHeader(2, 1, 16, 3);
    std::vector<uint8_t> masks(12, 0);
    PutU32(masks, 0, 0xF800);
    PutU32(masks, 4, 0x07E0);
    PutU32(masks, 8, 0x001F);
    dib.insert(dib.end(), masks.begin(), masks.end());
    std::vector<uint8_t> row(4, 0);
    PutU16(row, 0, 0xF800);   // red
    PutU16(row, 2, 0x07E0);   // green
    dib.insert(dib.end(), row.begin(), row.end());
    RgbaImage img;
    Check(DecodeDib(dib.data(), dib.size(), img), "16-bit 5-6-5 DIB decodes");
    Check(Pixel(img, 0, 0)[0] == 255 && Pixel(img, 0, 0)[1] == 0 && Pixel(img, 1, 0)[1] == 255 && Pixel(img, 1, 0)[0] == 0,
          "5-6-5 channels scale to full intensity");
    Check(Pixel(img, 0, 0)[3] == 255, "a 16-bit pixel is opaque");
}

void TestBgrx32Opaque() {
    // 32-bit BI_RGB from a plain header: the fourth byte is not alpha.
    std::vector<uint8_t> dib = InfoHeader(1, 1, 32, 0);
    const uint8_t pixel[] = {30, 20, 10, 0};
    dib.insert(dib.end(), pixel, pixel + sizeof(pixel));
    RgbaImage img;
    Check(DecodeDib(dib.data(), dib.size(), img), "32-bit BI_RGB DIB decodes");
    Check(img.pixels == std::vector<uint8_t>({10, 20, 30, 255}), "32-bit BI_RGB is BGRX and opaque");

    // A V5 header that names an alpha mask but leaves every alpha byte zero.
    RgbaImage blank;
    blank.width = 2;
    blank.height = 1;
    blank.pixels = {10, 20, 30, 0, 40, 50, 60, 0};
    std::vector<uint8_t> v5 = EncodeDibV5(blank);
    RgbaImage back;
    Check(DecodeDib(v5.data(), v5.size(), back), "zero-alpha DIBV5 decodes");
    Check(back.pixels == std::vector<uint8_t>({10, 20, 30, 255, 40, 50, 60, 255}),
          "a DIB with zero alpha everywhere reads as opaque");

    // A V5 header with an alpha mask over BI_RGB pixels - the CF_DIBV5
    // Windows synthesises from another program's 32-bit CF_DIB - whose fourth
    // bytes are leftovers: padding, not transparency.
    RgbaImage junk;
    junk.width = 2;
    junk.height = 1;
    junk.pixels = {10, 20, 30, 7, 40, 50, 60, 200};
    std::vector<uint8_t> synthesised = EncodeDibV5(junk);
    PutU32(synthesised, 16, 0);   // BI_RGB
    RgbaImage padded;
    Check(DecodeDib(synthesised.data(), synthesised.size(), padded), "BI_RGB DIBV5 decodes");
    Check(padded.pixels == std::vector<uint8_t>({10, 20, 30, 255, 40, 50, 60, 255}),
          "a BI_RGB pixel's fourth byte is padding, not alpha");
}

void TestCoreHeader() {
    // BITMAPCOREHEADER: 12 bytes, 3-byte palette entries, 24-bit here.
    std::vector<uint8_t> dib(12, 0);
    PutU32(dib, 0, 12);
    PutU16(dib, 4, 1);
    PutU16(dib, 6, 1);
    PutU16(dib, 8, 1);
    PutU16(dib, 10, 24);
    const uint8_t pixel[] = {3, 2, 1, 0};
    dib.insert(dib.end(), pixel, pixel + sizeof(pixel));
    RgbaImage img;
    Check(DecodeDib(dib.data(), dib.size(), img) && img.pixels == std::vector<uint8_t>({1, 2, 3, 255}),
          "a core-header DIB decodes");
}

void TestRefusals() {
    RgbaImage img;
    const std::vector<uint8_t> good = EncodeDib24(Sample());
    Check(!DecodeDib(good.data(), good.size() - 1, img), "a DIB missing its last byte is refused");
    Check(!DecodeDib(good.data(), 20, img), "a cut-off header is refused");
    Check(!DecodeDib(nullptr, 0, img), "no data is refused");

    std::vector<uint8_t> huge = InfoHeader(1000000, 1000000, 24, 0);
    huge.resize(huge.size() + 64, 0);
    Check(!DecodeDib(huge.data(), huge.size(), img), "an absurd size is refused before allocating");

    std::vector<uint8_t> rle = InfoHeader(1, 1, 8, 1);
    rle.resize(rle.size() + 1024 + 4, 0);
    Check(!DecodeDib(rle.data(), rle.size(), img), "an RLE DIB is refused");

    std::vector<uint8_t> palette = InfoHeader(1, 1, 8, 0, 100000);
    palette.resize(palette.size() + 64, 0);
    Check(!DecodeDib(palette.data(), palette.size(), img), "a palette larger than 256 entries is refused");
}

void TestBmpFile() {
    const std::vector<uint8_t> dib = EncodeDib24(Sample());
    std::vector<uint8_t> bmp(14, 0);
    bmp[0] = 'B';
    bmp[1] = 'M';
    PutU32(bmp, 2, static_cast<uint32_t>(14 + dib.size()));
    PutU32(bmp, 10, 14 + 40);
    bmp.insert(bmp.end(), dib.begin(), dib.end());
    Check(DibOfBmpFile(bmp.data(), bmp.size()) == dib, "a .bmp file's DIB is the bytes after its file header");
    Check(DibOfBmpFile(dib.data(), dib.size()).empty(), "a bare DIB is not a .bmp file");
}

} // namespace

int main() {
    TestDibV5RoundTrip();
    TestDib24();
    TestPngRoundTrip();
    TestPalette8TopDown();
    TestPalette1BottomUp();
    TestBitfields565();
    TestBgrx32Opaque();
    TestCoreHeader();
    TestRefusals();
    TestBmpFile();
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ClipboardDibTest: all checks passed\n";
    return 0;
}
