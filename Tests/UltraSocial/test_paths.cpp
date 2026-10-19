// Tests/UltraSocial/test_paths.cpp
// The application data folder (UltraSocialPaths.h): each platform's rule,
// where 0.1.x may have left its data, moving that data into place once (a
// real UltraVault still opens there), and the folder being its owner's alone.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraSocialCredentialVault.h"
#include "UltraSocialPaths.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace UltraSocial;
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
                         PathFromUtf8("ultrasocial-paths-" + tag + "-ทดสอบ-🚀");
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

// What 0.1.x left behind: the database and the vault with its device key.
void MakeLegacyData(const fs::path& dir) {
    WriteFile(dir / "social.db", "db");
    WriteFile(dir / "vault" / "ultrasocial.vault", "vault");
    WriteFile(dir / "vault" / "device.key", "key");
}

} // namespace

TEST(paths_resolve_per_platform) {
    // Windows: %APPDATA%, else the roaming folder under the profile.
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Windows, "", "C:/Users/erika/AppData/Roaming", ""),
                     fs::path("C:/Users/erika/AppData/Roaming") / "UltraSocial"));
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Windows, "", "", "C:/Users/erika"),
                     fs::path("C:/Users/erika") / "AppData" / "Roaming" / "UltraSocial"));
    // macOS: Application Support, not ~/.local/share.
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::MacOS, "", "", "/Users/erika"),
                     fs::path("/Users/erika/Library/Application Support/UltraSocial")));
    // Linux and the rest: XDG, unchanged from 0.1.x.
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Unix, "", "", "/home/erika"),
                     fs::path("/home/erika/.local/share/UltraSocial")));
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Unix, "/data", "", "/home/erika"),
                     fs::path("/data/UltraSocial")));
    // XDG_DATA_HOME wins everywhere, as in UltraMail.
    REQUIRE(SamePath(ResolveDataDir(DataDirPlatform::Windows, "D:/portable", "C:/AppData", ""),
                     fs::path("D:/portable/UltraSocial")));
    // No home of any kind: nothing to resolve.
    REQUIRE(ResolveDataDir(DataDirPlatform::Unix, "", "", "").empty());
    REQUIRE(ResolveDataDir(DataDirPlatform::Windows, "", "", "").empty());
}

TEST(paths_legacy_locations) {
    REQUIRE(LegacyDataDirs(DataDirPlatform::Unix, "/home/erika", "/tmp", "/opt/us").empty());

    auto mac = LegacyDataDirs(DataDirPlatform::MacOS, "/Users/erika", "/tmp", "/Applications");
    REQUIRE_EQ(mac.size(), static_cast<size_t>(1));
    REQUIRE(SamePath(mac[0], fs::path("/Users/erika/.local/share/UltraSocial")));

    // Windows without HOME (the usual case): the working directory and the
    // executable's folder, where "./UltraSocial" landed.
    auto win = LegacyDataDirs(DataDirPlatform::Windows, "", "C:/Work", "C:/Apps/UltraCanvas");
    REQUIRE_EQ(win.size(), static_cast<size_t>(2));
    REQUIRE(SamePath(win[0], fs::path("C:/Work/UltraSocial")));
    REQUIRE(SamePath(win[1], fs::path("C:/Apps/UltraCanvas/UltraSocial")));
    REQUIRE_EQ(LegacyDataDirs(DataDirPlatform::Windows, "C:/Users/erika", "C:/Work", "C:/Apps").size(),
               static_cast<size_t>(3));
}

TEST(paths_adopt_moves_legacy_data) {
    const fs::path root = Scratch("adopt");
    const fs::path legacy = root / "old" / "UltraSocial";
    const fs::path target = root / "new" / "UltraSocial";
    MakeLegacyData(legacy);

    std::string error;
    const std::string from = AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(legacy)}, error);
    REQUIRE(error.empty());
    REQUIRE(SamePath(from, legacy));
    REQUIRE(!fs::exists(legacy));
    REQUIRE_EQ(ReadFile(target / "social.db"), std::string("db"));
    REQUIRE_EQ(ReadFile(target / "vault" / "ultrasocial.vault"), std::string("vault"));
    REQUIRE_EQ(ReadFile(target / "vault" / "device.key"), std::string("key"));

    // A second start finds the data in place and moves nothing.
    MakeLegacyData(legacy);
    REQUIRE(AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(legacy)}, error).empty());
    REQUIRE(fs::exists(legacy / "social.db"));
    fs::remove_all(root);
}

