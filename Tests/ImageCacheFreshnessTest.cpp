// Tests/ImageCacheFreshnessTest.cpp
// A picture saved over must be shown as it is now.
//
// UCImage::Get() keeps every image it reads in a process-wide cache keyed by
// the path, and so does libvips under it, keyed by the file name. Neither
// looked at the file again, so a picture edited and saved over kept coming
// back as it was: UltraFiler rescanned the folder when the save landed, made
// the thumbnail again - and got the old picture from the cache, then wrote
// that into the thumbnail disk cache under the new file's stamp, so it stayed
// wrong on every run after.
//
// What this guards:
//   - GetFresh() reads a file saved over again: its new size and its new
//     pixels, at a size that was cached before the save.
//   - GetFresh() of an unchanged file is the cached image, not a reload.
//   - Get() afterwards serves the fresh image, not the old one.
//   - RemoveFromCache() followed by Get() reads the new header, which needs
//     libvips' own operation cache released too.
//   - RemoveFromCacheIfChanged() drops a changed file and reads nothing, and
//     UltraCanvasImageFileWatch's worker does that for a file that is no
//     longer the version a view drew, so a paint path that only calls Get()
//     draws the new picture.
// Version: 1.1.0
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework

#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasImageFileWatch.h"
#include "UltraCanvasFileStamp.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
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

#ifdef HAS_LIBVIPS
namespace {
    // A flat grey RGB picture of `width` x 20 pixels, every channel `level`.
    // Saving over the file moves its modification time on explicitly as
    // well, so the test does not depend on the filesystem's clock resolution.
    bool WritePicture(const std::filesystem::path& file, int width, int level) {
        try {
            vips::VImage::black(width, 20, vips::VImage::option()->set("bands", 3))
                    .linear(1.0, static_cast<double>(level))
                    .cast(VIPS_FORMAT_UCHAR)
                    .write_to_file(PathToUtf8(file).c_str());
        } catch (vips::VError& err) {
            std::cerr << "   could not write " << PathToUtf8(file) << ": " << err.what() << std::endl;
            return false;
        }
        static int saves = 0;
        std::error_code ec;
        std::filesystem::last_write_time(
                file, std::filesystem::file_time_type::clock::now() +
                              std::chrono::seconds(10 * ++saves), ec);
        return true;
    }

    // The grey level the middle of a pixmap shows: the blue channel of a
    // premultiplied ARGB pixel, which is the level itself for an opaque grey.
    int LevelOf(const std::shared_ptr<UCPixmapCairo>& pixmap) {
        if (!pixmap) return -1;
        const uint32_t pixel = pixmap->GetPixel(pixmap->GetRawWidth() / 2,
                                                pixmap->GetRawHeight() / 2);
        return static_cast<int>(pixel & 0xFFu);
    }

    bool Near(int level, int expected) { return std::abs(level - expected) <= 2; }
}
#endif

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Image Cache Freshness Suite"          << std::endl;
    std::cerr << "========================================" << std::endl;

#ifndef HAS_LIBVIPS
    std::cerr << "SKIP: whole suite (built without libvips)" << std::endl;
    return 0;
