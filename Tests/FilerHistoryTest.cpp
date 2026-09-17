// Tests/FilerHistoryTest.cpp
// UltraFiler's recently-used lists (Apps/UltraFiler/UltraFilerHistory.h): that
// the Files / Folders / Apps lists behind the toolbar's clock button survive a
// restart, and that they keep the number of entries Settings > Extras >
// History & Favorites asks for - per section, so a day of opening documents
// cannot push the remembered applications out.
//
// The limit is the part with teeth. It has to hold in three places that are
// easy to get wrong separately: while recording, while reading the file back,
// and at the moment it is lowered - a limit that only takes effect at the next
// restart looks, to the person who just moved the slider, like it did nothing.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

// The config directory is read from the environment, so it is pointed at a
// temporary folder before the header that caches nothing from it is used.
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "UltraFilerHistory.h"

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what.c_str());
    if (!ok) ++failures;
}

void CheckEq(size_t got, size_t want, const std::string& what) {
    const bool ok = got == want;
    std::printf("%s  %s", ok ? "  ok  " : " FAIL ", what.c_str());
    if (!ok) std::printf("  (got %zu, want %zu)", got, want);
    std::printf("\n");
    if (!ok) ++failures;
}

void CheckEq(const std::string& got, const std::string& want,
             const std::string& what) {
    const bool ok = got == want;
    std::printf("%s  %s", ok ? "  ok  " : " FAIL ", what.c_str());
    if (!ok) std::printf("  (got \"%s\", want \"%s\")", got.c_str(), want.c_str());
    std::printf("\n");
    if (!ok) ++failures;
}

// The config directory UltraFilerSettings::GetConfigDirectory() reports, moved
// into a temporary folder so the test never touches a real installation.
void RedirectConfigDirectory(const std::filesystem::path& root) {
    const std::string path = root.string();
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s("APPDATA", path.c_str());
#elif defined(__APPLE__)
    setenv("HOME", path.c_str(), 1);
#else
    setenv("XDG_CONFIG_HOME", path.c_str(), 1);
#endif
}

// The entries of one kind, newest first. Items() rather than Paths(), which
// also drops whatever has meanwhile left the disk - these paths never existed.
std::vector<std::string> PathsOf(const UltraFilerHistory& history,
                                 FilerHistoryKind kind) {
    std::vector<std::string> paths;
    for (const FilerHistoryItem& item : history.Items(kind))
        paths.push_back(item.path);
    return paths;
}

size_t CountOf(const UltraFilerHistory& history, FilerHistoryKind kind) {
    return history.Items(kind).size();
}

std::string Numbered(const char* prefix, int index) {
    return std::string(prefix) + std::to_string(index);
}

// ===== SAVED ON USE, LOADED ON START =====
// The three lists come back after a restart, each with its own entries, newest
// first - the order the History view shows them in.
void TestSurvivesRestart() {
    std::printf("\n-- persistence --\n");

    UltraFilerHistory history;
    history.Record(FilerHistoryKind::File,   "/docs/first.txt");
    history.Record(FilerHistoryKind::File,   "/docs/second.txt");
    history.Record(FilerHistoryKind::Folder, "/docs");
    history.Record(FilerHistoryKind::App,    "/usr/bin/editor");

    Check(std::filesystem::exists(UltraFilerHistory::GetHistoryPath()),
          "recording writes history.txt without waiting for exit");

    // A second instance is the next run of the application.
    UltraFilerHistory reloaded;
    Check(reloaded.Load(), "the file is read back at start-up");
    CheckEq(CountOf(reloaded, FilerHistoryKind::File), 2u, "both files came back");
    CheckEq(CountOf(reloaded, FilerHistoryKind::Folder), 1u, "the folder came back");
    CheckEq(CountOf(reloaded, FilerHistoryKind::App), 1u, "the app came back");

    const std::vector<std::string> files = PathsOf(reloaded, FilerHistoryKind::File);
    CheckEq(files.front(), "/docs/second.txt", "the newest file is first");
    CheckEq(PathsOf(reloaded, FilerHistoryKind::App).front(), "/usr/bin/editor",
            "the app kept its own list");

    // Using a path again moves it to the front and counts the use rather than
    // listing it twice.
    reloaded.Record(FilerHistoryKind::File, "/docs/first.txt");
    CheckEq(CountOf(reloaded, FilerHistoryKind::File), 2u,
            "using a remembered file again does not duplicate it");
    CheckEq(PathsOf(reloaded, FilerHistoryKind::File).front(), "/docs/first.txt",
            "using it again moves it to the front");
    CheckEq(static_cast<size_t>(reloaded.Items(FilerHistoryKind::File).front().useCount),
            2u, "the use was counted");
}