TEST(paths_adopt_skips_folders_without_a_database) {
    const fs::path root = Scratch("skip");
    // Outside Windows "./UltraSocial" in the build tree is the executable; a
    // folder of that name without social.db is not UltraSocial's data either.
    WriteFile(root / "binary" / "UltraSocial", "ELF");
    fs::create_directories(root / "empty" / "UltraSocial");
    const fs::path real = root / "real" / "UltraSocial";
    MakeLegacyData(real);
    const fs::path target = root / "new" / "UltraSocial";
    fs::create_directories(target);   // an empty folder in the way is replaced

    std::string error;
    const std::string from = AdoptLegacyDataDir(
        PathToUtf8(target),
        {PathToUtf8(root / "binary" / "UltraSocial"), PathToUtf8(root / "empty" / "UltraSocial"),
         PathToUtf8(real)},
        error);
    REQUIRE(error.empty());
    REQUIRE(SamePath(from, real));
    REQUIRE(fs::is_regular_file(root / "binary" / "UltraSocial"));
    REQUIRE(fs::is_regular_file(target / "social.db"));

    // The target itself listed as a candidate is never "moved" onto itself.
    REQUIRE(AdoptLegacyDataDir(PathToUtf8(root / "fresh"), {PathToUtf8(root / "fresh")}, error).empty());
    fs::remove_all(root);
}

TEST(paths_prepare_makes_a_private_folder) {
    const fs::path root = Scratch("prepare");
    const fs::path dir = root / "a" / "b" / "UltraSocial";
    std::string error;
    REQUIRE(PrepareDataDir(PathToUtf8(dir), error));
    REQUIRE(error.empty());
    REQUIRE(fs::is_directory(dir));
#if !defined(_WIN32)
    const fs::perms perms = fs::status(dir).permissions();
    REQUIRE((perms & fs::perms::all) == fs::perms::owner_all);
#endif
    // A file where the folder should be: reported, not ignored.
    WriteFile(root / "file", "x");
    REQUIRE(!PrepareDataDir(PathToUtf8(root / "file"), error));
    REQUIRE(!error.empty());
    fs::remove_all(root);
}

TEST(paths_adopted_vault_still_opens) {
    // The logins live in UltraVault (UltraSocialCredentialVault.h). Moving the
    // folder must keep them: the vault and its device key travel together and
    // nothing in them is tied to where they were.
    const fs::path root = Scratch("vault");
    const fs::path legacy = root / "old" / "UltraSocial";
    const fs::path target = root / "new" / "UltraSocial";
    WriteFile(legacy / "social.db", "db");
    {
        CredentialVault vault(PathToUtf8(legacy / "vault"));
        REQUIRE(vault.TryAutoUnlock());
        REQUIRE(vault.Store("mastodon-erika", "{\"token\":\"secret\"}"));
        vault.Lock();
    }

    std::string error;
    REQUIRE(!AdoptLegacyDataDir(PathToUtf8(target), {PathToUtf8(legacy)}, error).empty());
    {
        CredentialVault vault(PathToUtf8(target / "vault"));
        REQUIRE(vault.TryAutoUnlock());
        std::string secret;
        REQUIRE(vault.Retrieve("mastodon-erika", secret));
        REQUIRE_EQ(secret, std::string("{\"token\":\"secret\"}"));
        vault.Lock();
    }
    fs::remove_all(root);
}

TEST(paths_database_renamed_to_ultrasocial_db) {
    const fs::path root = Scratch("rename");
    REQUIRE(SamePath(DatabasePath(PathToUtf8(root)), root / "ultrasocial.db"));

    // 0.1.x's social.db, with the journal a crash left beside it.
    WriteFile(root / "social.db", "db");
    WriteFile(root / "social.db-journal", "journal");
    std::string error;
    REQUIRE(RenameLegacyDatabase(PathToUtf8(root), error));
    REQUIRE(error.empty());
    REQUIRE_EQ(ReadFile(root / "ultrasocial.db"), std::string("db"));
    REQUIRE_EQ(ReadFile(root / "ultrasocial.db-journal"), std::string("journal"));
    REQUIRE(!fs::exists(root / "social.db"));
    REQUIRE(!fs::exists(root / "social.db-journal"));

    // Done once: a social.db that turns up later never replaces the database.
    WriteFile(root / "social.db", "stale");
    REQUIRE(!RenameLegacyDatabase(PathToUtf8(root), error));
    REQUIRE(error.empty());
    REQUIRE_EQ(ReadFile(root / "ultrasocial.db"), std::string("db"));

    // A folder already holding ultrasocial.db is in place: nothing is moved
    // into it.
    const fs::path legacy = root / "old" / "UltraSocial";
    MakeLegacyData(legacy);
    REQUIRE(AdoptLegacyDataDir(PathToUtf8(root), {PathToUtf8(legacy)}, error).empty());
    REQUIRE(fs::exists(legacy / "social.db"));
    fs::remove_all(root);
}
