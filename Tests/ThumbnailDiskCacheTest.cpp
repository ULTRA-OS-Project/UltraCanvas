// Tests/ThumbnailDiskCacheTest.cpp
// The thumbnail cache that survives the process that made it
// (UltraCanvasThumbnailDiskCache) and the retention policy underneath it
// (UltraCanvasDiskCache).
//
// The rules this guards, in the order they matter:
//
//   - A stored thumbnail comes back. That is the whole feature: the second
//     launch draws the folder instead of decoding it again.
//   - It comes back only while it is still TRUE. The entry records the size
//     and modification time of the file it was made from, so editing the
//     picture is a miss, not yesterday's picture — and the dead entry is
//     removed rather than left to be asked again.
//   - A file that is used is touched, so it survives; one that is not used
//     is deleted after two weeks. A cache with no expiry is a directory that
//     grows for the life of the account, because its keys name files the
//     user is free to move, rename or delete without telling anyone.
//   - The touch is throttled. A folder of a thousand thumbnails redrawn all
//     afternoon must not be a thousand disk writes per repaint.
//   - Nothing outside the cache's own extensions is ever deleted. The sweep
//     walks a directory it does not own exclusively.
//
// The test never touches the user's real cache: it points the cache at a
// temporary directory of its own and removes it at the end.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasDiskCache.h"
#include "UltraCanvasThumbnailDiskCache.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

fs::path g_root;

void WriteFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

// Move a file's stamp back, which is how the sweep is told a file has not
// been served lately — the stamp IS the modification time.
void Backdate(const fs::path& path, std::chrono::hours ago) {
    std::error_code ec;
    fs::last_write_time(path, fs::file_time_type::clock::now() - ago, ec);
}

std::chrono::hours HoursAgo(const fs::path& path) {
    std::error_code ec;
    const auto stamp = fs::last_write_time(path, ec);
    if (ec) return std::chrono::hours(-1);
    return std::chrono::duration_cast<std::chrono::hours>(
            fs::file_time_type::clock::now() - stamp);
}

// Every entry file the cache currently holds.
std::vector<fs::path> EntryFiles() {
    std::vector<fs::path> files;
    std::error_code ec;
    fs::directory_iterator it(g_root / "cache", ec);
    if (ec) return files;
    for (const fs::directory_entry& item : it) {
        if (item.path().extension() == ".ucth") files.push_back(item.path());
    }
    return files;
}

std::vector<uint8_t> Blob(size_t size, uint8_t fill) {
    return std::vector<uint8_t>(size, fill);
}

ThumbnailDiskCache::Request RequestFor(const fs::path& source,
                                       int w = 128, int h = 128) {
    ThumbnailDiskCache::Request request;
    request.sourcePath = source.string();
    request.width = w;
    request.height = h;
    request.fit = 1;
    request.scale = 1.0f;
    return request;
}

// ===== STORING AND SERVING =====
void TestStoredThumbnailComesBack() {
    std::cout << "\nA thumbnail stored and asked for again:\n";

    const fs::path picture = g_root / "holiday.jpg";
    WriteFile(picture, "pretend this is a photograph");

    const auto request = RequestFor(picture);
    Check(ThumbnailDiskCache::Load(request).empty(),
          "nothing is served before anything is stored");

    const std::vector<uint8_t> blob = Blob(4096, 0xA5);
    Check(ThumbnailDiskCache::Store(request, blob), "it stores");

    const std::vector<uint8_t> served = ThumbnailDiskCache::Load(request);
    Check(served == blob, "and comes back byte for byte");
}

void TestGeometryIsPartOfTheIdentity() {
    std::cout << "\nThe same picture at two tile sizes:\n";

    const fs::path picture = g_root / "sizes.png";
    WriteFile(picture, "one picture");

    const std::vector<uint8_t> small = Blob(64, 0x11);
    const std::vector<uint8_t> large = Blob(256, 0x22);
    ThumbnailDiskCache::Store(RequestFor(picture, 64, 64), small);
    ThumbnailDiskCache::Store(RequestFor(picture, 256, 256), large);

    Check(ThumbnailDiskCache::Load(RequestFor(picture, 64, 64)) == small,
          "the small tile serves the small thumbnail");
    Check(ThumbnailDiskCache::Load(RequestFor(picture, 256, 256)) == large,
          "the big tile serves the big one");
    Check(ThumbnailDiskCache::Load(RequestFor(picture, 512, 512)).empty(),
          "a size that was never stored is a miss, not the nearest one");
}

