// Tests/VirtualFSDeleteTest.cpp
// Regression test for bulk entry deletion from archives (VirtualFS).
//
// Deleting entries from an archive requires rewriting the whole file. The
// batched DeleteEntries() API must rewrite the archive exactly ONCE for the
// whole selection - deleting a 19,000-file folder entry-by-entry (one rewrite
// per file, like MS Windows Explorer does) is O(N * archive size) and takes
// practically forever. This test verifies:
//   1. Correctness: requested files and directory subtrees disappear,
//      everything else survives byte-identical.
//   2. Single-pass behaviour: the progress callback observes each archive
//      entry at most once per delete call.
//   3. The ZIP raw-copy fast path and the generic libarchive rewrite
//      (.tar.gz) both work, as does the VirtualFSManager batch entry point.
// Version: 1.0.0
// Last Modified: 2026-07-25
// Author: UltraCanvas Framework

#include "VirtualFS/VirtualFS.h"
#include "VirtualFSLibArchiveProvider.h"

#include <cstdio>
#include <cstring>
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

// One deterministic payload per entry so survivors can be verified
// byte-for-byte after the rewrite.
static std::vector<uint8_t> PayloadFor(const std::string& name) {
    std::vector<uint8_t> data;
    data.reserve(64 + name.size());
    for (size_t i = 0; i < 64; ++i) {
        data.push_back(static_cast<uint8_t>((i * 7 + name.size() * 13) & 0xFF));
    }
    data.insert(data.end(), name.begin(), name.end());
    return data;
}

// Builds an archive with `bulkCount` files under "media/", three files under
// "docs/" and two root-level files.
static bool BuildArchive(const std::string& archivePath, size_t bulkCount,
                         std::vector<std::string>& outAllPaths) {
    VirtualFSLibArchiveProvider writer;
    if (writer.CreateArchive(archivePath) != VirtualFSResult::Success) {
        std::printf("  could not create %s: %s\n", archivePath.c_str(),
                    writer.GetLastError().c_str());
        return false;
    }

    outAllPaths.clear();
    char nameBuf[64];
    for (size_t i = 0; i < bulkCount; ++i) {
        std::snprintf(nameBuf, sizeof(nameBuf), "media/file_%04zu.bin", i);
        outAllPaths.push_back(nameBuf);
    }
    outAllPaths.push_back("docs/readme.txt");
    outAllPaths.push_back("docs/manual.txt");
    outAllPaths.push_back("docs/license.txt");
    outAllPaths.push_back("keep_root.txt");
    outAllPaths.push_back("also_kept.dat");

    for (const std::string& p : outAllPaths) {
        if (writer.AddFromMemory(p, PayloadFor(p)) != VirtualFSResult::Success) {
            std::printf("  could not add %s: %s\n", p.c_str(),
                        writer.GetLastError().c_str());
            return false;
        }
    }
    return writer.Finalize() == VirtualFSResult::Success;
}

static bool EntryContentIntact(VirtualFSLibArchiveProvider& p, const std::string& path) {
    std::vector<uint8_t> data;
    if (p.ReadFile(path, data) != VirtualFSResult::Success) return false;
    return data == PayloadFor(path);
}

