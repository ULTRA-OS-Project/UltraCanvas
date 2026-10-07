// Tests/ImageSaveStagedTest.cpp
// A failed image save leaves nothing behind and keeps the file it was saving
// over.
//
// A libvips writer opens - and truncates - its file before it has encoded a
// byte, so a save that failed left an empty file, and the same failure saving
// over an image destroyed it (AVIF on a libheif without an AV1 encoder did
// exactly that). PixelFX::FileIO's savers now write through
// WriteFileAtomically; a source that fails to decode halfway through the save
// stands in for the encoder, because it fails the same way on every machine.
// The QR code's own image and SVG exports, which write without PixelFX, are
// staged the same way, and its AVIF export no longer crashes libvips.
// (Which extension a save dialog gives the name is FileDialogTest's.)
// No display needed.
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
// After the header above, which pulls in the X11 chain: this one #undefs
// X11's None for its own enumerators.
#include "Plugins/QRCode/UltraCanvasQRCode.h"
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace UltraCanvas;
namespace fs = std::filesystem;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

namespace {

#ifdef HAS_LIBVIPS
std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Anything WriteFileAtomically staged and did not clean up.
bool HasStagedLeftovers(const fs::path& dir) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (PathToUtf8(entry.path().filename()).rfind(".ucsave", 0) == 0) return true;
    }
    return false;
}

template <typename Save>
bool Throws(Save save) {
    try {
        save();
    } catch (const PixelFX::PixelFXException&) {
        return true;
    }
    return false;
}

// The QR code writes its exports itself rather than through PixelFX.
void TestQRCodeExport(const fs::path& dir) {
    UltraCanvasQRCode qr("qr");
    qr.SetContent("https://example.com/ultracanvas");
    std::string error;
    const fs::path png = dir / "qr.png";
    TEST("a QR code exports as PNG",
         qr.ExportToImage(PathToUtf8(png), QRImageFormat::PNG, 4, &error) &&
         fs::file_size(png) > 0);
    const fs::path svg = dir / "qr.svg";
    TEST("and as SVG", qr.ExportToSVG(PathToUtf8(svg), 4) && fs::file_size(svg) > 0);

    // Over an existing file, as AVIF: where the encoder is missing the export
    // fails and the file must be as it was; where it is there, replaced whole.
    const fs::path avif = dir / "qr.avif";
    { std::ofstream(avif, std::ios::binary) << "ORIGINAL"; }
    const bool exported = qr.ExportToImage(PathToUtf8(avif), QRImageFormat::AVIF, 4, &error);
    TEST("an AVIF QR export replaces the file whole or keeps it",
         exported ? fs::file_size(avif) > 0 && ReadAll(avif) != "ORIGINAL"
                  : ReadAll(avif) == "ORIGINAL");
    TEST("no QR export left anything staged", !HasStagedLeftovers(dir));
}

void TestStagedImageSave(const char* programName) {
    if (VIPS_INIT(programName) != 0) {
        TEST("libvips starts", false);
        return;
    }
    // A Thai folder name, so the staged write is exercised on a name outside
    // ASCII as well.
    const fs::path dir = fs::temp_directory_path() /
                         PathFromUtf8("ImageSaveStagedTest-\xE0\xB8\x97\xE0\xB8\x94\xE0\xB8\xAA\xE0\xB8\xAD\xE0\xB8\x9A");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const PixelFX::PFXImage small(vips::VImage::black(8, 8, vips::VImage::option()->set("bands", 3)));
    const fs::path good = dir / "good.png";
    TEST("a save that works writes the file",
         PixelFX::FileIO::Save(small, PathToUtf8(good)) && fs::file_size(good) > 0);
    TEST("and leaves nothing staged behind", !HasStagedLeftovers(dir));

    // A source that fails to decode halfway through the save: the PNG is cut
    // short and read strictly, so the writer has opened its file by the time
    // the error comes.
    const fs::path whole = dir / "whole.png";
    vips::VImage noise = vips::VImage::gaussnoise(1200, 1200).cast(VIPS_FORMAT_UCHAR);
    noise.write_to_file(PathToUtf8(whole).c_str());
    const fs::path cut = dir / "cut.png";
    fs::copy_file(whole, cut, fs::copy_options::overwrite_existing);
    fs::resize_file(cut, fs::file_size(whole) / 2);
#if VIPS_MAJOR_VERSION > 8 || (VIPS_MAJOR_VERSION == 8 && VIPS_MINOR_VERSION >= 12)
    vips::VOption* strict = vips::VImage::option()->set("fail_on", VIPS_FAIL_ON_TRUNCATED);
#else
    vips::VOption* strict = vips::VImage::option()->set("fail", true);
#endif
    const PixelFX::PFXImage broken(vips::VImage::new_from_file(PathToUtf8(cut).c_str(), strict));

    const fs::path existing = dir / "existing.png";
    { std::ofstream(existing, std::ios::binary) << "ORIGINAL"; }
    TEST("a save that fails throws",
         Throws([&] { PixelFX::FileIO::Save(broken, PathToUtf8(existing)); }));
    TEST("and keeps the file it was saving over",
         ReadAll(existing) == "ORIGINAL");

    const fs::path fresh = dir / "fresh.png";
    TEST("a failed save to a new name throws",
         Throws([&] { PixelFX::FileIO::SavePng(broken, PathToUtf8(fresh)); }));
    TEST("and leaves no file", !fs::exists(fresh));

    const fs::path drawing = dir / "drawing.svg";
    TEST("a format libvips cannot write (SVG) throws",
         Throws([&] { PixelFX::FileIO::Save(small, PathToUtf8(drawing)); }));
    TEST("and leaves no file", !fs::exists(drawing));

    // AVIF needs libheif with an AV1 encoder, which not every build has. Where
    // it is missing the save must fail cleanly; where it is there, it works.
    const fs::path avif = dir / "photo.avif";
    const bool avifFailed = Throws([&] { PixelFX::FileIO::SaveAvif(small, PathToUtf8(avif)); });
    TEST("an AVIF save leaves a whole file or none",
         avifFailed ? !fs::exists(avif) : fs::file_size(avif) > 0);
    TEST("no failed save left anything staged", !HasStagedLeftovers(dir));

    TestQRCodeExport(dir);
    fs::remove_all(dir, ec);
}
#endif

} // namespace

int main(int argc, char** argv) {
    (void)argc;
#ifdef HAS_LIBVIPS
    TestStagedImageSave(argv[0]);
#else
    (void)argv;
    std::cerr << "SKIP: staged image save (built without libvips)" << std::endl;
#endif
    std::cerr << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
