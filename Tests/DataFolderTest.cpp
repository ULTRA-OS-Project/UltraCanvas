// Tests/DataFolderTest.cpp
// UltraCanvasDataFolder.h: each platform's rule for the per-user application
// data folder, the folders earlier versions used, and moving one of those into
// place once - renamed whole, never over data already there, never from a
// folder that is not the application's.
//
// Self-contained: no test framework, links nothing (the header is header-only).
// Author: UltraCanvas Framework
#include "UltraCanvasDataFolder.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

// Compared as paths, so the separator the platform joins with does not matter.
static bool SamePath(const fs::path& actual, const fs::path& expected) {
    return actual.lexically_normal() == expected.lexically_normal();
}
static bool SamePath(const std::string& actual, const fs::path& expected) {
    return SamePath(PathFromUtf8(actual), expected);
}

static void WriteFile(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

static std::string ReadFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void TestRule() {
    std::printf("rule\n");
    // Windows: %APPDATA%; without it, the folder %APPDATA% would have named.
    Check(SamePath(DataFolderRootFor(DataFolderPlatform::Windows, "", "C:/Users/erika/AppData/Roaming", ""),
                   fs::path("C:/Users/erika/AppData/Roaming")), "Windows: APPDATA");
    Check(SamePath(DataFolderRootFor(DataFolderPlatform::Windows, "", "", "C:/Users/erika"),
                   fs::path("C:/Users/erika/AppData/Roaming")), "Windows without APPDATA: under HOME");
    // macOS: Application Support, not ~/.local/share.
    Check(SamePath(DataFolderRootFor(DataFolderPlatform::MacOS, "", "", "/Users/erika"),
                   fs::path("/Users/erika/Library/Application Support")), "macOS: Application Support");
    // Linux and the rest: XDG.
    Check(SamePath(DataFolderRootFor(DataFolderPlatform::Unix, "", "", "/home/erika"),
                   fs::path("/home/erika/.local/share")), "Linux: ~/.local/share");
    Check(SamePath(DataFolderRootFor(DataFolderPlatform::Unix, "", "C:/AppData", "/home/erika"),
                   fs::path("/home/erika/.local/share")), "APPDATA means nothing outside Windows");
    Check(SamePath(DataFolderRootFor(DataFolderPlatform::MacOS, "", "C:/AppData", "/Users/erika"),
                   fs::path("/Users/erika/Library/Application Support")), "nor on macOS");
    // XDG_DATA_HOME wins everywhere.
    Check(SamePath(DataFolderRootFor(DataFolderPlatform::Windows, "D:/portable", "C:/AppData", "C:/Users/erika"),
                   fs::path("D:/portable")), "XDG_DATA_HOME wins on Windows");
    Check(SamePath(DataFolderRootFor(DataFolderPlatform::MacOS, "/Volumes/data", "", "/Users/erika"),
                   fs::path("/Volumes/data")), "XDG_DATA_HOME wins on macOS");
    Check(SamePath(DataFolderRootFor(DataFolderPlatform::Unix, "/data", "", "/home/erika"),
                   fs::path("/data")), "XDG_DATA_HOME wins on Linux");
    // No home of any kind: nothing to resolve.
    Check(DataFolderRootFor(DataFolderPlatform::Unix, "", "", "").empty(), "no home: nothing");
    Check(DataFolderRootFor(DataFolderPlatform::MacOS, "", "", "").empty(), "no home on macOS: nothing");
    Check(DataFolderRootFor(DataFolderPlatform::Windows, "", "", "").empty(), "no profile on Windows: nothing");

    // The folders the rule moved away from.
    Check(LegacyUnixDataFolders(DataFolderPlatform::Unix, "/home/erika", "UltraMail").empty(),
          "Linux kept its folder");
    Check(LegacyUnixDataFolders(DataFolderPlatform::MacOS, "", "UltraMail").empty(),
          "no home, no legacy folder");
    const auto mac = LegacyUnixDataFolders(DataFolderPlatform::MacOS, "/Users/erika", "UltraMail");
    Check(mac.size() == 1 && SamePath(mac[0], fs::path("/Users/erika/.local/share/UltraMail")),
          "macOS: ~/.local/share/<App>");
    const auto win = LegacyUnixDataFolders(DataFolderPlatform::Windows, "C:/Users/erika", "UltraMail");
    Check(win.size() == 1 && SamePath(win[0], fs::path("C:/Users/erika/.local/share/UltraMail")),
          "Windows with HOME: ~/.local/share/<App>");

    // The live environment names a folder on every CI machine; an app name in
    // any script is joined as UTF-8.
    const fs::path live = UltraCanvasDataFolder("App-ทดสอบ-🚀");
    Check(live.empty() || PathToUtf8(live.filename()) == "App-ทดสอบ-🚀", "the app name survives the join");
    Check(live.empty() == UltraCanvasDataFolderRoot().empty(), "empty exactly when there is no root");
}

static void TestAdopt(const fs::path& scratch) {
    std::printf("adopt\n");
    const std::vector<std::string> markers = {"mail.db", "social.db"};

    // The whole folder moves, and nothing is left behind.
    {
        const fs::path legacy = scratch / "move" / "old" / "App";
        const fs::path target = scratch / "move" / "new" / "App";
        WriteFile(legacy / "social.db", "db");   // the second marker counts too
        WriteFile(legacy / "vault" / "device.key", "key");
        std::string error;
        Check(SamePath(AdoptLegacyDataFolder(PathToUtf8(target), {PathToUtf8(legacy)}, markers, error), legacy),
              "moved from the old folder");
        Check(error.empty(), "moved without error");
        Check(ReadFile(target / "social.db") == "db", "the database came along");
        Check(ReadFile(target / "vault" / "device.key") == "key", "and the vault beside it");
        Check(!fs::exists(legacy), "nothing left behind");
        // A second start finds the data in place and moves nothing.
        Check(AdoptLegacyDataFolder(PathToUtf8(target), {PathToUtf8(legacy)}, markers, error).empty() && error.empty(),
              "nothing to move the second time");
    }
    // Data already in place is never replaced by an old folder.
    {
        const fs::path legacy = scratch / "keep" / "old" / "App";
        const fs::path target = scratch / "keep" / "new" / "App";
        WriteFile(legacy / "mail.db", "old");
        WriteFile(target / "mail.db", "new");
        std::string error;
        Check(AdoptLegacyDataFolder(PathToUtf8(target), {PathToUtf8(legacy)}, markers, error).empty(),
              "data in place is kept");
        Check(ReadFile(target / "mail.db") == "new" && ReadFile(legacy / "mail.db") == "old",
              "and neither folder is touched");
    }
    // Nothing to adopt: an empty name, a missing folder, one without a
    // marker, the target itself - and no folder is made for nothing.
    {
        const fs::path stray  = scratch / "skip" / "old" / "App";
        const fs::path target = scratch / "skip" / "new" / "App";
        WriteFile(stray / "notes.txt", "not the app's");
        std::string error;
        Check(AdoptLegacyDataFolder(PathToUtf8(target),
                                    {"", PathToUtf8(scratch / "missing"), PathToUtf8(stray), PathToUtf8(target)},
                                    markers, error).empty() && error.empty(),
              "nothing adopted from folders without a marker");
        Check(!fs::exists(target), "no folder made for nothing");
        Check(fs::is_regular_file(stray / "notes.txt"), "the stray folder is untouched");
        Check(AdoptLegacyDataFolder("", {PathToUtf8(stray)}, markers, error).empty(),
              "an empty data folder resolves to nothing");
    }
    // An empty folder in the way (an installer's) is replaced.
    {
        const fs::path legacy = scratch / "empty" / "old" / "App";
        const fs::path target = scratch / "empty" / "new" / "App";
        WriteFile(legacy / "mail.db", "db");
        fs::create_directories(target);
        std::string error;
        Check(SamePath(AdoptLegacyDataFolder(PathToUtf8(target), {PathToUtf8(legacy)}, markers, error), legacy),
              "moved over the empty folder");
        Check(ReadFile(target / "mail.db") == "db" && !fs::exists(legacy), "and the data is there");
    }
    // A folder with other files in the way: the data is copied beside them.
    {
        const fs::path legacy = scratch / "beside" / "old" / "App";
        const fs::path target = scratch / "beside" / "new" / "App";
        WriteFile(legacy / "mail.db", "db");
        WriteFile(legacy / "sub" / "a.txt", "a");
        WriteFile(target / "preferences.ini", "[ui]\n");
        std::string error;
        Check(SamePath(AdoptLegacyDataFolder(PathToUtf8(target), {PathToUtf8(legacy)}, markers, error), legacy),
              "copied beside the files already there");
        Check(error.empty(), "copied without error");
        Check(ReadFile(target / "mail.db") == "db" && ReadFile(target / "sub" / "a.txt") == "a",
              "everything copied");
        Check(ReadFile(target / "preferences.ini") == "[ui]\n", "the existing file kept");
        Check(!fs::exists(legacy), "the old folder removed after the copy");
    }
    // The first candidate with data wins; the rest stay where they are.
    {
        const fs::path first  = scratch / "order" / "first" / "App";
        const fs::path second = scratch / "order" / "second" / "App";
        const fs::path target = scratch / "order" / "new" / "App";
        WriteFile(second / "mail.db", "second");
        WriteFile(first / "readme.txt", "no marker here");
        std::string error;
        Check(SamePath(AdoptLegacyDataFolder(PathToUtf8(target),
                                             {PathToUtf8(first), PathToUtf8(second)}, markers, error),
                       second),
              "the first candidate with data");
        Check(fs::is_regular_file(first / "readme.txt"), "the other candidate untouched");
    }
}

int main() {
    // Named in Thai and emoji so every path goes through the UTF-8 conversions.
    const fs::path scratch = fs::temp_directory_path() / PathFromUtf8("ultracanvas-datafolder-ทดสอบ-🚀");
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch, ec);

    TestRule();
    TestAdopt(scratch);

    fs::remove_all(scratch, ec);
    std::printf("DataFolderTest: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
