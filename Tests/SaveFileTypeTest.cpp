// Tests/SaveFileTypeTest.cpp
// Saving a file: the type chosen in a save dialog decides the name, and a
// failed image save leaves nothing behind.
//
// 1. "Files of type" used to mean nothing. Every save dialog - the framework's
//    and the platforms' - returned the name as typed, so "photo" with JPEG
//    picked came back as "photo", and UltraViewer offered an SVG as
//    "diagram.svg" under "PNG image", which libvips cannot write. The rules
//    the dialogs now share (FileNameExtension, FileFilterIndexForName,
//    FileNameForFileType, FileNameWithTypeExtension) are checked here, and -
//    when there is a display - the framework dialog that applies them.
// 2. A libvips writer opens - and truncates - its file before it has encoded a
//    byte, so a save that failed left an empty file, and the same failure
//    saving over an image destroyed it (AVIF on a libheif without an AV1
//    encoder did exactly that). PixelFX::FileIO's savers now write through
//    WriteFileAtomically; a source that fails to decode halfway through the
//    save stands in for the encoder, because it fails the same way on every
//    machine.
// The dialog section skips without a DISPLAY; the rest needs none.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextInput.h"
#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

#define EXPECT_EQ(name, actual, expected)                                     \
    do {                                                                      \
        const std::string got = (actual);                                     \
        const std::string want = (expected);                                  \
        TEST(name, got == want);                                              \
        if (got != want) {                                                    \
            std::cerr << "      got \"" << got << "\", want \"" << want       \
                      << "\"" << std::endl;                                   \
        }                                                                     \
    } while (0)

