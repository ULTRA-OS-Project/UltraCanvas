// Tests/ElevatedFileOperationsTest.cpp
// The "Delete as administrator" retry (UltraCanvasElevatedFileOperations),
// minus the consent prompt: the command-line quoting the helper's argv is
// parsed back from, the splitting of a long list into command lines that
// fit, the report file both processes agree on, the helper's own delete in
// a temp folder (read-only entries included), and what a platform without a
// backend answers. Starting a real helper would put up a UAC prompt, so the
// launch itself is the one thing this cannot cover.
// Runs on Windows too (ULTRACANVAS_BUILD_FILER_TESTS), where the helper's
// delete meets read-only folders and files in use for real.
// Version: 1.1.0 - registered (FilerTests.cmake); paths through PathToUtf8;
//                  Windows: read-only folders, and a file in use named
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#include "UltraCanvasElevatedFileOperations.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace UltraCanvas;
using namespace UltraCanvas::ElevatedFileOperations;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what);
    if (!ok) ++failures;
}

void CheckEq(const std::string& got, const std::string& want, const char* what) {
    const bool ok = got == want;
    std::printf("%s  %s", ok ? "  ok  " : " FAIL ", what);
    if (!ok) std::printf("  (got \"%s\", want \"%s\")", got.c_str(), want.c_str());
    std::printf("\n");
    if (!ok) ++failures;
}

// A scratch folder that goes away with the test.
struct ScratchFolder {
    fs::path path;
    ScratchFolder() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / "UltraCanvasElevatedFileOperationsTest";
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~ScratchFolder() {
        std::error_code ec;
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(path, ec);
    }
};

void WriteFile(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
}

void TestQuoting() {
    std::printf("Command-line quoting\n");
    CheckEq(QuoteCommandLineArgument("C:\\Windows\\Temp\\a.txt"),
            "C:\\Windows\\Temp\\a.txt", "no spaces: passed through");
    CheckEq(QuoteCommandLineArgument("C:\\Program Files\\x.txt"),
            "\"C:\\Program Files\\x.txt\"", "a space: quoted");
    CheckEq(QuoteCommandLineArgument("C:\\Dir With Space\\"),
            "\"C:\\Dir With Space\\\\\"", "trailing backslash before the closing quote is doubled");
    CheckEq(QuoteCommandLineArgument("say \"hi\""),
            "\"say \\\"hi\\\"\"", "embedded quotes are escaped");
    CheckEq(QuoteCommandLineArgument("a\\\"b"),
            "\"a\\\\\\\"b\"", "backslash before an embedded quote is doubled too");
    CheckEq(QuoteCommandLineArgument("a\\\\b"),
            "a\\\\b", "backslashes not before a quote stay as they are");
    CheckEq(QuoteCommandLineArgument(""), "\"\"", "an empty argument survives as \"\"");
    CheckEq(QuoteCommandLineArgument("tab\there"), "\"tab\there\"", "a tab needs quotes");
}

void TestSplitting() {
    std::printf("Splitting into command lines\n");
    std::vector<std::string> paths;
    for (int i = 0; i < 10; ++i) paths.push_back("C:\\f\\" + std::to_string(i) + ".txt");
    // Each path is 11 characters plus its separator.
    auto runs = SplitIntoCommandLines(paths, 20, 20 + 12 * 4);
    Check(runs.size() == 3, "ten paths of twelve at a budget of four per line: three lines");
    size_t total = 0;
    for (const auto& r : runs) total += r.size();
    Check(total == 10, "every path lands in exactly one line");
    Check(runs[0].size() == 4 && runs[1].size() == 4 && runs[2].size() == 2,
          "lines fill up in order (4, 4, 2)");

    runs = SplitIntoCommandLines({std::string(500, 'x')}, 10, 100);
    Check(runs.size() == 1 && runs[0].size() == 1,
          "a single path longer than the budget still gets its own line");

    runs = SplitIntoCommandLines({}, 10, 100);
    Check(runs.empty(), "no paths: no lines");

    runs = SplitIntoCommandLines(paths, 20, 100000);
    Check(runs.size() == 1 && runs[0].size() == 10, "a generous budget: one line");
}

