// core/UltraCanvasFileRemoval.cpp
// Platform-independent half of UltraCanvasFileRemoval: the public entry
// points, the tree walk, and the std::filesystem backend of every platform
// but Windows. The Windows backend - read-only attributes, delete pending,
// mapped files, POSIX-semantics deletes - is
// OS/MSWindows/UltraCanvasWindowsFileRemoval.cpp.
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#include "UltraCanvasFileRemoval.h"

#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {

    // ===== STD::FILESYSTEM BACKEND =====
#if !defined(_WIN32) && !defined(_WIN64)
    FileRemovalProblem NativeClassifyFileRemovalError(const std::error_code& ec) {
        if (ec == std::errc::permission_denied ||
            ec == std::errc::operation_not_permitted ||
            ec == std::errc::read_only_file_system)
            return FileRemovalProblem::NotPermitted;
        if (ec == std::errc::device_or_resource_busy ||
            ec == std::errc::text_file_busy)
            return FileRemovalProblem::InUse;
        if (ec == std::errc::directory_not_empty)
            return FileRemovalProblem::NotEmpty;
        return FileRemovalProblem::Other;
    }

    bool NativeRemoveFileSystemEntry(const fs::path& path, std::error_code& ec,
                                     FileRemovalProblem& problem) {
        // remove() answers false without an error for a path that is not
        // there, which is a success here too.
        fs::remove(path, ec);
        if (!ec || ec == std::errc::no_such_file_or_directory) {
            ec.clear();
            return true;
        }
        problem = NativeClassifyFileRemovalError(ec);
        return false;
    }

    bool NativeFileSystemEntryIsDeletePending(const fs::path&) {
        return false;   // an unlinked entry is never listed
    }
#endif

    // ===== PUBLIC ENTRY POINTS =====
    FileRemovalProblem ClassifyFileRemovalError(const std::error_code& ec) {
        if (!ec) return FileRemovalProblem::NoProblem;
        return NativeClassifyFileRemovalError(ec);
    }

    bool FileSystemEntryIsDeletePending(const fs::path& path) {
        if (path.empty()) return false;
        return NativeFileSystemEntryIsDeletePending(path);
    }

    bool RemoveFileSystemEntry(const fs::path& path, std::error_code& ec,
                               FileRemovalProblem* problem) {
        ec.clear();
        if (problem) *problem = FileRemovalProblem::NoProblem;
        if (path.empty()) {
            ec = std::make_error_code(std::errc::invalid_argument);
            if (problem) *problem = FileRemovalProblem::Other;
            return false;
        }
        FileRemovalProblem why = FileRemovalProblem::NoProblem;
        if (NativeRemoveFileSystemEntry(path, ec, why)) {
            ec.clear();
            return true;
        }
        if (why == FileRemovalProblem::NoProblem) why = ClassifyFileRemovalError(ec);
        if (problem) *problem = why;
        return false;
    }

    namespace {
        bool RemoveTreeLevel(const fs::path& path, std::error_code& ec,
                             FileRemovalProblem& problem, fs::path& stoppedAt) {
            auto fail = [&](FileRemovalProblem why) {
                problem = why;
                stoppedAt = path;
                return false;
            };
            // symlink_status: a link is removed as the link it is.
            std::error_code statEc;
            const fs::file_status st = fs::symlink_status(path, statEc);
            if (st.type() == fs::file_type::not_found) return true;
            if (statEc) {
                // Deleted already, waiting for a program to let go of it.
                if (FileSystemEntryIsDeletePending(path)) return true;
                ec = statEc;
                return fail(ClassifyFileRemovalError(ec));
            }
            if (fs::is_directory(st)) {
                // Each pass lists the folder first and removes after, so the
                // listing is never read while entries leave it. A name seen
                // once is not handled again: an entry Windows still lists
                // while a program holds it (delete pending) is not work left.
                // A later pass only catches what an earlier listing missed.
                std::set<fs::path::string_type> seen;
                for (int pass = 0; pass < 8; ++pass) {
                    std::vector<fs::path> children;
                    std::error_code iterEc;
                    fs::directory_iterator it(path, iterEc), end;
                    if (iterEc) {
                        if (FileSystemEntryIsDeletePending(path)) return true;
                        ec = iterEc;
                        return fail(ClassifyFileRemovalError(ec));
                    }
                    for (; it != end; it.increment(iterEc)) {
                        if (iterEc) break;
                        if (seen.insert(it->path().filename().native()).second)
                            children.push_back(it->path());
                    }
                    if (iterEc) {
                        ec = iterEc;
                        return fail(ClassifyFileRemovalError(ec));
                    }
                    if (children.empty()) break;
                    for (const fs::path& child : children) {
                        if (!RemoveTreeLevel(child, ec, problem, stoppedAt))
                            return false;
                    }
                }
            }
            FileRemovalProblem why = FileRemovalProblem::NoProblem;
            if (!RemoveFileSystemEntry(path, ec, &why)) return fail(why);
            return true;
        }
    }

    bool RemoveFileSystemTree(const fs::path& path, std::error_code& ec,
                              FileRemovalProblem* problem, fs::path* stoppedAt) {
        ec.clear();
        if (problem) *problem = FileRemovalProblem::NoProblem;
        if (stoppedAt) stoppedAt->clear();
        if (path.empty()) {
            ec = std::make_error_code(std::errc::invalid_argument);
            if (problem) *problem = FileRemovalProblem::Other;
            return false;
        }
        FileRemovalProblem why = FileRemovalProblem::NoProblem;
        fs::path where;
        const bool removed = RemoveTreeLevel(path, ec, why, where);
        if (removed) ec.clear();
        if (problem) *problem = removed ? FileRemovalProblem::NoProblem : why;
        if (stoppedAt && !removed) *stoppedAt = where;
        return removed;
    }

} // namespace UltraCanvas