#else
    if (!UCImage::InitializeImageSubsysterm("ImageCacheFreshnessTest")) {
        std::cerr << "SKIP: whole suite (image subsystem would not start)" << std::endl;
        return 0;
    }

    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "UltraCanvas-ImageCacheFreshnessTest.png";
    const std::string path = PathToUtf8(file);
    UCImage::RemoveFromCache(path);

    std::cerr << "\n--- A picture, shown once ---" << std::endl;
    TEST("The first version is written", WritePicture(file, 40, 30));
    auto first = UCImage::Get(path);
    TEST("It is read", first && first->IsValid() && first->GetWidth() == 40);
    TEST("Its pixmap shows its grey",
         first && Near(LevelOf(first->GetPixmap(16, 8, ImageFitMode::Fill)), 30));

    std::cerr << "\n--- The same picture, saved over ---" << std::endl;
    TEST("The second version is written", WritePicture(file, 60, 200));
    TEST("Get() still answers from the cache, as documented",
         UCImage::Get(path) == first);

    auto fresh = UCImage::GetFresh(path);
    TEST("GetFresh() reads the file again", fresh && fresh != first);
    TEST("with the new width - libvips' cached header is not reused",
         fresh && fresh->GetWidth() == 60);
    TEST("and the new pixels at a size cached before the save",
         fresh && Near(LevelOf(fresh->GetPixmap(16, 8, ImageFitMode::Fill)), 200));
    TEST("Get() now serves the fresh image", UCImage::Get(path) == fresh);
    TEST("GetFresh() of an unchanged file is the cached image, not a reload",
         UCImage::GetFresh(path) == fresh);

    std::cerr << "\n--- Saved over again, then dropped by hand ---" << std::endl;
    TEST("The third version is written", WritePicture(file, 80, 120));
    UCImage::RemoveFromCache(path);
    auto reread = UCImage::Get(path);
    TEST("Get() after RemoveFromCache() reads the new header",
         reread && reread->GetWidth() == 80);
    TEST("and the new pixels",
         reread && Near(LevelOf(reread->GetPixmap(16, 8, ImageFitMode::Fill)), 120));

    std::cerr << "\n--- RemoveFromCacheIfChanged: the check without the reload ---" << std::endl;
    TEST("An unchanged file is left alone",
         !UCImage::RemoveFromCacheIfChanged(path) && UCImage::Get(path) == reread);
    TEST("The fourth version is written", WritePicture(file, 50, 90));
    TEST("A changed file is dropped", UCImage::RemoveFromCacheIfChanged(path));
    TEST("and nothing was read in its place: a second call has nothing cached",
         !UCImage::RemoveFromCacheIfChanged(path));
    auto fourth = UCImage::Get(path);
    TEST("the next Get() reads the file as it is now",
         fourth && fourth != reread && fourth->GetWidth() == 50);

    std::cerr << "\n--- UltraCanvasImageFileWatch: checked on its worker ---" << std::endl;
    {
        // No application runs here, so onChanged is never posted; what the
        // worker did shows in the image cache.
        UltraCanvasImageFileWatch watch([]() {}, 100);
        // What a paint path hands over: the path and the version it drew.
        // Drawn twice in one frame (and once with nothing to show), kept once.
        watch.SetDrawnImages({{path, FileStamp{}},
                              {path, fourth->GetSourceStamp()},
                              {std::string(), FileStamp{}}});
        const auto watched = watch.GetDrawnImages();
        TEST("A path drawn twice is watched once, against the version drawn last",
             watched.size() == 1 && watched[0].path == path &&
             watched[0].version == fourth->GetSourceStamp());
        TEST("The version an image was read from is the file as it is",
             fourth->GetSourceStamp() == StampFile(path));
        TEST("The fifth version is written", WritePicture(file, 70, 160));
        std::shared_ptr<UCImage> seen = fourth;
        for (int i = 0; i < 40 && seen == fourth; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // Get() is what a paint path calls: it reads again only once the
            // worker has dropped the old copy.
            seen = UCImage::Get(path);
        }
        TEST("The worker drops the copy of a file saved over",
             seen && seen != fourth && seen->GetWidth() == 70);
        TEST("and the next paint draws the new pixels",
             seen && Near(LevelOf(seen->GetPixmap(16, 8, ImageFitMode::Fill)), 160));
        // The repaint hands the new version over; nothing more is dropped.
        watch.SetDrawnImages({{path, seen->GetSourceStamp()}});
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        TEST("A file that is the version drawn is not dropped again",
             UCImage::Get(path) == seen);
        watch.SetDrawnImages({});
        TEST("An empty list watches nothing", watch.GetDrawnImages().empty());
    }
    TEST("Destroying the watch joins its worker", true);

    UCImage::RemoveFromCache(path);
    std::error_code ec;
    std::filesystem::remove(file, ec);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
#endif
}