void TestUnknownSourceIsAMiss() {
    std::cout << "\nA picture that is not there:\n";

    const fs::path missing = g_root / "deleted.jpg";
    WriteFile(missing, "here for now");
    const auto request = RequestFor(missing);
    ThumbnailDiskCache::Store(request, Blob(128, 0x33));
    Check(!ThumbnailDiskCache::Load(request).empty(), "stored while it existed");

    fs::remove(missing);
    Check(ThumbnailDiskCache::Load(request).empty(),
          "a source that no longer exists is a miss, never a stale hit");
}

// ===== STALENESS IS THE SOURCE FILE'S TO DECIDE =====
void TestEditedSourceInvalidates() {
    std::cout << "\nA picture edited after it was thumbnailed:\n";

    const fs::path picture = g_root / "edited.png";
    WriteFile(picture, "the first version");
    const auto request = RequestFor(picture);
    ThumbnailDiskCache::Store(request, Blob(512, 0x44));
    Check(!ThumbnailDiskCache::Load(request).empty(), "the first version is cached");

    // A different size and a new modification time: exactly what saving over
    // a picture in an editor does.
    WriteFile(picture, "the second version, which is longer than the first");
    Check(ThumbnailDiskCache::Load(request).empty(),
          "the edit is a miss, so the tile re-decodes and shows the edit");

    // And the entry that can never be a hit again does not linger: the same
    // tile is about to write its replacement.
    const auto usageAfter = ThumbnailDiskCache::GetUsage();
    ThumbnailDiskCache::Store(request, Blob(512, 0x55));
    Check(ThumbnailDiskCache::GetUsage().files == usageAfter.files + 1,
          "the dead entry was removed, not left beside its replacement");
    Check(ThumbnailDiskCache::Load(request) == Blob(512, 0x55),
          "and the new thumbnail is what is served");
}

// ===== RETENTION: TOUCH WHAT IS USED, SWEEP WHAT IS NOT =====
void TestServingTouchesTheFile() {
    std::cout << "\nServing a thumbnail marks it as still wanted:\n";

    const fs::path picture = g_root / "touched.jpg";
    WriteFile(picture, "a picture in a folder the user keeps visiting");
    const auto request = RequestFor(picture);

    // Which file on disk is this picture's entry: the one that was not there
    // before it was stored. Found this way rather than by computing the name,
    // so the test says nothing about how entries are named.
    const std::vector<fs::path> before = EntryFiles();
    ThumbnailDiskCache::Store(request, Blob(256, 0x66));
    fs::path entry;
    for (const fs::path& path : EntryFiles()) {
        if (std::find(before.begin(), before.end(), path) == before.end()) {
            entry = path;
        }
    }

    Check(!entry.empty(), "the entry is on disk as a file");
    if (entry.empty()) return;

    Backdate(entry, std::chrono::hours(24 * 10));
    Check(HoursAgo(entry) > std::chrono::hours(24 * 9),
          "aged to ten days without being served");

    Check(!ThumbnailDiskCache::Load(request).empty(), "it still serves");
    Check(HoursAgo(entry) < std::chrono::hours(1),
          "and serving it stamped it with today, so the sweep will keep it");
}

void TestTouchIsThrottled() {
    std::cout << "\nThe stamp is not rewritten on every hit:\n";

    const fs::path file = g_root / "stamp.txt";
    WriteFile(file, "x");
    Backdate(file, std::chrono::hours(2));

    Check(!DiskCache::Touch(file.string()),
          "a file served two hours ago is not restamped (the window is a day)");
    Check(HoursAgo(file) >= std::chrono::hours(2), "so its stamp is untouched");

    Backdate(file, std::chrono::hours(30));
    Check(DiskCache::Touch(file.string()),
          "one served thirty hours ago is restamped");
    Check(HoursAgo(file) < std::chrono::hours(1), "to today");

    // The throttle is a parameter so a caller that needs the stamp now can
    // have it; the default is what the Filer uses.
    Check(DiskCache::Touch(file.string(), std::chrono::seconds(0)),
          "a zero interval always writes");
}

void TestSweepExpiresWhatIsNotServed() {
    std::cout << "\nTwo weeks without being served:\n";

    const fs::path directory = g_root / "sweep";
    fs::create_directories(directory);

    const fs::path fresh = directory / "fresh.ucth";
    const fs::path stale = directory / "stale.ucth";
    const fs::path leftover = directory / "interrupted.tmp";
    const fs::path foreign = directory / "notours.txt";
    for (const fs::path& path : { fresh, stale, leftover, foreign }) {
        WriteFile(path, "content");
    }

    Backdate(fresh, std::chrono::hours(24 * 13));    // served last week
    Backdate(stale, std::chrono::hours(24 * 15));    // not for a fortnight
    Backdate(leftover, std::chrono::hours(24 * 15));
    Backdate(foreign, std::chrono::hours(24 * 400));

    const size_t removed = DiskCache::Sweep(directory, { ".ucth", ".tmp" });
    Check(removed == 2, "the two expired files go");
    Check(fs::exists(fresh), "one served inside the window stays");
    Check(!fs::exists(stale), "one that has not been served for two weeks goes");
    Check(!fs::exists(leftover), "and so does an interrupted write's leftover");
    Check(fs::exists(foreign),
          "a file that is not ours is never deleted, whatever its age");
}

