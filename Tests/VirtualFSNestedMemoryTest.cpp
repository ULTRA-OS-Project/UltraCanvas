// Tests/VirtualFSNestedMemoryTest.cpp
// Regression test for memory-backed nested archive traversal (VirtualFS).
//
// Reading a file out of an archive-inside-an-archive used to extract the
// inner archive to a temp file first, because providers could only open a
// real path. Every nested traversal therefore left the inner archive's
// decompressed bytes sitting in the temp directory - including archives
// decrypted from a password-protected parent.
//
// IVirtualFSProvider::OpenFromMemory() lets a provider take the bytes
// directly (libarchive's archive_read_open_memory). This test verifies:
//   1. Correctness: nested reads return byte-identical content, and
//      traversal still works two archives deep.
//   2. No spill: the manager's temp directory stays empty across the whole
//      traversal, so the inner archive never touches the disk.
//   3. Listing and existence checks work through the memory-backed provider,
//      not just single reads.
// Version: 1.0.0
// Last Modified: 2026-08-31
// Author: UltraCanvas Framework

#include "VirtualFS/VirtualFS.h"
#include "VirtualFSLibArchiveProvider.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace VirtualFS;

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

// Deterministic payload so nested reads can be verified byte-for-byte.
static std::vector<uint8_t> PayloadFor(const std::string& name) {
    std::vector<uint8_t> data;
    data.reserve(512);
    for (size_t i = 0; i < 512; ++i) {
        data.push_back(static_cast<uint8_t>(name[i % name.size()] + i));
    }
    return data;
}

// Builds a ZIP holding the given virtual-path -> payload entries.
static bool BuildArchive(const std::string& archivePath,
                         const std::vector<std::string>& entries) {
    VirtualFSLibArchiveProvider writer;
    if (writer.CreateArchive(archivePath) != VirtualFSResult::Success) {
        return false;
    }
    for (const auto& e : entries) {
        if (writer.AddFromMemory(e, PayloadFor(e)) != VirtualFSResult::Success) {
            return false;
        }
    }
    return writer.Finalize() == VirtualFSResult::Success;
}

// Wraps an existing file as a single entry inside a new ZIP.
static bool WrapFileInArchive(const std::string& outerPath,
                              const std::string& innerName,
                              const std::string& innerFile) {
    VirtualFSLibArchiveProvider writer;
    if (writer.CreateArchive(outerPath) != VirtualFSResult::Success) {
        return false;
    }
    if (writer.AddFile(innerFile, innerName) != VirtualFSResult::Success) {
        return false;
    }
    return writer.Finalize() == VirtualFSResult::Success;
}

static size_t CountFiles(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec);
         !ec && it != fs::directory_iterator(); ++it) {
        ++n;
    }
    return n;
}

int main(int argc, char** argv) {
    const fs::path outDir = (argc > 1) ? fs::path(argv[1])
                                       : fs::path("vfsnested-test-out");
    const fs::path tempDir = outDir / "temp";

    std::error_code ec;
    fs::remove_all(outDir, ec);
    fs::create_directories(tempDir, ec);

    std::printf("VirtualFS nested memory traversal test\n");
    std::printf("  work dir: %s\n\n", outDir.string().c_str());

    // --- Build inner.zip, then nest it inside outer.zip ---------------------
    const std::string innerPath = (outDir / "inner.zip").string();
    const std::string outerPath = (outDir / "outer.zip").string();

    const std::vector<std::string> innerEntries = {
        "docs/readme.txt", "docs/notes.txt", "src/main.cpp"
    };

    if (!BuildArchive(innerPath, innerEntries)) {
        std::printf("  FAIL  could not build inner.zip\n");
        return 1;
    }
    if (!WrapFileInArchive(outerPath, "bundle/inner.zip", innerPath)) {
        std::printf("  FAIL  could not build outer.zip\n");
        return 1;
    }

    VirtualFS_Initialize();
    VirtualFS_SetTempDirectory(tempDir.string());

    // Temp dir must be empty before we traverse anything.
    CHECK(CountFiles(tempDir) == 0, "temp directory starts empty");

    // --- 1. Nested read returns correct bytes ------------------------------
    const std::string nested = outerPath + "/bundle/inner.zip/docs/readme.txt";

    std::vector<uint8_t> got;
    const VirtualFSResult r = VirtualFS_ReadFile(nested, got);
    CHECK(r == VirtualFSResult::Success, "nested read succeeds");
    CHECK(got == PayloadFor("docs/readme.txt"), "nested read is byte-identical");

    // --- 2. The inner archive never spilled to disk ------------------------
    const size_t spilled = CountFiles(tempDir);
    if (spilled != 0) {
        std::printf("  (temp dir holds %zu file(s) after traversal)\n", spilled);
    }
    CHECK(spilled == 0, "no temp file written for the nested archive");

    // --- 3. Listing and existence work through the memory-backed provider --
    auto entries = VirtualFS_ListDirectory(outerPath + "/bundle/inner.zip/docs");
    CHECK(entries.size() == 2, "nested directory lists both files");

    CHECK(VirtualFS_Exists(outerPath + "/bundle/inner.zip/src/main.cpp"),
          "nested existence check succeeds");
    CHECK(!VirtualFS_Exists(outerPath + "/bundle/inner.zip/src/absent.cpp"),
          "absent nested entry reports missing");

    // A second read of a different entry exercises the cached provider.
    std::vector<uint8_t> got2;
    CHECK(VirtualFS_ReadFile(outerPath + "/bundle/inner.zip/src/main.cpp", got2)
              == VirtualFSResult::Success,
          "second nested read succeeds from the cached provider");
    CHECK(got2 == PayloadFor("src/main.cpp"),
          "second nested read is byte-identical");

    CHECK(CountFiles(tempDir) == 0, "temp directory still empty at the end");

    VirtualFS_Shutdown();

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "ALL PASSED" : "FAILURES",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