namespace {

const std::vector<FileFilter> kImageTypes = {
    FileFilter("PNG image", std::vector<std::string>{ "png" }),
    FileFilter("JPEG image", std::vector<std::string>{ "jpg", "jpeg" }),
    FileFilter("WebP image", std::vector<std::string>{ "webp" }),
    FileFilter("All files", std::vector<std::string>{ "*" }),
};
const FileFilter& kPng = kImageTypes[0];
const FileFilter& kJpeg = kImageTypes[1];
const FileFilter& kAll = kImageTypes[3];

void TestFileNameRules() {
    EXPECT_EQ("extension is lower case", FileNameExtension("photo.JPG"), "jpg");
    EXPECT_EQ("a dot in a folder is not the file's", FileNameExtension("/a.b/name"), "");
    EXPECT_EQ("a Windows folder's dot neither", FileNameExtension("C:\\x.y\\file"), "");
    EXPECT_EQ("a leading dot starts no extension", FileNameExtension(".profile"), "");
    EXPECT_EQ("a trailing dot is no extension", FileNameExtension("photo."), "");

    TEST("starts on the type of the name offered",
         FileFilterIndexForName(kImageTypes, "photo.jpeg") == 1);
    TEST("keeps the first type for a name it describes",
         FileFilterIndexForName(kImageTypes, "photo.png") == 0);
    TEST("keeps the first type for a name without an extension",
         FileFilterIndexForName(kImageTypes, "photo") == 0);
    TEST("keeps the first type for an extension no type lists",
         FileFilterIndexForName(kImageTypes, "diagram.svg") == 0);
    const std::vector<FileFilter> editorTypes = {
        FileFilter("All Files", "*"),
        FileFilter("Text Files", std::vector<std::string>{ "txt", "md" }),
    };
    TEST("an editor that starts on All Files stays there",
         FileFilterIndexForName(editorTypes, "notes.md") == 0);

    EXPECT_EQ("picking JPEG renames a PNG",
              FileNameForFileType("photo.png", kJpeg, kImageTypes), "photo.jpg");
    EXPECT_EQ("picking JPEG keeps .jpeg",
              FileNameForFileType("photo.jpeg", kJpeg, kImageTypes), "photo.jpeg");
    EXPECT_EQ("picking JPEG gives a bare name .jpg",
              FileNameForFileType("photo", kJpeg, kImageTypes), "photo.jpg");
    EXPECT_EQ("an extension no type lists is kept, the type's added",
              FileNameForFileType("notes.v2", kPng, kImageTypes), "notes.v2.png");
    EXPECT_EQ("a trailing dot is not doubled",
              FileNameForFileType("photo.", kPng, kImageTypes), "photo.png");
    EXPECT_EQ("All files leaves the name alone",
              FileNameForFileType("photo.png", kAll, kImageTypes), "photo.png");
    EXPECT_EQ("an empty name stays empty",
              FileNameForFileType("", kPng, kImageTypes), "");

    EXPECT_EQ("a name typed without an extension gets the chosen type's",
              FileNameWithTypeExtension("/tmp/photo", kJpeg, kImageTypes), "/tmp/photo.jpg");
    EXPECT_EQ("an extension of a type offered was typed on purpose",
              FileNameWithTypeExtension("/tmp/photo.png", kJpeg, kImageTypes), "/tmp/photo.png");
    EXPECT_EQ("an extension none of the types offers gets the chosen type's",
              FileNameWithTypeExtension("/tmp/diagram.svg", kPng, kImageTypes),
              "/tmp/diagram.svg.png");
    EXPECT_EQ("a dot in the folder does not count",
              FileNameWithTypeExtension("/tmp/dir.d/photo", kPng, kImageTypes),
              "/tmp/dir.d/photo.png");
    EXPECT_EQ("All files saves the name as typed",
              FileNameWithTypeExtension("/tmp/Makefile", kAll, kImageTypes), "/tmp/Makefile");
}

// The framework's own save dialog, driven the way a user drives it: it starts
// on the type of the name offered, renames the file when another type is
// picked, and OK saves a bare name as the chosen type.
void TestFrameworkSaveDialog(const fs::path& dir) {
    FileDialogConfig config;
    config.title = "Save test";
    config.dialogType = FileDialogType::Save;
    config.initialDirectory = PathToUtf8(dir);
    config.defaultFileName = "photo.jpg";
    config.filters = kImageTypes;
    auto dialog = UltraCanvasDialogManager::CreateFileDialog(config);
    auto result = std::make_shared<DialogResult>(DialogResult::NoResult);
    UltraCanvasDialogManager::ShowDialog(
            dialog, [result](DialogResult r) { *result = r; }, nullptr);

    TEST("the dialog starts on the type of the name offered",
         dialog->GetSelectedFilterIndex() == 1);
    auto* name = dynamic_cast<UltraCanvasTextInput*>(dialog->FindChildById("FileDialogName"));
    auto* type = dynamic_cast<UltraCanvasDropdown*>(dialog->FindChildById("FileDialogType"));
    TEST("the dialog has its name field and type list", name && type);
    if (!name || !type) return;
    EXPECT_EQ("the name field shows the name offered", name->GetText(), "photo.jpg");

    type->SetSelectedIndex(0, true);
    EXPECT_EQ("picking PNG renames the file", name->GetText(), "photo.png");
    name->SetText("shot");
    type->SetSelectedIndex(2, true);
    EXPECT_EQ("picking WebP gives a bare name .webp", name->GetText(), "shot.webp");

    // Typed again after picking the type: OK applies it.
    name->SetText("shot");
    if (name->onEnterPressed) name->onEnterPressed(name->GetText());
    TEST("OK closes the dialog", *result == DialogResult::OK);
    EXPECT_EQ("and returns the name as the chosen type",
              dialog->GetSelectedFilePath(), PathToUtf8(dir / "shot.webp"));
}

#ifdef HAS_LIBVIPS
std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Anything WriteFileAtomically staged and did not clean up.
bool HasStagedLeftovers(const fs::path& dir) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (PathToUtf8(entry.path().filename()).rfind(".ucsave", 0) == 0) return true;
    }
    return false;
}

template <typename Save>
bool Throws(Save save) {
    try {
        save();
    } catch (const PixelFX::PixelFXException&) {
        return true;
    }
    return false;
}

