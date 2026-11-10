// Tests/FileTimesTest.cpp
// Headless unit test for CopyFileTimes (UltraCanvasFileTimes.h): a file and a
// folder are given old dates, copied the way std::filesystem copies (which
// keeps none of them off Windows), and CopyFileTimes must carry the
// modification date - and, where the platform keeps one, the access date -
// over to the copy, to the nanosecond. Built against the platform backend
// where the build has one, otherwise against the core fallback. Works in a
// private temporary folder. Exits non-zero on any failure.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasFileTimes.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(ULTRACANVAS_HAS_NATIVE_FILE_TIMES) && !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "ok  " : "FAIL", what.c_str());
    if (!cond) ++failures;
}

// Twenty years before the test runs, plus a fraction of a second.
fs::file_time_type OldStamp(int extraSeconds) {
    return fs::file_time_type::clock::now() - std::chrono::hours(24 * 365 * 20)
           + std::chrono::seconds(extraSeconds) + std::chrono::microseconds(123456);
}

#if defined(ULTRACANVAS_HAS_NATIVE_FILE_TIMES) && !defined(_WIN32) && !defined(__APPLE__)
bool SameAccessTime(const fs::path& a, const fs::path& b) {
    struct stat sa {}, sb {};
    if (::stat(a.c_str(), &sa) != 0 || ::stat(b.c_str(), &sb) != 0) return false;
    return sa.st_atim.tv_sec == sb.st_atim.tv_sec && sa.st_atim.tv_nsec == sb.st_atim.tv_nsec;
}
void SetAccessTime(const fs::path& p, time_t seconds) {
    struct timespec times[2] = {{seconds, 0}, {0, UTIME_OMIT}};
    ::utimensat(AT_FDCWD, p.c_str(), times, 0);
}
#endif

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "FileTimesTest";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "source folder", ec);

    // ----- a file -----
    std::printf("File\n");
    const fs::path original = root / "photo \xC3\xA9t\xC3\xA9.jpg";   // a UTF-8 name
    { std::ofstream(original) << "not really a photo"; }
    const auto stamp = OldStamp(0);
    fs::last_write_time(original, stamp, ec);
#if defined(ULTRACANVAS_HAS_NATIVE_FILE_TIMES) && !defined(_WIN32) && !defined(__APPLE__)
    SetAccessTime(original, 1000000000);
#endif
    const fs::path copy = root / "copy.jpg";
    fs::copy_file(original, copy, ec);
    Check(!ec, "std::filesystem copied the file");

    Check(CopyFileTimes(PathToUtf8(original), PathToUtf8(copy)), "CopyFileTimes succeeded");
    Check(fs::last_write_time(copy, ec) == fs::last_write_time(original, ec),
          "the copy has the original's modification date, to the nanosecond");
#if defined(ULTRACANVAS_HAS_NATIVE_FILE_TIMES) && !defined(_WIN32) && !defined(__APPLE__)
    Check(SameAccessTime(original, copy), "the copy has the original's access date");
#endif

    // ----- a folder (dates set after its contents are written) -----
    std::printf("Folder\n");
    const fs::path folder = root / "source folder";
    fs::last_write_time(folder, OldStamp(60), ec);
    const fs::path folderCopy = root / "copied folder";
    fs::create_directories(folderCopy, ec);
    { std::ofstream(folderCopy / "child.txt") << "x"; }
    Check(CopyFileTimes(PathToUtf8(folder), PathToUtf8(folderCopy)), "CopyFileTimes on a folder");
    Check(fs::last_write_time(folderCopy, ec) == fs::last_write_time(folder, ec),
          "the folder copy has the original's modification date");

    // ----- refusals -----
    std::printf("Refusals\n");
    Check(!CopyFileTimes(PathToUtf8(root / "missing"), PathToUtf8(copy)), "a missing source is refused");
    Check(!CopyFileTimes(PathToUtf8(original), PathToUtf8(root / "missing")), "a missing target is refused");
    Check(!CopyFileTimes("", PathToUtf8(copy)), "an empty path is refused");
#if defined(ULTRACANVAS_HAS_NATIVE_FILE_TIMES) && (defined(_WIN32) || defined(__APPLE__))
    Check(CanCopyCreationTime(), "this platform carries the creation date");
#else
    Check(!CanCopyCreationTime(), "this platform cannot set a creation date");
#endif

    fs::remove_all(root, ec);
    if (failures) {
        std::printf("FileTimesTest: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("FileTimesTest: all passed\n");
    return 0;
}
