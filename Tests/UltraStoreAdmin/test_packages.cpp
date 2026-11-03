// Tests/UltraStoreAdmin/test_packages.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "PackageStore.h"
#include "Verifier.h"

using namespace UltraStoreAdmin;
using namespace admin_test;

TEST(formats_follow_the_extension) {
    REQUIRE_EQ(FormatOf("App-1.0.zip"), std::string("zip"));
    REQUIRE_EQ(FormatOf("/x/y/App.7Z"), std::string("7z"));
    REQUIRE_EQ(FormatOf("app.tar.zst"), std::string("tar.zst"));
    REQUIRE_EQ(FormatOf("app.tgz"), std::string("tar.gz"));
    REQUIRE_EQ(FormatOf("app.tar.xz"), std::string("tar.xz"));
    REQUIRE_EQ(FormatOf("App.AppImage"), std::string("appimage"));
    REQUIRE_EQ(FormatOf("app.exe"), std::string());
}

TEST(a_package_is_stored_under_its_hash_once) {
    const std::string dir = TempDir("pkg");
    const std::string source = Join(dir, "hello-1.0.zip");
    REQUIRE(WriteFile(source, "not really a zip, but bytes are bytes"));
    const std::string store = Join(dir, "packages");
    const auto first = ImportPackage(source, store);
    REQUIRE(first.Ok());
    REQUIRE_EQ(first.Value().format, std::string("zip"));
    REQUIRE_EQ(first.Value().size, uint64_t(37));
    REQUIRE_EQ(first.Value().sha256, UltraStore::Sha256HexOfFile(source));
    REQUIRE(first.Value().file.find(first.Value().sha256 + ".zip") != std::string::npos);
    REQUIRE_EQ(ReadFile(first.Value().file), ReadFile(source));
    // The same bytes under another name: the same file, nothing rewritten.
    const std::string copy = Join(dir, "hello-renamed.zip");
    REQUIRE(WriteFile(copy, ReadFile(source)));
    const auto second = ImportPackage(copy, store);
    REQUIRE(second.Ok());
    REQUIRE_EQ(second.Value().file, first.Value().file);
    REQUIRE(!ImportPackage(Join(dir, "missing.zip"), store).Ok());
    REQUIRE(WriteFile(Join(dir, "app.exe"), "x"));
    REQUIRE(!ImportPackage(Join(dir, "app.exe"), store).Ok());
    RemoveDir(dir);
}

TEST(the_entry_is_suggested_from_the_listing) {
    using UltraStore::OS;
    const std::vector<std::string> linuxZip = {"README.txt", "bin/", "bin/hello", "lib/libx.so", "share/icon.png"};
    REQUIRE_EQ(SuggestEntry(linuxZip, OS::Linux), std::string("bin/hello"));
    const std::vector<std::string> windowsZip = {"Hello/", "Hello/Hello.exe", "Hello/uninstall.exe", "Hello/lib.dll"};
    REQUIRE_EQ(SuggestEntry(windowsZip, OS::Windows), std::string("Hello/Hello.exe"));
    const std::vector<std::string> macZip = {"Hello.app/", "Hello.app/Contents/Info.plist", "Hello.app/Contents/MacOS/Hello"};
    REQUIRE_EQ(SuggestEntry(macZip, OS::MacOS), std::string("Hello.app"));
    const std::vector<std::string> appImage = {"Hello-x86_64.AppImage"};
    REQUIRE_EQ(SuggestEntry(appImage, OS::Linux), std::string("Hello-x86_64.AppImage"));
    const std::vector<std::string> single = {"hello", "README"};
    REQUIRE_EQ(SuggestEntry(single, OS::Linux), std::string());   // two candidates: none
    const std::vector<std::string> one = {"hello", "README.md"};
    REQUIRE_EQ(SuggestEntry(one, OS::Linux), std::string("hello"));
    REQUIRE_EQ(SuggestEntry({}, OS::Linux), std::string());
}