void TestSweepKeepsAClockSetBack() {
    std::cout << "\nA stamp in the future:\n";

    const fs::path directory = g_root / "future";
    fs::create_directories(directory);
    const fs::path ahead = directory / "ahead.ucth";
    WriteFile(ahead, "content");
    Backdate(ahead, std::chrono::hours(-24 * 30));   // a month from now

    DiskCache::Sweep(directory, { ".ucth" });
    Check(fs::exists(ahead),
          "reads as fresh rather than as impossibly old, so it survives");
}

void TestSweepOnceRunsOnce() {
    std::cout << "\nThe startup sweep:\n";

    const fs::path directory = g_root / "cache";
    const fs::path stale = directory / "ancient.ucth";
    WriteFile(stale, "content");
    Backdate(stale, std::chrono::hours(24 * 30));

    ThumbnailDiskCache::SweepOnce();
    Check(!fs::exists(stale), "the first call expires what is over the window");

    // Every worker calls it; only the first does anything.
    const fs::path second = directory / "later.ucth";
    WriteFile(second, "content");
    Backdate(second, std::chrono::hours(24 * 30));
    ThumbnailDiskCache::SweepOnce();
    Check(fs::exists(second),
          "and later calls are no-ops, so four workers cost one directory walk");
}

// ===== THE SWITCH, AND HAVING NOWHERE TO WRITE =====
void TestDisabledCacheIsInert() {
    std::cout << "\nSwitched off:\n";

    const fs::path picture = g_root / "off.jpg";
    WriteFile(picture, "a picture");
    const auto request = RequestFor(picture);
    ThumbnailDiskCache::Store(request, Blob(64, 0x77));

    ThumbnailDiskCache::SetEnabled(false);
    Check(!ThumbnailDiskCache::IsEnabled(), "it reports as off");
    Check(!ThumbnailDiskCache::Store(request, Blob(64, 0x88)),
          "nothing is written");
    Check(ThumbnailDiskCache::Load(request).empty(), "and nothing is read");

    // Switched off is a choice, not a broken system: the location and what is
    // in it are still true, and a settings page that showed "nowhere to write"
    // here would be telling the user their machine cannot do something it can.
    Check(!ThumbnailDiskCache::Directory().empty(),
          "the location is still reported while it is off");
    Check(ThumbnailDiskCache::GetUsage().files > 0,
          "and so is what is still sitting in it, waiting to expire");

    ThumbnailDiskCache::SetEnabled(true);
    Check(ThumbnailDiskCache::Load(request) == Blob(64, 0x77),
          "what was on disk is untouched, so switching back on costs nothing");
}

void TestUsageAndClear() {
    std::cout << "\nWhat it occupies, and throwing it away:\n";

    const DiskCache::Usage before = ThumbnailDiskCache::GetUsage();
    Check(before.files > 0 && before.bytes > 0,
          "the cache reports the files and bytes it holds");

    const size_t cleared = ThumbnailDiskCache::Clear();
    Check(cleared == before.files, "clearing removes every one of them");

    const DiskCache::Usage after = ThumbnailDiskCache::GetUsage();
    Check(after.files == 0 && after.bytes == 0, "leaving it empty");
}

} // namespace

int main() {
    std::cout << "Thumbnail disk cache and its retention\n";
    std::cout << "======================================\n";

    std::error_code ec;
    g_root = fs::temp_directory_path(ec) / "UltraCanvasThumbnailDiskCacheTest";
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);
    if (ec) {
        std::cout << "  [FAIL] cannot create a temporary directory to test in\n";
        return 1;
    }
    // Never the user's own cache.
    ThumbnailDiskCache::SetDirectoryOverride((g_root / "cache").string());
    Check(ThumbnailDiskCache::IsAvailable(), "the cache has somewhere to write");

    TestStoredThumbnailComesBack();
    TestGeometryIsPartOfTheIdentity();
    TestUnknownSourceIsAMiss();
    TestEditedSourceInvalidates();
    TestServingTouchesTheFile();
    TestTouchIsThrottled();
    TestSweepExpiresWhatIsNotServed();
    TestSweepKeepsAClockSetBack();
    TestSweepOnceRunsOnce();
    TestDisabledCacheIsInert();
    TestUsageAndClear();

    fs::remove_all(g_root, ec);

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) failed.\n";
    return 1;
}