void TestReport() {
    std::printf("Report encoding\n");
    std::vector<ElevatedFailure> in = {
        {"C:\\a b\\x.txt", "Access is denied."},
        {"C:\\y", "The process cannot access the file\r\nbecause it is in use."},
    };
    const std::string text = FormatReport(in);
    CheckEq(text, "C:\\a b\\x.txt\tAccess is denied.\n"
                  "C:\\y\tThe process cannot access the file because it is in use.\n",
            "one line per failure, CR dropped, LF in a reason flattened");
    auto out = ParseReport(text);
    Check(out.size() == 2, "parses back to two failures");
    if (out.size() == 2) {
        CheckEq(out[0].path, "C:\\a b\\x.txt", "path with a space survives");
        CheckEq(out[0].reason, "Access is denied.", "reason survives");
        CheckEq(out[1].reason, "The process cannot access the file because it is in use.",
                "flattened reason survives");
    }
    Check(ParseReport("").empty(), "an empty report is no failures");
    Check(ParseReport("\r\n\n").empty(), "blank lines are ignored");
    out = ParseReport("C:\\noreason\r\n");
    Check(out.size() == 1 && out[0].path == "C:\\noreason" && out[0].reason.empty(),
          "a line without a tab is a path without a reason (CRLF tolerated)");
}

