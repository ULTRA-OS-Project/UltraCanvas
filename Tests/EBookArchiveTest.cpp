// Tests/EBookArchiveTest.cpp
// Unit tests for EBookArchive (ZIP access + DEFLATE helpers on miniz).
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "EBookArchive.h"

#include "miniz.h"

#include <cstdio>
#include <cstring>
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
    TestNotAZip();
    TestInflate();

    std::printf("%s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