void TestStagedImageSave(const char* programName) {
    if (VIPS_INIT(programName) != 0) {
        TEST("libvips starts", false);
        return;
    }
    // A Thai folder name, so the staged write is exercised on a name outside
    // ASCII as well.
    const fs::path dir = fs::temp_directory_path() /
                         PathFromUtf8("SaveFileTypeTest-\xE0\xB8\x97\xE0\xB8\x94\xE0\xB8\xAA\xE0\xB8\xAD\xE0\xB8\x9A");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const PixelFX::PFXImage small(vips::VImage::black(8, 8, vips::VImage::option()->set("bands", 3)));
    const fs::path good = dir / "good.png";
    TEST("a save that works writes the file",
         PixelFX::FileIO::Save(small, PathToUtf8(good)) && fs::file_size(good) > 0);
    TEST("and leaves nothing staged behind", !HasStagedLeftovers(dir));

    // A source that fails to decode halfway through the save: the PNG is cut
    // short and read strictly, so the writer has opened its file by the time
    // the error comes.
    const fs::path whole = dir / "whole.png";
    vips::VImage noise = vips::VImage::gaussnoise(1200, 1200).cast(VIPS_FORMAT_UCHAR);
    noise.write_to_file(PathToUtf8(whole).c_str());
    const fs::path cut = dir / "cut.png";
    fs::copy_file(whole, cut, fs::copy_options::overwrite_existing);
    fs::resize_file(cut, fs::file_size(whole) / 2);
#if VIPS_MAJOR_VERSION > 8 || (VIPS_MAJOR_VERSION == 8 && VIPS_MINOR_VERSION >= 12)
    vips::VOption* strict = vips::VImage::option()->set("fail_on", VIPS_FAIL_ON_TRUNCATED);
#else
    vips::VOption* strict = vips::VImage::option()->set("fail", true);
#endif
    const PixelFX::PFXImage broken(vips::VImage::new_from_file(PathToUtf8(cut).c_str(), strict));

    const fs::path existing = dir / "existing.png";
    { std::ofstream(existing, std::ios::binary) << "ORIGINAL"; }
    TEST("a save that fails throws",
         Throws([&] { PixelFX::FileIO::Save(broken, PathToUtf8(existing)); }));
    TEST("and keeps the file it was saving over",
         ReadAll(existing) == "ORIGINAL");

    const fs::path fresh = dir / "fresh.png";
    TEST("a failed save to a new name throws",
         Throws([&] { PixelFX::FileIO::SavePng(broken, PathToUtf8(fresh)); }));
    TEST("and leaves no file", !fs::exists(fresh));

    const fs::path drawing = dir / "drawing.svg";
    TEST("a format libvips cannot write (SVG) throws",
         Throws([&] { PixelFX::FileIO::Save(small, PathToUtf8(drawing)); }));
    TEST("and leaves no file", !fs::exists(drawing));

    // AVIF needs libheif with an AV1 encoder, which not every build has. Where
    // it is missing the save must fail cleanly; where it is there, it works.
    const fs::path avif = dir / "photo.avif";
    const bool avifFailed = Throws([&] { PixelFX::FileIO::SaveAvif(small, PathToUtf8(avif)); });
    TEST("an AVIF save leaves a whole file or none",
         avifFailed ? !fs::exists(avif) : fs::file_size(avif) > 0);
    TEST("no failed save left anything staged", !HasStagedLeftovers(dir));

    fs::remove_all(dir, ec);
}
#endif

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    TestFileNameRules();

    if (std::getenv("DISPLAY")) {
        UltraCanvasApplication app;
        if (app.Initialize("SaveFileTypeTest")) {
            std::error_code ec;
            const fs::path dir = fs::temp_directory_path() / "SaveFileTypeTest-dialog";
            fs::create_directories(dir, ec);
            TestFrameworkSaveDialog(dir);
            fs::remove_all(dir, ec);
        } else {
            std::cerr << "SKIP: save dialog (application would not initialise)" << std::endl;
        }
    } else {
        std::cerr << "SKIP: save dialog (no DISPLAY)" << std::endl;
    }
#ifdef HAS_LIBVIPS
    TestStagedImageSave(argv[0]);
#else
    (void)argv;
    std::cerr << "SKIP: staged image save (built without libvips)" << std::endl;
#endif
    std::cerr << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
