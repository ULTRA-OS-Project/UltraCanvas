// Tests/ZipPackageTest.cpp
// UCZipPackageReader / UCZipPackageWriter on a file whose name is Thai and an
// emoji: written, read back, released on Close().
//
// The rule this guards: a ZIP package opens by its UTF-8 name on every
// platform (OpenFileUtf8), never through miniz's own fopen - which converts
// the name only under MSVC and 64-bit MinGW, and there opens with _wfopen_s,
// which denies every other program write access while the archive is open.
// So on Windows the test also opens the file for writing while the reader
// holds it: that failed with miniz's open. Windows CI compiles this file by
// hand (no BUILD_TESTS there) on a code page 1252 runner, which has neither
// Thai nor emoji.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasZipPackage.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

const std::string kMimetype = "application/vnd.oasis.opendocument.text";
const std::string kContent(5000, 'x');

bool WritePackage(const std::string& path) {
    UCZipPackageWriter writer;
    return writer.Open(path) &&
           writer.AddEntry("mimetype", kMimetype, /*compress=*/false) &&
           writer.AddEntry("content.xml", kContent) &&
           writer.Finalize();
}

} // namespace

int main() {
    std::cout << "=== UCZipPackage on a Thai-and-emoji path ===\n";
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
        PathFromUtf8("\xE0\xB8\x8B\xE0\xB8\xB4\xE0\xB8\x9B \xF0\x9F\x93\xA6 zip-test");
    fs::create_directories(dir, ec);
    const std::string package = PathToUtf8(
        dir / PathFromUtf8("\xE0\xB9\x80\xE0\xB8\xAD\xE0\xB8\x81\xE0\xB8\xAA\xE0\xB8\xB2\xE0\xB8\xA3 "
                           "\xF0\x9F\x93\x84.odt"));

    Check(WritePackage(package), "the writer creates the package");
    Check(fs::exists(PathFromUtf8(package), ec), "the package is on disk under its name");

    UCZipPackageReader reader;
    Check(reader.Open(package), "the reader opens it by its UTF-8 name");
    Check(reader.IsOpen(), "the reader is open");
    Check(reader.EntryNames().size() == 2, "both entries are listed");
    Check(reader.HasEntry("content.xml"), "content.xml is found");
    std::string text;
    Check(reader.ReadEntry("mimetype", text) && text == kMimetype,
          "the stored entry reads back");
    Check(reader.ReadEntry("content.xml", text) && text == kContent,
          "the compressed entry reads back");

#if defined(_WIN32) || defined(_WIN64)
    // Another program writing the file while the reader holds it - what
    // _wfopen_s, miniz's own open, refused.
    std::FILE* other = OpenFileUtf8(package, "r+b");
    Check(other != nullptr, "another writer can open the file while it is read");
    if (other) std::fclose(other);
#endif

    reader.Close();
    Check(!reader.IsOpen(), "Close() closes it");
    Check(fs::remove(PathFromUtf8(package), ec),
          "the file can be deleted after Close() (the reader let it go)");

    // Reopening with the same reader closes the first file.
    const std::string first = PathToUtf8(dir / "first.zip");
    const std::string second = PathToUtf8(dir / "second.zip");
    Check(WritePackage(first) && WritePackage(second), "two packages written");
    Check(reader.Open(first) && reader.Open(second), "one reader opens one after the other");
    Check(fs::remove(PathFromUtf8(first), ec), "opening the second released the first");
    reader.Close();

    std::cout << "\n=== What does not open ===\n";
    Check(!reader.Open(PathToUtf8(dir / "missing.zip")), "a missing file does not open");
    Check(reader.GetLastError().find("Cannot open") != std::string::npos,
          "and says it could not be opened");
    Check(!reader.IsOpen(), "and leaves the reader closed");
    const std::string junk = PathToUtf8(dir / "junk.zip");
    if (std::FILE* f = OpenFileUtf8(junk, "wb")) {
        std::fputs("PK but not an archive", f);
        std::fclose(f);
    }
    Check(!reader.Open(junk), "a file that is not a ZIP does not open");
    Check(reader.GetLastError().find("Not a valid ZIP") != std::string::npos,
          "and says it is not a ZIP archive");
    Check(fs::remove(PathFromUtf8(junk), ec), "and is not held open after the failure");
    Check(reader.Open(second) && reader.ReadEntry("mimetype", text) && text == kMimetype,
          "the reader still opens a good file after a failure");
    reader.Close();

    fs::remove_all(dir, ec);
    std::cout << "\n"
              << (g_failures == 0 ? "All checks passed\n"
                                  : std::to_string(g_failures) + " check(s) FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
