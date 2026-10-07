// Tests/EBookArchiveTest.cpp
// Unit tests for EBookArchive (ZIP access + DEFLATE helpers on miniz).
// Version: 1.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "EBookArchive.h"

#include "UltraCanvasPathUtf8.h"
#include "miniz.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using UltraCanvas::EBookArchive;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

// Build an in-memory ZIP with miniz's writer.
static std::vector<uint8_t> MakeTestZip() {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    mz_zip_writer_init_heap(&zip, 0, 0);

    const char* mimetype = "application/epub+zip";
    mz_zip_writer_add_mem(&zip, "mimetype", mimetype, std::strlen(mimetype),
                          MZ_NO_COMPRESSION);

    std::string chapter = "<html><body><p>Hello archive</p></body></html>";
    mz_zip_writer_add_mem(&zip, "OEBPS/chapter1.xhtml", chapter.data(),
                          chapter.size(), MZ_BEST_COMPRESSION);

    void* buffer = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    std::vector<uint8_t> result(static_cast<uint8_t*>(buffer),
                                static_cast<uint8_t*>(buffer) + size);
    mz_zip_writer_end(&zip);
    mz_free(buffer);
    return result;
}

static void TestZipReading() {
    std::vector<uint8_t> zipData = MakeTestZip();
    CHECK(EBookArchive::IsZipData(zipData.data(), zipData.size()));

    EBookArchive archive;
    CHECK(archive.OpenFromMemory(zipData));
    CHECK(archive.IsOpen());

    auto names = archive.FileNames();
    CHECK(names.size() == 2);

    CHECK(archive.Contains("mimetype"));
    CHECK(archive.Contains("OEBPS/chapter1.xhtml"));
    CHECK(archive.Contains("oebps/CHAPTER1.xhtml"));   // case-insensitive fallback
    CHECK(!archive.Contains("missing.txt"));

    CHECK(archive.ReadTextFile("mimetype") == "application/epub+zip");
    std::string chapter = archive.ReadTextFile("OEBPS/chapter1.xhtml");
    CHECK(chapter.find("Hello archive") != std::string::npos);

    archive.Close();
    CHECK(!archive.IsOpen());
}

// The same archive read from a file: only the entries asked for are read,
// the name is UTF-8 (Thai and an emoji here), and Close() lets the file go.
static void TestZipFromFile() {
    std::vector<uint8_t> zipData = MakeTestZip();
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        UltraCanvas::PathFromUtf8("\xE0\xB8\x8B\xE0\xB8\xB4\xE0\xB8\x9B "
                                  "\xF0\x9F\x93\xA6 archive-test.zip");
    std::FILE* f = UltraCanvas::OpenFileUtf8(UltraCanvas::PathToUtf8(path), "wb");
    CHECK(f != nullptr);
    if (!f) return;
    std::fwrite(zipData.data(), 1, zipData.size(), f);
    std::fclose(f);

    EBookArchive archive;
    CHECK(archive.OpenFromFile(UltraCanvas::PathToUtf8(path)));
    CHECK(archive.IsOpen());
    CHECK(archive.FileNames().size() == 2);
    CHECK(archive.ReadTextFile("mimetype") == "application/epub+zip");
    CHECK(archive.ReadTextFile("oebps/CHAPTER1.xhtml").find("Hello archive") !=
          std::string::npos);
    CHECK(archive.FileSize("OEBPS/chapter1.xhtml") ==
          std::strlen("<html><body><p>Hello archive</p></body></html>"));
    CHECK(archive.FileSize("missing.txt") == 0);

    // Reopening from memory replaces the file-backed archive cleanly.
    CHECK(archive.OpenFromMemory(zipData));
    CHECK(archive.ReadTextFile("mimetype") == "application/epub+zip");
    archive.Close();
    CHECK(!archive.IsOpen());

    std::error_code ec;
    CHECK(std::filesystem::remove(path, ec));

    // A file that is not a ZIP, and one that does not exist.
    const std::filesystem::path junkPath =
        std::filesystem::temp_directory_path() / "ebook-archive-junk.zip";
    f = UltraCanvas::OpenFileUtf8(UltraCanvas::PathToUtf8(junkPath), "wb");
    if (f) {
        std::fputs("PK but not really an archive", f);
        std::fclose(f);
    }
    CHECK(!archive.OpenFromFile(UltraCanvas::PathToUtf8(junkPath)));
    CHECK(!archive.IsOpen());
    CHECK(!archive.GetLastError().empty());
    std::filesystem::remove(junkPath, ec);
    CHECK(!archive.OpenFromFile(UltraCanvas::PathToUtf8(junkPath)));
}

static void TestNotAZip() {
    EBookArchive archive;
    std::vector<uint8_t> junk = {'n', 'o', 't', 'a', 'z', 'i', 'p'};
    CHECK(!archive.OpenFromMemory(junk));
    CHECK(!archive.IsOpen());
    CHECK(!archive.GetLastError().empty());
}

static void TestInflate() {
    std::string original(4000, 'x');
    for (size_t i = 0; i < original.size(); i += 7) original[i] = 'y';

    // Compress with miniz's zlib-style compressor.
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(original.size()));
    std::vector<uint8_t> compressed(bound);
    mz_ulong compressedLen = bound;
    int rc = mz_compress(compressed.data(), &compressedLen,
                         reinterpret_cast<const unsigned char*>(original.data()),
                         static_cast<mz_ulong>(original.size()));
    CHECK(rc == MZ_OK);
    compressed.resize(compressedLen);

    std::vector<uint8_t> restored;
    CHECK(EBookArchive::InflateZlib(compressed.data(), compressed.size(), restored));
    CHECK(restored.size() == original.size());
    CHECK(std::memcmp(restored.data(), original.data(), original.size()) == 0);

    std::vector<uint8_t> bad = {0x01, 0x02, 0x03};
    std::vector<uint8_t> out;
    CHECK(!EBookArchive::InflateZlib(bad.data(), bad.size(), out));
}

int main() {
    TestZipReading();
    TestZipFromFile();
    TestNotAZip();
    TestInflate();

    std::printf("%s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
