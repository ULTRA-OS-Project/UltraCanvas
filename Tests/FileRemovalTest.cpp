// Tests/FileRemovalTest.cpp
// UltraCanvasFileRemoval: deleting for good, and saying why it failed in
// terms of the user's next step. Everywhere: files, empty and non-empty
// folders, trees with links in them (removed as links), the classification
// of errors. On Windows, the four things "access denied" means there, each
// made for real: read-only files and FOLDERS (removed), an entry deleted
// while another handle holds it ("delete pending" - counted as removed, and
// its folder waited for until the holder lets go), a memory-mapped file and
// one open without delete sharing (InUse - never NotPermitted, which is what
// sent UltraFiler's delete to an administrator who could do no better).
// Runs on Windows in CI (ULTRACANVAS_BUILD_FILER_TESTS); its scratch folder
// has a Thai name, so every call goes through UTF-16 there.
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#include "UltraCanvasFileRemoval.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

    int failures = 0;

    void Check(bool ok, const char* what) {
        std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what);
        if (!ok) ++failures;
    }

    const char* ProblemName(FileRemovalProblem p) {
        switch (p) {
            case FileRemovalProblem::NoProblem:    return "NoProblem";
            case FileRemovalProblem::InUse:        return "InUse";
            case FileRemovalProblem::NotPermitted: return "NotPermitted";
            case FileRemovalProblem::NotEmpty:     return "NotEmpty";
            case FileRemovalProblem::Other:        return "Other";
        }
        return "?";
    }

    void Show(const char* what, const std::error_code& ec, FileRemovalProblem p) {
        std::printf("        (%s: %s, \"%s\")\n", what, ProblemName(p),
                    ec ? ec.message().c_str() : "no error");
    }

    void WriteFile(const fs::path& p, const std::string& text) {
        std::ofstream out(p, std::ios::binary);
        out << text;
    }

    // A scratch folder with a Thai name that goes away with the test.
    struct ScratchFolder {
        fs::path path;
        ScratchFolder() {
            std::error_code ec;
            path = fs::temp_directory_path(ec) /
                   PathFromUtf8("UltraCanvasFileRemovalTest \xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2");
            RemoveFileSystemTree(path, ec);
            fs::create_directories(path, ec);
        }
        ~ScratchFolder() {
            std::error_code ec;
            // A failed check may leave a folder this user cannot write to.
            fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ec);
            for (fs::recursive_directory_iterator it(path, ec), end; !ec && it != end;
                 it.increment(ec)) {
                std::error_code pec;
                fs::permissions(it->path(), fs::perms::owner_all, fs::perm_options::add, pec);
            }
            RemoveFileSystemTree(path, ec);
        }
    };

    void TestEntries(const fs::path& root) {
        std::printf("One entry\n");
        std::error_code ec;
        FileRemovalProblem problem = FileRemovalProblem::Other;

        const fs::path file = root / "plain.txt";
        WriteFile(file, "x");
        Check(RemoveFileSystemEntry(file, ec, &problem) && !ec &&
              problem == FileRemovalProblem::NoProblem, "a file is removed");
        Check(!fs::exists(file, ec), "and is gone");

        Check(RemoveFileSystemEntry(root / "never-existed", ec, &problem) && !ec,
              "a path that is not there counts as removed");

        const fs::path full = root / "full";
        fs::create_directories(full, ec);
        WriteFile(full / "a.txt", "x");
        const bool removedFull = RemoveFileSystemEntry(full, ec, &problem);
        Show("full folder", ec, problem);
        Check(!removedFull && ec && problem == FileRemovalProblem::NotEmpty,
              "a folder with entries is not removed: NotEmpty");
        Check(fs::exists(full / "a.txt", ec), "and nothing in it is touched");

        const fs::path empty = root / "empty";
        fs::create_directories(empty, ec);
        Check(RemoveFileSystemEntry(empty, ec, &problem), "an empty folder is removed");
        Check(!fs::exists(empty, ec), "and is gone");

        Check(!FileSystemEntryIsDeletePending(full / "a.txt"),
              "an ordinary file is not delete pending");
        Check(!FileSystemEntryIsDeletePending(root / "never-existed"),
              "nor is a path that is not there");
        Check(!RemoveFileSystemEntry(fs::path(), ec, &problem) && ec,
              "an empty path is refused, not resolved");
    }

    void TestTree(const fs::path& root) {
        std::printf("A tree\n");
        std::error_code ec;
        FileRemovalProblem problem = FileRemovalProblem::Other;
        fs::path stoppedAt = "unset";

        const fs::path tree = root / "Lei";
        fs::create_directories(tree / "Album" / "2024", ec);
        WriteFile(tree / "a.jpg", "x");
        WriteFile(tree / "Album" / "b.jpg", "x");
        WriteFile(tree / "Album" / "2024" / "c.jpg", "x");
        // A read-only file: the read-only attribute on Windows, a mode
        // without write bits elsewhere (where unlinking does not care).
        fs::permissions(tree / "Album" / "b.jpg",
                        fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                        fs::perm_options::remove, ec);
        // A link to a folder outside: removed as the link, the folder kept.
        const fs::path outside = root / "outside";
        fs::create_directories(outside, ec);
        WriteFile(outside / "keep.txt", "x");
        std::error_code linkEc;
        fs::create_directory_symlink(outside, tree / "link", linkEc);
        const bool haveLink = !linkEc;

        const bool removed = RemoveFileSystemTree(tree, ec, &problem, &stoppedAt);
        Show("tree", ec, problem);
        Check(removed && !ec && problem == FileRemovalProblem::NoProblem,
              "a tree with a read-only file in it is removed");
        Check(stoppedAt.empty(), "nothing stopped it");
        Check(!fs::exists(tree, ec), "and is gone");
        if (haveLink) {
            Check(fs::exists(outside / "keep.txt", ec),
                  "a link inside is removed as a link, its target kept");
        } else {
            std::printf("        (no directory link: this account may not create one)\n");
        }

        Check(RemoveFileSystemTree(root / "never-existed", ec, &problem) && !ec,
              "a tree that is not there counts as removed");

        const fs::path single = root / "single.txt";
        WriteFile(single, "x");
        Check(RemoveFileSystemTree(single, ec, &problem) && !fs::exists(single, ec),
              "a tree that is one file is removed");
    }

    void TestClassification() {
        std::printf("Classification\n");
        Check(ClassifyFileRemovalError(std::error_code()) == FileRemovalProblem::NoProblem,
              "no error: NoProblem");
        Check(ClassifyFileRemovalError(std::make_error_code(std::errc::permission_denied)) ==
                      FileRemovalProblem::NotPermitted,
              "EACCES: NotPermitted");
        Check(ClassifyFileRemovalError(std::make_error_code(std::errc::device_or_resource_busy)) ==
                      FileRemovalProblem::InUse,
              "EBUSY: InUse");
        Check(ClassifyFileRemovalError(std::make_error_code(std::errc::directory_not_empty)) ==
                      FileRemovalProblem::NotEmpty,
              "ENOTEMPTY: NotEmpty");
        Check(ClassifyFileRemovalError(std::make_error_code(std::errc::io_error)) ==
                      FileRemovalProblem::Other,
              "EIO: Other");
#if defined(_WIN32) || defined(_WIN64)
        auto win32 = [](DWORD error) {
            return std::error_code(static_cast<int>(error), std::system_category());
        };
        Check(ClassifyFileRemovalError(win32(ERROR_SHARING_VIOLATION)) == FileRemovalProblem::InUse,
              "ERROR_SHARING_VIOLATION: InUse");
        Check(ClassifyFileRemovalError(win32(ERROR_USER_MAPPED_FILE)) == FileRemovalProblem::InUse,
              "ERROR_USER_MAPPED_FILE: InUse");
        Check(ClassifyFileRemovalError(win32(ERROR_ACCESS_DENIED)) == FileRemovalProblem::NotPermitted,
              "ERROR_ACCESS_DENIED from a failed open: NotPermitted");
        Check(ClassifyFileRemovalError(win32(ERROR_DIR_NOT_EMPTY)) == FileRemovalProblem::NotEmpty,
              "ERROR_DIR_NOT_EMPTY: NotEmpty");
#endif
    }

