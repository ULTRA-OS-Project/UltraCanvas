// Tests/VirtualFSBridgeArchiveTest.cpp
// UCVFSBridge::CreateArchive and the method of an archive:
//   1. UCVFSCompressionOptions::type is the buffer compressors' choice and
//      does not reach an archive - the defaults (type Zstd) still make a ZIP,
//      which no ZIP writer before libarchive 3.8 could do with Zstandard;
//   2. UCVFSArchiveWriteOptions::method is what a ZIP / 7-Zip is written
//      with: Uncompressed stores a ZIP, BZip2 and LZMA2 make a 7-Zip that
//      reads back;
//   3. a method no archive can be written with (LZX) and one that
//      contradicts the extension (Zstd for a .tar.gz) fail without a file.
//
// Archives are written below the directory given as the first argument
// (ctest passes one in the build tree), else the system temp directory.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#include "UltraCanvasVirtualFSBridge.h"
#include "UltraCanvasPathUtf8.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

static int failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (cond) {                                                         \
            std::printf("  PASS  %s\n", msg);                               \
        } else {                                                            \
            std::printf("  FAIL  %s (line %d)\n", msg, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

// The content of `name` in the archive at `path`, or "<missing>".
static std::string ReadEntry(const fs::path& path, const std::string& name) {
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    std::string content = "<missing>";
    if (archive_read_open_filename(a, PathToUtf8(path).c_str(), 64 * 1024) == ARCHIVE_OK) {
        struct archive_entry* entry = nullptr;
        while (archive_read_next_header(a, &entry) >= ARCHIVE_WARN) {
            if (name != archive_entry_pathname(entry)) {
                archive_read_data_skip(a);
                continue;
            }
            content.clear();
            char buffer[8192];
            la_ssize_t got;
            while ((got = archive_read_data(a, buffer, sizeof(buffer))) > 0)
                content.append(buffer, static_cast<size_t>(got));
            break;
        }
    }
    archive_read_free(a);
    return content;
}

static uintmax_t SizeOf(const fs::path& path) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    return ec ? 0 : size;
}

static bool Pack(const fs::path& archive, const fs::path& file,
                 const UCVFSCompressionOptions& options) {
    std::error_code ec;
    fs::remove(archive, ec);
    return UCVFSBridge::CreateArchive(PathToUtf8(archive), {PathToUtf8(file)}, options);
}

int main(int argc, char** argv) {
    const fs::path root = (argc > 1 ? PathFromUtf8(argv[1]) : fs::temp_directory_path())
                        / "vfs-bridge-archive";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    std::string text;
    for (int i = 0; i < 20000; ++i)
        text += "a line of sample text, number " + std::to_string(i % 500) + "\n";
    const fs::path file = root / "data.txt";
    {
        std::ofstream out(file, std::ios::binary);
        out << text;
    }
    CHECK(UCVFSBridge::Initialize(), "the bridge initialises");

    std::printf("== type is for buffers ==\n");
    {
        const UCVFSCompressionOptions defaults;   // type Zstd, no archive method
        CHECK(defaults.type == UCVFSCompressionType::Zstd && !defaults.archive.method,
              "the defaults: Zstd for buffers, the library's method for archives");
        CHECK(Pack(root / "default.zip", file, defaults) &&
              ReadEntry(root / "default.zip", "data.txt") == text,
              "a ZIP made with the defaults is a readable ZIP");
    }

    std::printf("== archive.method ==\n");
    {
        UCVFSCompressionOptions stored;
        stored.archive.method = UCVFSCompressionType::Uncompressed;
        CHECK(Pack(root / "stored.zip", file, stored) &&
              SizeOf(root / "stored.zip") > text.size() &&
              ReadEntry(root / "stored.zip", "data.txt") == text,
              "Uncompressed stores a ZIP");
        for (UCVFSCompressionType method : {UCVFSCompressionType::BZip2,
                                            UCVFSCompressionType::LZMA2}) {
            UCVFSCompressionOptions options;
            options.archive.method = method;
            const fs::path archive = root / (method == UCVFSCompressionType::BZip2
                                             ? "bzip2.7z" : "lzma2.7z");
            const std::string what = std::string("a 7-Zip with ") +
                    (method == UCVFSCompressionType::BZip2 ? "BZip2" : "LZMA2") +
                    " packs, compresses and reads back";
            CHECK(Pack(archive, file, options) && SizeOf(archive) < text.size() &&
                  ReadEntry(archive, "data.txt") == text, what.c_str());
        }
    }

    std::printf("== methods no archive takes ==\n");
    {
        UCVFSCompressionOptions cabinet;
        cabinet.archive.method = UCVFSCompressionType::LZX;
        CHECK(!Pack(root / "lzx.7z", file, cabinet) && !fs::exists(root / "lzx.7z"),
              "LZX is refused without a file");
        CHECK(!UCVFSBridge::GetLastError().empty(), "... and says why");
        UCVFSCompressionOptions wrong;
        wrong.archive.method = UCVFSCompressionType::Zstd;
        CHECK(!Pack(root / "wrong.tar.gz", file, wrong) && !fs::exists(root / "wrong.tar.gz"),
              "Zstd for a .tar.gz is refused without a file");
    }

    UCVFSBridge::Shutdown();
    fs::remove_all(root, ec);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
