// Tests/ImageCursorCacheTest.cpp
// Unit tests for UCImageCursorCache, the per-scaling store of cursors drawn
// from a picture (Windows and Linux backends). Header-only: draws fake handles
// through a callable, so it needs no display and no framework link.
// Version: 1.0.0
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework

#include "UltraCanvasImageCursorCache.h"

#include <cstdio>
#include <string>
#include <vector>

using UltraCanvas::UCImageCursorCache;
using UltraCanvas::UCMouseCursor;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    ++checks; \
    auto va = (a); auto vb = (b); \
    if (!(va == vb)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
    } \
} while (0)

// A fake platform: a handle is a number, every draw is logged, a file named
// "missing" cannot be drawn.
struct Draw {
    std::string file;
    int hotspotX;
    int hotspotY;
    float scale;
};

struct FakePlatform {
    std::vector<Draw> draws;
    std::vector<int> released;
    int nextHandle = 1;

    UCImageCursorCache<int> MakeCache() {
        return UCImageCursorCache<int>(
                [this](const std::string& file, int hx, int hy, float scale) {
                    draws.push_back({file, hx, hy, scale});
                    return file == "missing" ? 0 : nextHandle++;
                },
                [this](int handle) { released.push_back(handle); });
    }
};

static void TestOnePerScaling() {
    FakePlatform platform;
    auto cache = platform.MakeCache();

    CHECK(!cache.HasSource(UCMouseCursor::ContextMenu));
    CHECK_EQ(cache.Get(UCMouseCursor::ContextMenu, 1.0f), 0);   // nothing registered

    CHECK(cache.SetSource(UCMouseCursor::ContextMenu, "menu.svg", 0, 0, 1.0f));
    CHECK(cache.HasSource(UCMouseCursor::ContextMenu));
    CHECK_EQ(platform.draws.size(), 1u);
    const int at100 = cache.Get(UCMouseCursor::ContextMenu, 1.0f);
    CHECK(at100 != 0);
    CHECK_EQ(platform.draws.size(), 1u);                          // kept, not redrawn

    // The window moved to a 150 % screen: drawn again, at that size.
    const int at150 = cache.Get(UCMouseCursor::ContextMenu, 1.5f);
    CHECK(at150 != 0);
    CHECK(at150 != at100);
    CHECK_EQ(platform.draws.size(), 2u);
    CHECK(platform.draws.back().scale > 1.49f && platform.draws.back().scale < 1.51f);

    // ...and back: the 100 % one is still there, nothing is redrawn.
    CHECK_EQ(cache.Get(UCMouseCursor::ContextMenu, 1.0f), at100);
    CHECK_EQ(cache.Get(UCMouseCursor::ContextMenu, 1.5f), at150);
    // A scale that differs only by float noise is the same scaling.
    CHECK_EQ(cache.Get(UCMouseCursor::ContextMenu, 1.5000001f), at150);
    CHECK_EQ(platform.draws.size(), 2u);
    CHECK(platform.released.empty());
}

static void TestHotspotAndFile() {
    FakePlatform platform;
    auto cache = platform.MakeCache();

    CHECK(cache.SetSource(UCMouseCursor::Custom1, "picker.png", 1, 22, 2.0f));
    CHECK_EQ(platform.draws.size(), 1u);
    CHECK_EQ(platform.draws[0].file, std::string("picker.png"));
    CHECK_EQ(platform.draws[0].hotspotX, 1);
    CHECK_EQ(platform.draws[0].hotspotY, 22);

    // The same file again keeps what was drawn.
    const int first = cache.Get(UCMouseCursor::Custom1, 2.0f);
    CHECK(cache.SetSource(UCMouseCursor::Custom1, "picker.png", 1, 22, 2.0f));
    CHECK_EQ(platform.draws.size(), 1u);
    CHECK_EQ(cache.Get(UCMouseCursor::Custom1, 2.0f), first);

    // Another file replaces it: the old cursor is released.
    CHECK(cache.SetSource(UCMouseCursor::Custom1, "other.png", 0, 0, 2.0f));
    CHECK_EQ(platform.released.size(), 1u);
    CHECK_EQ(platform.released[0], first);
    CHECK(cache.Get(UCMouseCursor::Custom1, 2.0f) != first);

    // Other cursors are untouched by it.
    CHECK(cache.SetSource(UCMouseCursor::LookingGlass, "glass.png", 0, 0, 1.0f));
    CHECK(cache.SetSource(UCMouseCursor::Custom1, "third.png", 0, 0, 1.0f));
    CHECK(cache.Get(UCMouseCursor::LookingGlass, 1.0f) != 0);
}

static void TestMissingPicture() {
    FakePlatform platform;
    auto cache = platform.MakeCache();

    CHECK(!cache.SetSource(UCMouseCursor::LookingGlass, "missing", 0, 0, 1.0f));
    CHECK(!cache.HasSource(UCMouseCursor::LookingGlass));          // nothing registered
    CHECK_EQ(cache.Get(UCMouseCursor::LookingGlass, 1.0f), 0);
    CHECK(platform.released.empty());                              // nothing to release
}

static void TestClearReleasesEverything() {
    FakePlatform platform;
    auto cache = platform.MakeCache();

    cache.SetSource(UCMouseCursor::ContextMenu, "menu.svg", 0, 0, 1.0f);
    cache.Get(UCMouseCursor::ContextMenu, 2.0f);
    cache.SetSource(UCMouseCursor::LookingGlass, "glass.png", 0, 0, 1.0f);
    CHECK_EQ(platform.draws.size(), 3u);

    cache.Clear();
    CHECK_EQ(platform.released.size(), 3u);
    CHECK(!cache.HasSource(UCMouseCursor::ContextMenu));
    CHECK(!cache.HasSource(UCMouseCursor::LookingGlass));
}

int main() {
    TestOnePerScaling();
    TestHotspotAndFile();
    TestMissingPicture();
    TestClearReleasesEverything();

    std::printf("ImageCursorCacheTest: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