#if defined(_WIN32) || defined(_WIN64)
    DWORD Attributes(const fs::path& p) { return GetFileAttributesW(p.c_str()); }

    void SetReadOnly(const fs::path& p) {
        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_READONLY);
    }

    // Another program's handle on `p`, the way a virus scanner, the search
    // indexer or a thumbnail extractor holds a file: every sharing flag, so
    // a delete goes through - and, marked deleted through that handle, the
    // file stays listed as "delete pending" until the handle is closed.
    HANDLE HoldDeletePending(const fs::path& p) {
        HANDLE h = CreateFileW(p.c_str(), DELETE | GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) return h;
        struct { BOOLEAN deleteFile; } classic = {TRUE};   // FILE_DISPOSITION_INFO
        SetFileInformationByHandle(h, FileDispositionInfo, &classic, sizeof(classic));
        return h;
    }

    void TestReadOnlyFolders(const fs::path& root) {
        std::printf("Windows: read-only folders\n");
        std::error_code ec;
        FileRemovalProblem problem = FileRemovalProblem::Other;
        fs::path stoppedAt;

        // What a folder copied off a CD or a camera card, or one with a
        // desktop.ini, looks like: the read-only attribute on the folders as
        // well as the files. Windows refuses to delete either while it is
        // set, with the "access denied" a real permission problem gives.
        const fs::path tree = root / "Lei (read-only)";
        fs::create_directories(tree / "Album", ec);
        WriteFile(tree / "Album" / "photo.jpg", "x");
        WriteFile(tree / "desktop.ini", "[.ShellClassInfo]\r\n");
        SetReadOnly(tree / "Album" / "photo.jpg");
        SetReadOnly(tree / "desktop.ini");
        SetReadOnly(tree / "Album");
        SetReadOnly(tree);
        Check((Attributes(tree / "Album") & FILE_ATTRIBUTE_READONLY) != 0,
              "setup: the folder carries the read-only attribute");

        const fs::path emptyLocked = root / "empty read-only";
        fs::create_directories(emptyLocked, ec);
        SetReadOnly(emptyLocked);
        const bool removedEmpty = RemoveFileSystemEntry(emptyLocked, ec, &problem);
        Show("empty read-only folder", ec, problem);
        Check(removedEmpty && !fs::exists(emptyLocked, ec),
              "an empty read-only folder is removed");

        const bool removed = RemoveFileSystemTree(tree, ec, &problem, &stoppedAt);
        Show("read-only tree", ec, problem);
        Check(removed && problem == FileRemovalProblem::NoProblem,
              "a tree of read-only folders and files is removed");
        Check(!fs::exists(tree, ec), "and is gone");
    }

    void TestDeletePending(const fs::path& root) {
        std::printf("Windows: delete pending\n");
        std::error_code ec;
        FileRemovalProblem problem = FileRemovalProblem::Other;
        fs::path stoppedAt;

        const fs::path folder = root / "pending";
        fs::create_directories(folder, ec);
        const fs::path held = folder / "held.jpg";
        WriteFile(held, "x");
        HANDLE h = HoldDeletePending(held);
        Check(h != INVALID_HANDLE_VALUE, "setup: a handle that marked the file deleted");
        if (h == INVALID_HANDLE_VALUE) return;

        Check(FileSystemEntryIsDeletePending(held), "the file is delete pending");
        std::error_code statEc;
        (void)fs::symlink_status(held, statEc);
        std::printf("        (std::filesystem says about it: \"%s\")\n",
                    statEc ? statEc.message().c_str() : "nothing");
        Check(RemoveFileSystemEntry(held, ec, &problem) && !ec,
              "removing it again counts as removed, not \"access denied\"");

        // Its folder: not empty while the holder keeps the file, so it is
        // waited for - and the holder lets go a moment later.
        std::thread holder([h]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            CloseHandle(h);
        });
        const auto started = std::chrono::steady_clock::now();
        const bool removed = RemoveFileSystemTree(folder, ec, &problem, &stoppedAt);
        const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        holder.join();
        Show("folder of a pending file", ec, problem);
        std::printf("        (waited %lld ms)\n", static_cast<long long>(waited));
        Check(removed && !fs::exists(folder, ec),
              "its folder is removed once the holder lets go, inside the wait");

        // A holder that keeps it past the wait: in use, never "permission".
        const fs::path folder2 = root / "pending long";
        fs::create_directories(folder2, ec);
        const fs::path held2 = folder2 / "held.jpg";
        WriteFile(held2, "x");
        HANDLE h2 = HoldDeletePending(held2);
        const bool removed2 = RemoveFileSystemTree(folder2, ec, &problem, &stoppedAt);
        Show("held past the wait", ec, problem);
        Check(!removed2 && problem == FileRemovalProblem::InUse,
              "held past the wait: InUse, not NotPermitted");
        Check(stoppedAt == folder2, "the folder is what stopped");
        if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
        Check(RemoveFileSystemTree(folder2, ec, &problem) && !fs::exists(folder2, ec),
              "and removed once the holder let go");
    }

    void TestInUse(const fs::path& root) {
        std::printf("Windows: in use\n");
        std::error_code ec;
        FileRemovalProblem problem = FileRemovalProblem::Other;

        // A picture libvips has read: mapped into the process, the file
        // itself closed again. std::filesystem's delete answers "access
        // denied" - which is how UltraFiler came to ask for administrator
        // rights that cannot help.
        const fs::path mapped = root / "IMG_0001.JPG";
        WriteFile(mapped, std::string(64 * 1024, 'x'));
        SetReadOnly(mapped);   // and the attribute must come back on failure
        HANDLE file = CreateFileW(mapped.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        HANDLE mapping = file == INVALID_HANDLE_VALUE
                ? nullptr
                : CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        void* view = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
        if (mapping) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        Check(view != nullptr, "setup: the file is mapped, its handles closed");

        const bool removed = RemoveFileSystemEntry(mapped, ec, &problem);
        Show("mapped file", ec, problem);
        Check(problem != FileRemovalProblem::NotPermitted,
              "a mapped file is never taken for a permission problem");
        if (removed) {
            std::printf("        (removed: this file system deletes a mapped file "
                        "with POSIX semantics)\n");
        } else {
            Check(problem == FileRemovalProblem::InUse, "it is in use");
            Check((Attributes(mapped) & FILE_ATTRIBUTE_READONLY) != 0,
                  "its read-only attribute is put back");
        }
        if (view) UnmapViewOfFile(view);
        if (!removed) {
            Check(RemoveFileSystemEntry(mapped, ec, &problem) && !fs::exists(mapped, ec),
                  "unmapped, it is removed");
        }

        // Open without delete sharing, as most programs open a document.
        const fs::path open = root / "open.txt";
        WriteFile(open, "x");
        HANDLE h = CreateFileW(open.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        const bool removedOpen = RemoveFileSystemEntry(open, ec, &problem);
        Show("open without delete sharing", ec, problem);
        Check(!removedOpen && problem == FileRemovalProblem::InUse,
              "a file open without delete sharing: InUse");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        Check(RemoveFileSystemEntry(open, ec, &problem), "closed, it is removed");

        // Deep in a tree: the tree reports the file, and why.
        const fs::path tree = root / "tree in use";
        fs::create_directories(tree / "sub", ec);
        const fs::path inner = tree / "sub" / "inner.txt";
        WriteFile(inner, "x");
        HANDLE hi = CreateFileW(inner.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        fs::path stoppedAt;
        const bool removedTree = RemoveFileSystemTree(tree, ec, &problem, &stoppedAt);
        Check(!removedTree && problem == FileRemovalProblem::InUse && stoppedAt == inner,
              "a tree names the file inside that is in use");
        if (hi != INVALID_HANDLE_VALUE) CloseHandle(hi);
        Check(RemoveFileSystemTree(tree, ec, &problem) && !fs::exists(tree, ec),
              "and goes once it is closed");
    }
#else
    void TestNotPermitted(const fs::path& root) {
        std::printf("POSIX: not permitted\n");
        if (geteuid() == 0) {
            std::printf("        (skipped: root is refused nothing)\n");
            return;
        }
        std::error_code ec;
        FileRemovalProblem problem = FileRemovalProblem::Other;
        fs::path stoppedAt;
        const fs::path tree = root / "locked";
        fs::create_directories(tree / "sub", ec);
        const fs::path inner = tree / "sub" / "inner.txt";
        WriteFile(inner, "x");
        // A folder this user may not write to: nothing in it can go.
        fs::permissions(tree / "sub",
                        fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                        fs::perm_options::remove, ec);
        const bool removed = RemoveFileSystemTree(tree, ec, &problem, &stoppedAt);
        Show("file in a folder without write permission", ec, problem);
        Check(!removed && problem == FileRemovalProblem::NotPermitted,
              "a file in a folder this user may not write to: NotPermitted");
        Check(stoppedAt == inner, "the file inside is named, not the folder");
        fs::permissions(tree / "sub", fs::perms::owner_all, fs::perm_options::add, ec);
        Check(RemoveFileSystemTree(tree, ec, &problem) && !fs::exists(tree, ec),
              "with the permission back, it goes");
    }
#endif

} // namespace

int main() {
    std::printf("=== UltraCanvasFileRemoval ===\n");
    ScratchFolder scratch;
    TestEntries(scratch.path);
    TestTree(scratch.path);
    TestClassification();
#if defined(_WIN32) || defined(_WIN64)
    TestReadOnlyFolders(scratch.path);
    TestDeletePending(scratch.path);
    TestInUse(scratch.path);
#else
    TestNotPermitted(scratch.path);
#endif
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
