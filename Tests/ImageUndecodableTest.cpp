// Tests/ImageUndecodableTest.cpp
// An image whose header reads but whose pixels do not decode. libvips opens
// it lazily, so the size is known and the image counts as valid, and only
// making the pixmap finds the pixels missing: VImage::data() comes back null.
// CreatePixmapFromVImage copied from that null pointer - a crash in the
// demo's Media Viewer when browsing to a HEIC on a build without an HEVC
// decoder. A truncated PNG fails the same way on every build.
//
// The pixmap request must fail cleanly: no pixmap, an error message, and the
// next request answered from that error without decoding again. An older
// libvips that recovers the partial picture instead only has to not crash.
// Version: 1.0.0
// Last Modified: 2026-10-03
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Undecodable Image Suite"              << std::endl;
    std::cerr << "========================================" << std::endl;

#ifndef HAS_LIBVIPS
    std::cerr << "SKIP: whole suite (built without libvips)" << std::endl;
    return 0;
#else
    if (!UCImage::InitializeImageSubsysterm("ImageUndecodableTest")) {
        std::cerr << "SKIP: whole suite (image subsystem would not start)" << std::endl;
        return 0;
    }

    // The first 2000 bytes of a 720x460 PNG: the header is whole, the pixel
    // data stops short.
    const std::filesystem::path source =
        std::filesystem::path(UC_MEDIA_DIR) / "images" / "OCR-demo.png";
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        std::cerr << "SKIP: whole suite (" << source.string() << " not found)" << std::endl;
        return 0;
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bytes.resize(std::min<size_t>(bytes.size(), 2000));

    const std::filesystem::path truncated =
        std::filesystem::temp_directory_path() / "UltraCanvas-ImageUndecodableTest.png";
    {
        std::ofstream out(truncated, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    std::cerr << "\n--- A PNG cut short after its header ---" << std::endl;
    auto image = UCImage::Get(truncated.string());
    TEST("The header is read", image && image->GetWidth() == 720 && image->GetHeight() == 460);

    if (image) {
        auto pixmap = image->GetPixmap(360, 230, ImageFitMode::Fill);   // crashed here
        TEST("Asking for the pixmap returns", true);
        if (pixmap) {
            // Older libvips (8.12, Ubuntu 22.04) decodes a truncated PNG
            // leniently: it warns and hands back the rows it got. Nothing
            // is undecodable there, so the failure path is not reached.
            std::cerr << "SKIP: this libvips recovers the partial picture "
                         "(no decode failure to handle)" << std::endl;
        } else {
            TEST("The failure is reported", !image->errorMessage.empty());
            std::cerr << "   error: " << image->errorMessage << std::endl;
            TEST("A second request fails the same way",
                 image->GetPixmap(180, 115, ImageFitMode::Contain) == nullptr);
        }
    }

    UCImage::RemoveFromCache(truncated.string());
    std::error_code ec;
    std::filesystem::remove(truncated, ec);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
#endif
}