void TestHelperDelete() {
    std::printf("The helper's delete\n");
    ScratchFolder scratch;
    std::error_code ec;

    const fs::path file = scratch.path / "plain.txt";
    const fs::path locked = scratch.path / "locked.txt";
    const fs::path tree = scratch.path / "tree";
    WriteFile(file, "x");
    WriteFile(locked, "x");
    fs::create_directories(tree / "sub", ec);
    WriteFile(tree / "sub" / "inner.txt", "x");
    fs::permissions(locked, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove, ec);
    fs::permissions(tree / "sub" / "inner.txt",
                    fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove, ec);

    std::vector<ElevatedFailure> failed;
    const int code = RunHelperDelete({PathToUtf8(file), PathToUtf8(locked), PathToUtf8(tree),
                                      PathToUtf8(scratch.path / "never-existed")},
                                     failed);
    Check(code == kHelperExitOk, "exit 0 when everything went");
    Check(failed.empty(), "no failures reported");
    Check(!fs::exists(file, ec), "plain file gone");
    Check(!fs::exists(locked, ec), "read-only file gone (protection lifted first)");
    Check(!fs::exists(tree, ec), "folder with a read-only file inside gone");

#if defined(_WIN32) || defined(_WIN64)
    // What a folder copied off a CD or a camera card looks like: the
    // read-only attribute on the folders too. Windows refuses to delete a
    // read-only folder with the "access denied" a real permission problem
    // gives; the helper lifts it as it goes.
    const fs::path album = scratch.path / "Lei";
    fs::create_directories(album / "Album", ec);
    WriteFile(album / "Album" / "photo.jpg", "x");
    SetFileAttributesW((album / "Album").c_str(), FILE_ATTRIBUTE_READONLY);
    SetFileAttributesW(album.c_str(), FILE_ATTRIBUTE_READONLY);
    failed.clear();
    Check(RunHelperDelete({PathToUtf8(album)}, failed) == kHelperExitOk && failed.empty(),
          "Windows: a tree of read-only folders goes");
    Check(!fs::exists(album, ec), "Windows: and is gone");

    // A file a program holds: no administrator gets past that, and the
    // reason says which file and that it is in use.
    const fs::path held = scratch.path / "held";
    fs::create_directories(held, ec);
    const fs::path inner = held / "IMG_0001.JPG";
    WriteFile(inner, "x");
    HANDLE h = CreateFileW(inner.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    failed.clear();
    Check(RunHelperDelete({PathToUtf8(held)}, failed) == kHelperExitPartial &&
          failed.size() == 1, "Windows: a folder with a file in use is reported");
    if (failed.size() == 1) {
        std::printf("        (reason: %s)\n", failed[0].reason.c_str());
        Check(failed[0].reason.rfind("IMG_0001.JPG: in use", 0) == 0,
              "Windows: the reason names the file inside and says it is in use");
    }
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
#endif

    failed.clear();
    const int relative = RunHelperDelete({"relative/path.txt"}, failed);
    Check(relative == kHelperExitPartial, "a relative path: partial exit");
    Check(failed.size() == 1 && failed[0].reason == "Not an absolute path",
          "a relative path is refused, not resolved");

    failed.clear();
    Check(RunHelperDelete({}, failed) == kHelperExitBadArguments, "no paths: bad arguments");
}

void TestHelperEntry() {
    std::printf("RunHelperIfRequested\n");
    int exitCode = -1;
    // Only meaningful off Windows, where argv is what the core reads; on
    // Windows the backend reads the real command line, which is this test's.
    const char* ordinary[] = {"app", "/some/folder"};
    Check(!RunHelperIfRequested(2, const_cast<char**>(ordinary), exitCode),
          "an ordinary command line is not the helper");
#if !defined(_WIN32) && !defined(_WIN64)
    ScratchFolder scratch;
    const fs::path report = scratch.path / "report.txt";
    const fs::path victim = scratch.path / "victim.txt";
    WriteFile(victim, "x");
    const std::string reportS = PathToUtf8(report), victimS = PathToUtf8(victim);
    const char* helper[] = {"app", kHelperDeleteFlag, reportS.c_str(), victimS.c_str()};
    Check(RunHelperIfRequested(4, const_cast<char**>(helper), exitCode),
          "the helper flag makes this the helper");
    Check(exitCode == kHelperExitOk, "and it exits 0");
    std::error_code ec;
    Check(!fs::exists(victim, ec), "the victim is gone");
    Check(fs::exists(report, ec) && fs::file_size(report, ec) == 0,
          "an empty report is written for a clean run");

    const char* missing[] = {"app", kHelperDeleteFlag};
    Check(RunHelperIfRequested(2, const_cast<char**>(missing), exitCode) &&
          exitCode == kHelperExitBadArguments,
          "the flag without a report file: helper, bad arguments");
#endif
}

void TestAvailability() {
    std::printf("Availability\n");
#if defined(_WIN32) || defined(_WIN64)
    // RunHelperIfRequested above installed the helper; whether the retry is
    // offered then depends only on this process not being elevated already.
    Check(IsAvailable() == !ProcessIsElevated(),
          "Windows: available exactly when not already elevated");
#else
    Check(!IsAvailable(), "no backend: never available");
    Check(!ProcessIsElevated(), "no backend: never elevated");
    auto result = DeleteElevated({"/nowhere"});
    Check(result.outcome == ElevatedOutcome::Unavailable, "no backend: Unavailable");
    Check(!result.error.empty(), "and it says why");
#endif
    Check(DeleteElevated({}).outcome == ElevatedOutcome::Completed,
          "nothing to delete completes without a helper");
    Check(IsPermissionFailure(std::make_error_code(std::errc::permission_denied)),
          "EACCES is a permission failure");
    Check(!IsPermissionFailure(std::make_error_code(std::errc::no_such_file_or_directory)),
          "ENOENT is not");
    Check(!IsPermissionFailure(std::error_code()), "no error is not");
}

} // namespace

int main() {
    std::printf("=== UltraCanvasElevatedFileOperations ===\n");
    TestQuoting();
    TestSplitting();
    TestReport();
    TestHelperDelete();
    TestHelperEntry();
    TestAvailability();
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
