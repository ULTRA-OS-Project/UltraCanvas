// Tests/InstanceLockTest.cpp
// Unit tests for UltraCanvasInstanceLock: the first lock is taken, a second
// one on the same file is refused while the first is held and taken once it
// is released, the folder is created when missing, a UTF-8 folder name works,
// and the lock is free again once the process holding it is gone.
//
// Framework-independent: builds from the lock's own sources (core plus this
// platform's backend), no display connection and no UltraCanvas library.
// On a platform with no backend Acquire() answers Unsupported by design; the
// locking checks are then skipped rather than failed.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasInstanceLock.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <filesystem>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

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
    std::printf("InstanceLockTest\n");
    // Thai and an emoji in the folder name: the lock file lives in a user's
    // data folder, which can be named anything.
    const fs::path root = fs::temp_directory_path() /
        PathFromUtf8("InstanceLockTest-\xE0\xB8\x97\xE0\xB8\x94\xE0\xB8\xAA\xE0\xB8\xAD\xE0\xB8\x9A-\xF0\x9F\x93\xAC");
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::string path = PathToUtf8(root / "sub" / "instance.lock");

    if (!InstanceLockAvailable()) {
        InstanceLock lock;
        CHECK(lock.Acquire(path) == InstanceLockResult::Unsupported, "no backend: Unsupported");
        CHECK(!lock.IsHeld(), "no backend: nothing held");
        std::printf("%d/%d checks passed (no backend on this platform)\n", checks - failures, checks);
        return failures == 0 ? 0 : 1;
    }

    {
        InstanceLock first;
        std::string error;
        CHECK(first.Acquire(path, &error) == InstanceLockResult::Acquired, "first lock is taken");
        CHECK(first.IsHeld(), "first lock reports held");
        CHECK(fs::exists(PathFromUtf8(path)), "the missing folder and file were created");
        CHECK(first.Acquire(path) == InstanceLockResult::Acquired, "taking it again while held answers Acquired");

        InstanceLock second;
        CHECK(second.Acquire(path) == InstanceLockResult::HeldElsewhere, "a second lock is refused while the first is held");
        CHECK(!second.IsHeld(), "the refused lock holds nothing");

        first.Release();
        CHECK(!first.IsHeld(), "released lock reports not held");
        CHECK(second.Acquire(path) == InstanceLockResult::Acquired, "after the release the second lock is taken");
    }
    {
        InstanceLock again;
        CHECK(again.Acquire(path) == InstanceLockResult::Acquired, "the destructor released the lock");
    }

#if !defined(_WIN32)
    // A process that ends without releasing - as a crash would - leaves no
    // stale lock behind.
    const pid_t child = ::fork();
    if (child == 0) {
        InstanceLock held;
        const bool took = held.Acquire(path) == InstanceLockResult::Acquired;
        ::_exit(took ? 0 : 3);   // no destructor runs
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "a child process took the lock");
    InstanceLock afterChild;
    CHECK(afterChild.Acquire(path) == InstanceLockResult::Acquired, "the lock is free once that process is gone");
#endif

    InstanceLock bad;
    std::string error;
    const std::string inFile = path + "/cannot-be-inside-a-file.lock";
    CHECK(bad.Acquire(inFile, &error) == InstanceLockResult::Failed, "a path that cannot be opened fails");
    CHECK(!error.empty(), "the failure says why");

    fs::remove_all(root, ec);
    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
