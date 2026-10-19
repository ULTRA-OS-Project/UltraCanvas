// Tests/ImageFileWatchDisplayTest.cpp
// An album and a slideshow showing a picture that is saved over repaint with
// the new picture by themselves.
//
// Both paint with UCImage::Get(), which never looks at the disk, so a photo
// edited while they showed it kept its old picture for the life of the widget.
// They now hand the paths they drew to an UltraCanvasImageFileWatch, whose
// worker drops a changed file from the image cache and asks the widget to
// repaint on the UI thread. This test reads the composited window back: the
// picture is red, the file is saved over green, and the test then only runs
// what the UI thread would - posted tasks, then a render of whatever is
// dirty. No element is marked dirty by the test, so green on screen means the
// widget asked for the repaint itself.
//
// Runs headless under Xvfb. Skips - rather than fails - when there is no
// display, so it stays usable on a bare CI machine.
// Version: 1.0.0
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework

#include "DisplayTestSupport.h"
#include "UltraCanvasAlbum.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSlideshow.h"
#include "UltraCanvasWindow.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

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

#define SKIP_ALL(reason)                                                      \
    do {                                                                      \
        std::cerr << "SKIP: whole suite (" << reason << ")" << std::endl;     \
        return 0;                                                             \
    } while (0)

namespace {

// The test runs the UI thread's posted tasks itself, without the rest of an
// event-loop iteration (no timers, no native events) - so nothing but the
// watch's own callback can make a widget repaint.
class TestApplication : public UltraCanvasApplication {
public:
    using UltraCanvasApplicationBase::ProcessPostedTasks;
};

const Color kRed(208, 16, 16);
const Color kGreen(16, 160, 16);

bool NearlyEqual(const Color& a, const Color& b, int tolerance = 24) {
    auto close = [tolerance](uint8_t l, uint8_t r) {
        return std::abs(static_cast<int>(l) - static_cast<int>(r)) <= tolerance;
    };
    return close(a.r, b.r) && close(a.g, b.g) && close(a.b, b.b);
}

#ifdef HAS_LIBVIPS
// A flat 400x300 picture of `colour`, its modification time moved on so the
// test does not depend on the filesystem's clock resolution.
bool WritePicture(const std::filesystem::path& file, const Color& colour) {
    try {
        std::vector<double> rgb = {static_cast<double>(colour.r),
                                   static_cast<double>(colour.g),
                                   static_cast<double>(colour.b)};
        vips::VImage::black(400, 300, vips::VImage::option()->set("bands", 3))
                .linear(std::vector<double>{1.0, 1.0, 1.0}, rgb)
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
#endif

// Does the region show `colour` (at the 5x5 grid of sample points)?
bool RegionShows(const std::shared_ptr<UltraCanvasWindow>& window,
                 const Rect2Di& region, const Color& colour) {
    int hits = 0;
    for (int iy = 1; iy <= 5; ++iy) {
        for (int ix = 1; ix <= 5; ++ix) {
            Color px;
            const int x = region.x + region.width * ix / 6;
            const int y = region.y + region.height * iy / 6;
            if (window->GetPixelColor(x, y, px) && NearlyEqual(px, colour)) ++hits;
        }
    }
    return hits >= 3;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   Image File Watch Display Suite"       << std::endl;
    std::cerr << "========================================" << std::endl;

#ifndef HAS_LIBVIPS
    SKIP_ALL("built without libvips");
#else
    if (!std::getenv("DISPLAY")) SKIP_ALL("no DISPLAY");

    TestApplication app;
    if (!app.Initialize("ImageFileWatchDisplayTest")) SKIP_ALL("application would not initialise");

    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "UltraCanvas-ImageFileWatchDisplayTest.png";
    const std::string path = PathToUtf8(file);
    UCImage::RemoveFromCache(path);
    if (!WritePicture(file, kRed)) SKIP_ALL("the test picture could not be written");

    WindowConfig cfg;
    cfg.title = "ImageFileWatchDisplayTest";
    cfg.width = 640;
    cfg.height = 320;
    auto window = CreateWindow(cfg);
    if (!window) SKIP_ALL("window could not be created");
    window->Show();

    // The album on the left: one photo, stretched over its tile so the tile is
    // the picture's colour edge to edge.
    auto album = CreateAlbum("Album", 0, 0, 300, 300);
    AlbumConfig albumConfig = album->GetConfig();
    albumConfig.imageDisplay = AlbumImageDisplay::Stretch;
    album->SetConfig(albumConfig);
    AlbumItem photo;
    photo.mediaPath = path;
    photo.mediaType = AlbumMediaType::Photo;
    album->AddItem(photo);
    window->AddChild(album);

    // The slideshow on the right: one slide, full bleed, no indicators, not
    // playing - nothing of its own repaints it.
    auto slideshow = CreateSlideshow("Slideshow", 320, 0, 300, 300);
    SlideshowConfig showConfig = slideshow->GetConfig();
    showConfig.autoPlay = false;
    showConfig.infoLayout = SlideshowInfoLayout::Hidden;
    showConfig.indicators.shape = SlideshowIndicatorShape::Hidden;
    showConfig.imageFit = ImageFitMode::Fill;
    slideshow->SetConfig(showConfig);
    slideshow->AddSlide(SlideshowSlide{path, "", ""});
    window->AddChild(slideshow);

    DisplayTest::Frame(window, {album, slideshow});
    const Rect2Di albumTile(20, 20, 120, 80);      // inside the first tile
    const Rect2Di slideArea(380, 60, 180, 180);
    std::cerr << "\n--- The picture as first drawn ---" << std::endl;
    TEST("The album draws it red", RegionShows(window, albumTile, kRed));
    TEST("The slideshow draws it red", RegionShows(window, slideArea, kRed));

    std::cerr << "\n--- Saved over green, nothing marked dirty by the test ---" << std::endl;
    TEST("The green version is written", WritePicture(file, kGreen));
    bool albumGreen = false, slideGreen = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < deadline && !(albumGreen && slideGreen)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        app.ProcessPostedTasks();     // where the watch's callback arrives
        window->UpdateAndRender();    // renders only what asked for it
        albumGreen = RegionShows(window, albumTile, kGreen);
        slideGreen = RegionShows(window, slideArea, kGreen);
    }
    TEST("The album repaints the picture green by itself", albumGreen);
    TEST("The slideshow repaints the picture green by itself", slideGreen);

    window->RemoveChild(album);
    window->RemoveChild(slideshow);
    album.reset();
    slideshow.reset();
    TEST("Both widgets and their watches go away cleanly", true);

    UCImage::RemoveFromCache(path);
    std::error_code ec;
    std::filesystem::remove(file, ec);

    std::cerr << "\n========================================" << std::endl;
    std::cerr << "   " << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    std::cerr << "========================================" << std::endl;
    return failCount == 0 ? 0 : 1;
#endif
}
