// Tests/UltraStoreAdmin/test_packages.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "PackageStore.h"

#include "UltraCanvasImage.h"
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

namespace {
// A PNG of the size, a flat colour with a darker square in it, written
// through the image library so the test needs no fixture file.
bool WritePng(const std::string& pathUtf8, int width, int height) {
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t* p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            const bool inner = x > width / 4 && x < width * 3 / 4 && y > height / 4 && y < height * 3 / 4;
            p[0] = inner ? 37 : 219; p[1] = inner ? 99 : 234; p[2] = inner ? 235 : 254; p[3] = 255;
        }
    }
    static const bool started = UltraCanvas::UCImageRaster::InitializeImageSubsysterm("UltraStoreAdminCoreTests");
    if (!started) return false;
    auto image = UltraCanvas::UCImageRaster::FromRgba(rgba.data(), width, height);
    if (!image) return false;
    UltraCanvas::UCImageSave::ImageExportOptions options;
    options.format = UltraCanvas::UCImageSaveFormat::PNG;
    return image->Save(pathUtf8, options).empty();
}
} // namespace

TEST(a_picture_is_imported_with_the_web_stores_sizes) {
    const std::string dir = TempDir("variants");
    REQUIRE(WritePng(Join(dir, "icon.png"), 512, 512));
    REQUIRE(WritePng(Join(dir, "shot.png"), 1400, 900));
    REQUIRE(WritePng(Join(dir, "small.png"), 400, 300));
    const std::string store = Join(dir, "store");

    const auto icon = ImportMediaVariants(Join(dir, "icon.png"), store, "org.ultraos.hello", MediaKind::Icon);
    REQUIRE(icon.Ok());
    REQUIRE_EQ(icon.Value().original, std::string("media/org.ultraos.hello/icon.png"));
    // WebP every image library writes; AVIF where there is an encoder - then
    // it is warned about, never failed on.
    std::vector<int> webpWidths, avifWidths;
    for (const UltraStore::MediaVariant& v : icon.Value().variants) {
        REQUIRE(std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(Join(store, v.url))));
        REQUIRE(v.url.rfind("media/org.ultraos.hello/icon-", 0) == 0);
        (v.format == "webp" ? webpWidths : avifWidths).push_back(v.width);
    }
    REQUIRE(webpWidths == (std::vector<int>{64, 128, 256}));
    bool avifWarned = false;
    for (const std::string& w : icon.Value().warnings) if (w.find("no avif") != std::string::npos) avifWarned = true;
    REQUIRE(avifWidths.empty() ? avifWarned : avifWidths == (std::vector<int>{64, 128, 256}));
    // A written icon of a flat picture is well inside its budget.
    for (const UltraStore::MediaVariant& v : icon.Value().variants) {
        REQUIRE(std::filesystem::file_size(UltraCanvas::PathFromUtf8(Join(store, v.url))) <= MediaBudget(MediaKind::Icon, v.width));
    }

    const auto shot = ImportMediaVariants(Join(dir, "shot.png"), store, "org.ultraos.hello", MediaKind::Screenshot);
    REQUIRE(shot.Ok());
    webpWidths.clear();
    for (const UltraStore::MediaVariant& v : shot.Value().variants) if (v.format == "webp") webpWidths.push_back(v.width);
    REQUIRE(webpWidths == (std::vector<int>{560, 1120}));
    // Never above the original: a 400 px picture gives 400 px once.
    const auto small = ImportMediaVariants(Join(dir, "small.png"), store, "org.ultraos.hello", MediaKind::Screenshot);
    REQUIRE(small.Ok());
    webpWidths.clear();
    for (const UltraStore::MediaVariant& v : small.Value().variants) if (v.format == "webp") webpWidths.push_back(v.width);
    REQUIRE(webpWidths == (std::vector<int>{400}));
    // The budgets: 12 KB at 256, 120 KB at 1120, less for a smaller size.
    REQUIRE_EQ(MediaBudget(MediaKind::Icon, 256), size_t(12 * 1024));
    REQUIRE_EQ(MediaBudget(MediaKind::Screenshot, 1120), size_t(120 * 1024));
    REQUIRE(MediaBudget(MediaKind::Icon, 64) < MediaBudget(MediaKind::Icon, 128));
    // Something that is not a picture: the original alone, with a warning.
    REQUIRE(WriteFile(Join(dir, "text.png"), "not a picture"));
    const auto text = ImportMediaVariants(Join(dir, "text.png"), store, "org.ultraos.hello", MediaKind::Icon);
    REQUIRE(text.Ok());
    REQUIRE(text.Value().variants.empty());
    REQUIRE_EQ(text.Value().warnings.size(), size_t(1));
    RemoveDir(dir);
}
