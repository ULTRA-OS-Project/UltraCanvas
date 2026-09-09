// Tests/FileLockTest.cpp
// Unit tests for UltraCanvasFileLock: that a file nobody touches reads Free,
// that one this test holds open is seen, that the probe changes nothing about
// the file it looks at, and that the batch call answers one entry per path in
// the order it was asked.
//
// Framework-independent: builds from the probe's own sources (core plus this
// platform's backend), no display connection and no UltraCanvas library.
//
// On a platform with no backend every probe answers Unknown by design; the
// detection checks are then skipped rather than failed, because the caller's
// documented answer there is to leave the display out.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasFileLock.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what) do { \
    ++checks; \
    if (cond) { \
        std::printf("  PASS  %s\n", what); \
    } else { \
        ++failures; \
        std::printf("  FAIL  %s (%s:%d)\n", what, __FILE__, __LINE__); \
    } \
} while (0)

int main() {
    const bool native = FileLockProbeAvailable();
    std::printf("── UltraCanvasFileLock (native backend: %s) ──\n",
                native ? "yes" : "no - every probe answers Unknown");

    const fs::path dir = fs::temp_directory_path() / "uc-filelock-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path quiet = dir / "quiet.txt";
    const fs::path held  = dir / "held.txt";
    {
        std::ofstream(quiet) << "nobody is holding this";
        std::ofstream(held)  << "this one is held open below";
    }

    // ===== A FILE NOBODY HOLDS =====
    {
        FileLockInfo info = ProbeFileLock(quiet.string());
        if (native) {
            CHECK(info.state == FileLockState::Free, "an untouched file is Free");
            CHECK(!info.writeBlocked && !info.replaceBlocked,
                  "an untouched file blocks neither write nor replace");
            CHECK(FileLockText(info).empty(), "Free has nothing to say");
            CHECK(FileLockAttributeLetter(info.state) == 0, "Free has no letter");
        } else {
            CHECK(info.state == FileLockState::Unknown,
                  "without a backend every state is Unknown");
        }
    }

    // ===== THE PROBE MUST NOT TOUCH THE FILE =====
    {
        const auto sizeBefore = fs::file_size(quiet, ec);
        const auto timeBefore = fs::last_write_time(quiet, ec);
        ProbeFileLock(quiet.string(), true);
        CHECK(fs::file_size(quiet, ec) == sizeBefore, "probing does not resize the file");
        CHECK(fs::last_write_time(quiet, ec) == timeBefore,
              "probing does not touch the modification time");
        CHECK(fs::exists(quiet, ec), "probing does not remove the file");
    }

    // ===== A FILE THIS PROCESS HOLDS OPEN =====
    // What that means is the platform's answer, not the test's: on Windows an
    // open write handle blocks a replace, on Linux it blocks nothing. Either
    // way the probe must not report it as a state it cannot support, and must
    // not crash or hang on a file that is in use.
    {
        std::ofstream keep(held, std::ios::app);
        CHECK(keep.is_open(), "the test could hold a file open");
        FileLockInfo info = ProbeFileLock(held.string(), true);
        if (native) {
            CHECK(info.state != FileLockState::Unknown,
                  "an existing file gets an answer while held open");
            CHECK(!info.Blocks() || !FileLockText(info).empty(),
                  "a blocking state always has something to say");
        }
        keep.close();
    }

    // ===== WHAT CANNOT BE ANSWERED =====
    {
        FileLockInfo gone = ProbeFileLock((dir / "no-such-file").string());
        CHECK(gone.state == FileLockState::Unknown, "a missing file is Unknown");
        FileLockInfo folder = ProbeFileLock(dir.string());
        CHECK(folder.state == FileLockState::Unknown,
              "a directory is Unknown - what holds one is not asked here");
        FileLockInfo empty = ProbeFileLock("");
        CHECK(empty.state == FileLockState::Unknown, "an empty path is Unknown");
    }

    // ===== THE BATCH ANSWERS IN ORDER =====
    {
        std::vector<std::string> paths = {
            quiet.string(), (dir / "no-such-file").string(), held.string()
        };
        std::vector<FileLockInfo> infos = ProbeFileLocks(paths);
        CHECK(infos.size() == paths.size(), "one answer per path");
        CHECK(infos[1].state == FileLockState::Unknown,
              "the answers keep the order of the paths");
        if (native)
            CHECK(infos[0].state == FileLockState::Free,
                  "a batch answers the same as a single probe");
        CHECK(ProbeFileLocks({}).empty(), "an empty batch is an empty answer");
    }

    // ===== WORDING =====
    {
        FileLockInfo info;
        info.state = FileLockState::Locked;
        info.replaceBlocked = true;
        CHECK(FileLockText(info) == "In use by another program (cannot be replaced)",
              "a nameless holder still explains the block");
        info.holders.push_back("Firefox (1234)");
        CHECK(FileLockText(info) == "In use by Firefox (1234) (cannot be replaced)",
              "a named holder is named");
        CHECK(FileLockAttributeLetter(FileLockState::Locked) == 'X' &&
              FileLockAttributeLetter(FileLockState::OpenElsewhere) == 'O',
              "the attribute letters are X (locked) and O (open)");
        FileLockInfo open;
        open.state = FileLockState::OpenElsewhere;
        CHECK(FileLockText(open) == "Open in another program",
              "being open is stated without warning about it");
    }

    fs::remove_all(dir, ec);

    std::printf("\n%d checks, %d failure%s\n", checks, failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
