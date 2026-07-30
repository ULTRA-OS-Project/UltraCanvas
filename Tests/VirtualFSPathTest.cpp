// Tests/VirtualFSPathTest.cpp
// Regression test for VirtualFSPath resolution of archive paths.
//
// VirtualFSPath::Resolve() splits a virtual path ("/home/u/a.zip/docs/x.txt")
// into the real filesystem part and the archive-internal part. The realPath it
// produces is handed verbatim to std::filesystem / the archive providers, so
// it must survive round-tripping for every path flavour the filer can emit:
// Unix absolute, relative, and Windows drive-letter / backslash paths. The
// Windows case used to come back as "/C:/Users/..." (a slash prepended to the
// drive), which no provider could open — every ZIP double-clicked in the filer
// on Windows listed as "(empty folder)".
//
// Header-only: exercises the real VirtualFSPath with no link dependencies.
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "VirtualFS/VirtualFSPath.h"

#include <cstdio>
#include <string>

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

#define CHECK_EQ(actual, expected, msg)                                     \
    do {                                                                    \
        const std::string a = (actual);                                     \
        const std::string e = (expected);                                   \
        if (a == e) {                                                       \
            std::printf("  PASS  %s\n", msg);                               \
        } else {                                                            \
            std::printf("  FAIL  %s (line %d): got \"%s\", want \"%s\"\n",  \
                        msg, __LINE__, a.c_str(), e.c_str());               \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static void TestUnixPaths() {
    std::printf("Unix absolute paths\n");

    auto r = VirtualFSPath::Resolve("/home/user/backup.zip/docs/report.txt");
    CHECK_EQ(r.realPath, "/home/user/backup.zip", "realPath is the archive");
    CHECK_EQ(r.virtualPath, "docs/report.txt", "virtualPath is the interior");
    CHECK(r.isInsideArchive, "path is inside the archive");
    CHECK(r.archiveStack.size() == 1, "one archive on the stack");

    r = VirtualFSPath::Resolve("/home/user/backup.zip");
    CHECK_EQ(r.realPath, "/home/user/backup.zip", "archive itself: realPath");
    CHECK_EQ(r.virtualPath, "", "archive itself: no virtualPath");
    CHECK(!r.isInsideArchive, "archive itself is not inside an archive");
    CHECK(r.archiveStack.size() == 1, "archive itself still stacks once");

    r = VirtualFSPath::Resolve("/home/user/plain/folder");
    CHECK_EQ(r.realPath, "/home/user/plain/folder", "non-archive: realPath");
    CHECK(r.archiveStack.empty(), "non-archive: empty stack");

    // Nested archives
    r = VirtualFSPath::Resolve("/data/outer.zip/inner.7z/file.csv");
    CHECK_EQ(r.realPath, "/data/outer.zip", "nested: realPath is outermost");
    CHECK(r.archiveStack.size() == 2, "nested: two archives on the stack");
    CHECK_EQ(r.virtualPath, "inner.7z/file.csv", "nested: virtualPath");
}

static void TestWindowsDrivePaths() {
    std::printf("Windows drive-letter paths\n");

    // The exact shape the filer produces after double-clicking a ZIP in a
    // Windows Downloads folder. realPath must keep the bare drive prefix —
    // "/C:/..." cannot be opened and made every archive list as empty.
    auto r = VirtualFSPath::Resolve("C:/Users/SFLau/Downloads/wimpinfo124.zip");
    CHECK_EQ(r.realPath, "C:/Users/SFLau/Downloads/wimpinfo124.zip",
             "drive path: realPath keeps drive prefix, no leading slash");
    CHECK_EQ(r.virtualPath, "", "drive path: archive itself has no interior");
    CHECK(!r.isInsideArchive, "drive path: archive itself not inside");
    CHECK(r.archiveStack.size() == 1, "drive path: one archive stacked");

    // Backslash-separated (raw std::filesystem::path::string() on Windows)
    r = VirtualFSPath::Resolve("C:\\Users\\SFLau\\Downloads\\wimpinfo124.zip\\sub\\file.txt");
    CHECK_EQ(r.realPath, "C:/Users/SFLau/Downloads/wimpinfo124.zip",
             "backslash path: realPath normalized, drive intact");
    CHECK_EQ(r.virtualPath, "sub/file.txt", "backslash path: virtualPath");
    CHECK(r.isInsideArchive, "backslash path: inside archive");

    // Nested archive below a drive letter
    r = VirtualFSPath::Resolve("D:/backup/outer.zip/inner.tar/readme.md");
    CHECK_EQ(r.realPath, "D:/backup/outer.zip", "drive nested: realPath");
    CHECK(r.archiveStack.size() == 2, "drive nested: two archives stacked");

    // Plain folder on a drive — no archive involved
    r = VirtualFSPath::Resolve("C:\\Users\\SFLau\\Documents");
    CHECK_EQ(r.realPath, "C:/Users/SFLau/Documents", "drive folder: realPath");
    CHECK(r.archiveStack.empty(), "drive folder: empty stack");
}

static void TestRelativePaths() {
    std::printf("Relative paths\n");

    auto r = VirtualFSPath::Resolve("downloads/archive.zip/img/a.png");
    CHECK_EQ(r.realPath, "downloads/archive.zip", "relative: realPath");
    CHECK_EQ(r.virtualPath, "img/a.png", "relative: virtualPath");
}

static void TestNormalize() {
    std::printf("Normalize\n");

    CHECK_EQ(VirtualFSPath::Normalize("C:\\Users\\SFLau\\Downloads"),
             "C:/Users/SFLau/Downloads", "backslashes become slashes");
    CHECK_EQ(VirtualFSPath::Normalize("C:/Users/../Users/SFLau"),
             "C:/Users/SFLau", "'..' collapses under a drive");
    CHECK_EQ(VirtualFSPath::Normalize("C:/Users/.."),
             "C:", "'..' can reach the drive root");
    CHECK_EQ(VirtualFSPath::Normalize("C:/.."),
             "C:", "'..' cannot escape above the drive root");
    CHECK_EQ(VirtualFSPath::Normalize("/a/b/../c"), "/a/c",
             "Unix '..' collapse still works");
    CHECK_EQ(VirtualFSPath::Normalize("../x"), "../x",
             "relative paths may keep leading '..'");
}

int main() {
    std::printf("VirtualFSPathTest\n");
    std::printf("=================\n");

    TestUnixPaths();
    TestWindowsDrivePaths();
    TestRelativePaths();
    TestNormalize();

    if (failures) {
        std::printf("\n%d FAILURE(S)\n", failures);
        return 1;
    }
    std::printf("\nAll checks passed.\n");
    return 0;
}