// Runs the shared delete scenario against one archive file. `label` names the
// format in the output; the format is chosen by the file extension.
static void RunDeleteScenario(const std::string& archivePath, const char* label,
                              size_t bulkCount) {
    std::printf("── %s ──\n", label);

    std::vector<std::string> allPaths;
    CHECK(BuildArchive(archivePath, bulkCount, allPaths), "archive created");

    VirtualFSLibArchiveProvider p;
    CHECK(p.Open(archivePath) == VirtualFSResult::Success, "archive opens");
    const size_t entriesBefore = p.GetArchiveInfo().entryCount;
    CHECK(entriesBefore == allPaths.size(), "all entries present before delete");

    // Single batched call: the whole "media" subtree + one named file.
    size_t callbackCalls = 0;
    size_t maxFilesProcessed = 0;
    auto progress = [&](const VirtualFSProgress& pr) {
        ++callbackCalls;
        if (pr.filesProcessed > maxFilesProcessed) {
            maxFilesProcessed = pr.filesProcessed;
        }
        return true;
    };

    auto result = p.DeleteEntries({"media", "docs/manual.txt"}, progress);
    CHECK(result == VirtualFSResult::Success, "batched delete succeeds");

    // Correctness: deleted entries gone, survivors intact.
    CHECK(!p.Exists("media/file_0000.bin"), "first bulk file removed");
    char lastName[64];
    std::snprintf(lastName, sizeof(lastName), "media/file_%04zu.bin", bulkCount - 1);
    CHECK(!p.Exists(lastName), "last bulk file removed");
    CHECK(!p.Exists("docs/manual.txt"), "named file removed");
    CHECK(p.Exists("docs/readme.txt"), "sibling file kept");
    CHECK(p.GetArchiveInfo().entryCount == entriesBefore - bulkCount - 1,
          "entry count dropped by exactly the deleted set");
    CHECK(EntryContentIntact(p, "docs/readme.txt"), "kept file content intact");
    CHECK(EntryContentIntact(p, "keep_root.txt"), "root file content intact");
    CHECK(EntryContentIntact(p, "also_kept.dat"), "second root file content intact");

    // Single-pass behaviour: the rewrite examines each entry of the original
    // archive once, so the callback fires at most once per entry. A
    // rewrite-per-deleted-entry implementation would fire O(N^2) times.
    CHECK(callbackCalls > 0, "progress callback invoked");
    CHECK(callbackCalls <= entriesBefore, "progress shows a single rewrite pass");
    CHECK(maxFilesProcessed <= entriesBefore, "files-processed never exceeds one pass");

    // Deleting something that does not exist must fail without touching data.
    CHECK(p.DeleteEntries({"no/such/entry"}) == VirtualFSResult::NotFound,
          "missing entry reports NotFound");
    CHECK(p.Exists("docs/readme.txt"), "failed delete leaves archive untouched");

    // Cancellation must not replace the archive.
    auto cancel = [](const VirtualFSProgress&) { return false; };
    CHECK(p.DeleteEntries({"docs/readme.txt"}, cancel) == VirtualFSResult::Cancelled,
          "cancel callback aborts delete");
    VirtualFSLibArchiveProvider check;
    CHECK(check.Open(archivePath) == VirtualFSResult::Success &&
              check.Exists("docs/readme.txt"),
          "cancelled delete leaves archive intact on disk");
    check.Close();

    // Delete everything that is left -> a valid, empty archive.
    std::vector<std::string> rest;
    for (const auto& e : p.ListAll()) rest.push_back(e.path);
    CHECK(p.DeleteEntries(rest) == VirtualFSResult::Success, "delete-all succeeds");
    CHECK(p.GetArchiveInfo().entryCount == 0, "archive is empty after delete-all");
    p.Close();
}

int main(int argc, char** argv) {
    std::string outDir = (argc > 1) ? argv[1] : "vfsdelete-test-out";
    std::error_code ec;
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir);

    // The bulk folder is 800 files - enough that an
    // O(N * archive size) implementation would visibly hang the test,
    // while a single-pass rewrite finishes instantly.
    constexpr size_t kBulkCount = 800;

    // ZIP exercises the miniz raw-copy fast path (when built in);
    // .tar.gz always exercises the generic libarchive rewrite.
    RunDeleteScenario(outDir + "/bulk.zip", "ZIP archive", kBulkCount);
    RunDeleteScenario(outDir + "/bulk.tar.gz", "TAR.GZ archive", 100);

    // Manager-level batch entry point (what UltraFiler / the Filer widget
    // calls): one DeleteFromArchive call for a mixed selection.
    std::printf("── VirtualFSManager::DeleteFromArchive ──\n");
    {
        RegisterLibArchiveProvider();
        std::string zipPath = outDir + "/manager.zip";
        std::vector<std::string> allPaths;
        CHECK(BuildArchive(zipPath, 50, allPaths), "manager test archive created");

        auto& mgr = VirtualFSManager::Instance();
        CHECK(mgr.DeleteFromArchive(zipPath, {"media", "docs/license.txt"})
                  == VirtualFSResult::Success,
              "manager batch delete succeeds");

        // The test zip stores no explicit directory entries, so check the
        // files themselves rather than directory listings.
        std::vector<uint8_t> data;
        CHECK(VirtualFS_ReadFile(zipPath + "/media/file_0000.bin", data)
                  != VirtualFSResult::Success,
              "manager delete removed the folder");
        CHECK(VirtualFS_ReadFile(zipPath + "/docs/readme.txt", data)
                      == VirtualFSResult::Success &&
                  data == PayloadFor("docs/readme.txt"),
              "manager delete kept the sibling folder");
        CHECK(VirtualFS_ReadFile(zipPath + "/docs/license.txt", data)
                  != VirtualFSResult::Success,
              "manager delete removed the named file");

        CHECK(VirtualFS_ReadFile(zipPath + "/keep_root.txt", data)
                      == VirtualFSResult::Success &&
                  data == PayloadFor("keep_root.txt"),
              "manager delete kept file content readable");
    }

    std::printf("%s: %d failure(s)\n", failures == 0 ? "SUCCESS" : "FAILURE",
                failures);
    return failures == 0 ? 0 : 1;
}