// ===== THE LIMIT, WHILE RECORDING =====
// It counts per section: filling the Files list must leave the Apps list alone.
void TestLimitWhileRecording() {
    std::printf("\n-- the limit while recording --\n");

    UltraFilerHistory history;
    history.ClearAll();
    history.SetLimit(12);
    CheckEq(history.Limit(), 12u, "the limit is what was asked for");

    for (int i = 0; i < 40; ++i)
        history.Record(FilerHistoryKind::File, Numbered("/docs/file", i));
    for (int i = 0; i < 5; ++i)
        history.Record(FilerHistoryKind::App, Numbered("/usr/bin/app", i));

    CheckEq(CountOf(history, FilerHistoryKind::File), 12u,
            "the Files list stops at the limit");
    CheckEq(CountOf(history, FilerHistoryKind::App), 5u,
            "the Apps list is capped separately, so files cannot crowd apps out");
    CheckEq(PathsOf(history, FilerHistoryKind::File).front(), "/docs/file39",
            "what is kept is the newest");
    CheckEq(PathsOf(history, FilerHistoryKind::File).back(), "/docs/file28",
            "what dropped off is the oldest");
}

// ===== LOWERING THE LIMIT =====
// Takes effect at once, and reaches the file: the entries past the new limit
// must not come back at the next start.
void TestLoweringTheLimit() {
    std::printf("\n-- lowering the limit --\n");

    UltraFilerHistory history;
    history.ClearAll();
    history.SetLimit(50);
    for (int i = 0; i < 50; ++i)
        history.Record(FilerHistoryKind::File, Numbered("/docs/file", i));
    CheckEq(CountOf(history, FilerHistoryKind::File), 50u, "50 entries recorded");

    history.SetLimit(10);
    CheckEq(CountOf(history, FilerHistoryKind::File), 10u,
            "a lowered limit drops the entries past it straight away");
    CheckEq(PathsOf(history, FilerHistoryKind::File).front(), "/docs/file49",
            "the newest entries are the ones kept");

    UltraFilerHistory reloaded;
    reloaded.SetLimit(50);          // the limit is no longer what trims it
    reloaded.Load();
    CheckEq(CountOf(reloaded, FilerHistoryKind::File), 10u,
            "the trim reached the file, so the dropped entries stay gone");
}

// ===== A FILE LONGER THAN THE LIMIT IN FORCE =====
// The limit was lowered while UltraFiler was not running (the config file
// edited, or a shorter limit set from another machine's synced settings): the
// file is trimmed as it is read, and written back at its new length.
void TestLoadTrimsAndRewrites() {
    std::printf("\n-- a file longer than the limit --\n");

    UltraFilerHistory history;
    history.ClearAll();
    history.SetLimit(UltraFilerHistory::kMaxItemsPerKind);
    for (int i = 0; i < 60; ++i)
        history.Record(FilerHistoryKind::Folder, Numbered("/work/folder", i));

    UltraFilerHistory reloaded;
    reloaded.SetLimit(20);
    reloaded.Load();
    CheckEq(CountOf(reloaded, FilerHistoryKind::Folder), 20u,
            "the file is trimmed as it is read");
    CheckEq(PathsOf(reloaded, FilerHistoryKind::Folder).front(), "/work/folder59",
            "and it is the newest entries that are kept");

    UltraFilerHistory again;
    again.SetLimit(UltraFilerHistory::kMaxItemsPerKind);
    again.Load();
    CheckEq(CountOf(again, FilerHistoryKind::Folder), 20u,
            "the shortened list was written back, not merely shown short");
}

