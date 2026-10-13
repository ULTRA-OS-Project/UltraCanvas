// Tests/UltraMail/test_paths.cpp
// The application data folder (UltraMailPaths.h): each platform's rule,
// where earlier versions left the data on macOS, and moving that data into
// place once - the whole folder, vault included - without touching a folder
// that already holds a mailbox.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailPaths.h"

#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace UltraMail;
namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace {

// Compared as paths, so the separator the platform joins with does not matter.
bool SamePath(const std::string& actual, const fs::path& expected) {
    return PathFromUtf8(actual).lexically_normal() == expected.lexically_normal();
}

// A fresh scratch folder under the temp directory, named in Thai and emoji so
// every path below goes through the UTF-8 conversions.
fs::path Scratch(const std::string& tag) {
    const fs::path dir = fs::temp_directory_path() /
                         PathFromUtf8("ultramail-paths-" + tag + "-ทดสอบ-🚀");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

void WriteFile(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

std::string ReadFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// What an earlier version left behind: the mailbox, the preferences and the
// vault with its device key.
void MakeLegacyData(const fs::path& dir) {
    WriteFile(dir / "mail.db", "mailbox");
    WriteFile(dir / "preferences.ini", "[ui]\n");
    WriteFile(dir / "vault" / "ultramail.vault", "vault");
    WriteFile(dir / "vault" / "device.key", "key");
}

} // namespace

TEST(paths_resolve_per_platform) {
    // Windows: %APPDATA%; without it, the folder UltraMail always used under HOME.
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Windows, "", "C:/Users/erika/AppData/Roaming", ""),
                     fs::path("C:/Users/erika/AppData/Roaming") / "UltraMail"));
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Windows, "", "", "C:/Users/erika"),
                     fs::path("C:/Users/erika") / ".local" / "share" / "UltraMail"));
    // macOS: Application Support, not ~/.local/share.
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::MacOS, "", "", "/Users/erika"),
                     fs::path("/Users/erika/Library/Application Support/UltraMail")));
    // Linux and the rest: XDG, unchanged.
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Unix, "", "", "/home/erika"),
                     fs::path("/home/erika/.local/share/UltraMail")));
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Unix, "/data", "", "/home/erika"),
                     fs::path("/data/UltraMail")));
    // %APPDATA% means nothing outside Windows.
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Unix, "", "C:/AppData", "/home/erika"),
                     fs::path("/home/erika/.local/share/UltraMail")));
    // XDG_DATA_HOME wins everywhere.
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Windows, "D:/portable", "C:/AppData", ""),
                     fs::path("D:/portable/UltraMail")));
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::MacOS, "/Volumes/data", "", "/Users/erika"),
                     fs::path("/Volumes/data/UltraMail")));
    // No home of any kind: nothing to resolve.
    REQUIRE(ResolveDataDir(DataDirPlatform::Unix, "", "", "").empty());
    REQUIRE(ResolveDataDir(DataDirPlatform::Windows, "", "", "").empty());
    REQUIRE(ResolveDataDir(DataDirPlatform::MacOS, "", "", "").empty());
}

TEST(paths_legacy_locations) {
    // Only macOS moved; Linux and Windows kept their folders.
    REQUIRE(LegacyDataDirs(DataDirPlatform::Unix, "/home/erika").empty());
    REQUIRE(LegacyDataDirs(DataDirPlatform::Windows, "C:/Users/erika").empty());
    REQUIRE(LegacyDataDirs(DataDirPlatform::MacOS, "").empty());

    auto mac = LegacyDataDirs(DataDirPlatform::MacOS, "/Users/erika");
    REQUIRE_EQ(mac.size(), static_cast<size_t>(1));
    REQUIRE(SamePath(mac[0], fs::path("/Users/erika/.local/share/UltraMail")));
}

TEST(paths_adopt_moves_legacy_data) {
    const fs::path root = Scratch("adopt");
    const fs::path legacy = root / "old" / "UltraMail";
    const fs::path target = root / "new" / "UltraMail";
    MakeLegacyData(legacy);

    std::string error;
    const std::string from = AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(legacy)}, error);
    REQUIRE(error.empty());
    REQUIRE(SamePath(from, legacy));

    // Everything came along, and nothing is left behind.
    REQUIRE(fs::is_regular_file(target / "mail.db"));
    REQUIRE_EQ(ReadFile(target / "preferences.ini"), std::string("[ui]\n"));
    REQUIRE_EQ(ReadFile(target / "vault" / "ultramail.vault"), std::string("vault"));
    REQUIRE_EQ(ReadFile(target / "vault" / "device.key"), std::string("key"));
    REQUIRE(!fs::exists(legacy));

    // A second start finds the data in place and moves nothing.
    REQUIRE(AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(legacy)}, error).empty());
    REQUIRE(error.empty());

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(paths_adopt_leaves_an_existing_mailbox_alone) {
    const fs::path root = Scratch("keep");
    const fs::path legacy = root / "old" / "UltraMail";
    const fs::path target = root / "new" / "UltraMail";
    MakeLegacyData(legacy);
    WriteFile(target / "mail.db", "the newer mailbox");

    std::string error;
    REQUIRE(AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(legacy)}, error).empty());
    REQUIRE(error.empty());
    REQUIRE_EQ(ReadFile(target / "mail.db"), std::string("the newer mailbox"));
    REQUIRE(fs::is_regular_file(legacy / "mail.db"));   // untouched

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(paths_adopt_skips_folders_without_a_mailbox) {
    const fs::path root = Scratch("skip");
    const fs::path stray = root / "old" / "UltraMail";
    const fs::path target = root / "new" / "UltraMail";
    WriteFile(stray / "notes.txt", "not UltraMail's");   // a folder of that name, no mailbox

    std::string error;
    // Nothing to adopt: a missing candidate, one without a mailbox, the target itself.
    REQUIRE(AdoptLegacyDataDir(PathToUtf8(target),
                               {"", PathToUtf8(root / "missing"), PathToUtf8(stray), PathToUtf8(target)},
                               error).empty());
    REQUIRE(error.empty());
    REQUIRE(!fs::exists(target));
    REQUIRE(fs::is_regular_file(stray / "notes.txt"));
    // And an empty data folder name resolves to nothing at all.
    REQUIRE(AdoptLegacyDataDir("", {PathToUtf8(stray)}, error).empty());

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(paths_adopt_replaces_an_empty_folder_in_the_way) {
    const fs::path root = Scratch("empty");
    const fs::path legacy = root / "old" / "UltraMail";
    const fs::path target = root / "new" / "UltraMail";
    MakeLegacyData(legacy);
    fs::create_directories(target);   // an installer's empty folder

    std::string error;
    REQUIRE(SamePath(AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(legacy)}, error), legacy));
    REQUIRE(error.empty());
    REQUIRE(fs::is_regular_file(target / "mail.db"));
    REQUIRE(!fs::exists(legacy));

    std::error_code ec;
    fs::remove_all(root, ec);
}