// ===== THE LIMIT IS CLAMPED =====
// A number out of range cannot turn the history off altogether or make it
// unbounded, whichever way it arrived.
void TestLimitClamped() {
    std::printf("\n-- the limit is clamped --\n");

    UltraFilerHistory history;
    history.SetLimit(0);
    CheckEq(history.Limit(), UltraFilerHistory::kMinItemsPerKind,
            "0 is raised to the smallest useful list");
    history.SetLimit(1000000);
    CheckEq(history.Limit(), UltraFilerHistory::kMaxItemsPerKind,
            "an enormous number is held at the ceiling");

    CheckEq(UltraFilerHistory::kDefaultItemsPerKind,
            static_cast<size_t>(UltraFilerSettings::kDefaultHistoryEntries),
            "the default is the setting's own, so the two cannot drift apart");
}

// ===== THE SETTING =====
// Extras > History & Favorites' "Limit of entries" is written to the config
// file and read back, and a hand-edited value out of range is brought inside it.
void TestSettingRoundTrip() {
    std::printf("\n-- the setting --\n");

    {
        UltraFilerSettings settings;
        CheckEq(static_cast<size_t>(settings.historyMaxEntries),
                static_cast<size_t>(UltraFilerSettings::kDefaultHistoryEntries),
                "a fresh installation starts at the default");
        settings.historyMaxEntries = 42;
        Check(settings.Save(), "the limit is saved with the other settings");
    }
    {
        UltraFilerSettings settings;
        Check(settings.Load(), "the config file is read back");
        CheckEq(static_cast<size_t>(settings.historyMaxEntries), 42u,
                "the limit came back");
    }

    // Hand-edited past the ends of the slider's range.
    {
        std::ofstream file(UltraFilerSettings::GetConfigPath(), std::ios::app);
        file << "extras.history.max.entries = 999999\n";
    }
    {
        UltraFilerSettings settings;
        settings.Load();
        CheckEq(static_cast<size_t>(settings.historyMaxEntries),
                static_cast<size_t>(UltraFilerSettings::kMaxHistoryEntries),
                "a value above the range is held at the ceiling");
    }
    {
        std::ofstream file(UltraFilerSettings::GetConfigPath(), std::ios::app);
        file << "extras.history.max.entries = -5\n";
    }
    {
        UltraFilerSettings settings;
        settings.Load();
        CheckEq(static_cast<size_t>(settings.historyMaxEntries),
                static_cast<size_t>(UltraFilerSettings::kMinHistoryEntries),
                "a value below the range is raised to the floor");
    }
}

// ===== WHAT HAS LEFT THE DISK =====
// Paths() is what the History view is filled from: an entry whose file is gone
// is forgotten rather than shown as a dead tile.
void TestMissingPathsDropOut() {
    std::printf("\n-- entries that have left the disk --\n");

    const std::filesystem::path dir =
            std::filesystem::path(UltraFilerSettings::GetConfigDirectory()) / "files";
    std::filesystem::create_directories(dir);
    const std::filesystem::path kept = dir / "kept.txt";
    const std::filesystem::path gone = dir / "gone.txt";
    { std::ofstream(kept) << "x"; }
    { std::ofstream(gone) << "x"; }

    UltraFilerHistory history;
    history.ClearAll();
    history.Record(FilerHistoryKind::File, kept.string());
    history.Record(FilerHistoryKind::File, gone.string());
    CheckEq(history.Paths(FilerHistoryKind::File).size(), 2u,
            "both files are listed while both exist");

    std::filesystem::remove(gone);
    const std::vector<std::string> paths = history.Paths(FilerHistoryKind::File);
    CheckEq(paths.size(), 1u, "the deleted file is forgotten");
    CheckEq(paths.front(), kept.string(), "the one still there is kept");
}

} // namespace

int main() {
    std::printf("=== UltraFiler History Test ===\n");

    std::error_code ec;
    const std::filesystem::path root =
            std::filesystem::temp_directory_path(ec) / "ultrafiler-history-test";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        std::printf(" FAIL  could not create the temporary config directory\n");
        return 1;
    }
    RedirectConfigDirectory(root);
    std::printf("config directory: %s\n",
                UltraFilerSettings::GetConfigDirectory().c_str());

    TestSurvivesRestart();
    TestLimitWhileRecording();
    TestLoweringTheLimit();
    TestLoadTrimsAndRewrites();
    TestLimitClamped();
    TestSettingRoundTrip();
    TestMissingPathsDropOut();

    std::filesystem::remove_all(root, ec);

    std::printf("\n%s\n", failures == 0 ? "All history tests passed."
                                        : "SOME HISTORY TESTS FAILED.");
    return failures == 0 ? 0 : 1;
}
