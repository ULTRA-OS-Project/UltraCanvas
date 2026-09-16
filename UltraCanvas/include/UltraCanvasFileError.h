// include/UltraCanvasFileError.h
// Shared helpers for the two things every Save path needs: writing a file
// without risking the one already there (`WriteFileAtomically`), and turning a
// failure into a clear, human-readable reason (missing, folder, locked/in-use,
// permission denied, read-only, disk full, ...) instead of a generic "failed".
// Used by every Load*/Save* path so behaviour and error reporting are
// consistent across the framework.
//
// Header-only and dependency-free (std only) so any module can use it.
// Version: 1.1.0
// Author: UltraCanvas Framework
#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <filesystem>
#include <functional>

namespace UltraCanvas {

// Explain why a file cannot be opened for READING. Returns an empty string when
// the file opens fine. On Windows an exclusively locked file reports as EACCES,
// so the message names "locked by another application".
inline std::string DescribeFileReadError(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (path.empty()) return "No file name was given.";
    if (!fs::exists(path, ec)) return "File not found: " + path;
    if (fs::is_directory(path, ec)) return "This is a folder, not a file: " + path;

    errno = 0;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f) { std::fclose(f); return std::string(); }

    int e = errno;
    switch (e) {
        case EACCES:
            return "Access denied — the file may be open or locked by another "
                   "application, or you do not have permission to read it: " + path;
#ifdef EBUSY
        case EBUSY:
            return "The file is in use or locked by another application: " + path;
#endif
        case ENOENT:
            return "File not found: " + path;
#ifdef EISDIR
        case EISDIR:
            return "This is a folder, not a file: " + path;
#endif
#ifdef EMFILE
        case EMFILE:
#endif
#ifdef ENFILE
        case ENFILE:
#endif
            return "Too many files are open; cannot open: " + path;
        default: {
            const char* sys = std::strerror(e);
            return std::string("Cannot open file (") + (sys ? sys : "unknown error") +
                   "): " + path;
        }
    }
}

// Explain why a file cannot be WRITTEN. Returns an empty string when writing
// should succeed. This is non-destructive: it never truncates or creates the
// target file, so it is safe to call after a save has already failed.
inline std::string DescribeFileWriteError(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (path.empty()) return "No file name was given.";

    fs::path p(path);
    fs::path dir = p.has_parent_path() ? p.parent_path() : fs::path(".");

    if (!fs::exists(dir, ec))      return "The destination folder does not exist: " + dir.string();
    if (!fs::is_directory(dir, ec))return "The destination is not a folder: " + dir.string();
    if (fs::exists(p, ec) && fs::is_directory(p, ec))
        return "Cannot write the file because a folder with that name exists: " + path;

    errno = 0;
    if (fs::exists(p, ec)) {
        // Open the existing file for writing WITHOUT truncating it.
        std::FILE* f = std::fopen(path.c_str(), "r+b");
        if (f) { std::fclose(f); return std::string(); }
    } else {
        // Don't touch the target path; probe the folder with a temp file instead.
        fs::path probe = dir / (".ucwrite_probe_" + std::to_string(std::time(nullptr)));
        errno = 0;
        std::FILE* pf = std::fopen(probe.string().c_str(), "wb");
        if (pf) {
            std::fclose(pf);
            std::error_code rmEc;
            fs::remove(probe, rmEc);
            return std::string();  // folder is writable -> cause not file-related
        }
    }

    int e = errno;
    switch (e) {
        case EACCES:
            return "Access denied — the file may be open or locked by another "
                   "application, or you do not have permission to write here: " + path;
#ifdef EROFS
        case EROFS:
            return "The location is read-only: " + path;
#endif
#ifdef ENOSPC
        case ENOSPC:
            return "There is not enough free space to save the file: " + path;
#endif
#ifdef EISDIR
        case EISDIR:
            return "Cannot write the file because a folder with that name exists: " + path;
#endif
        default: {
            const char* sys = std::strerror(e);
            return std::string("Cannot save file (") + (sys ? sys : "unknown error") +
                   "): " + path;
        }
    }
}

// ===== WRITING A FILE WITHOUT RISKING THE ONE ALREADY THERE =====

namespace Detail {

    // Where a write is staged. The folder is the target's own, so the rename
    // that follows never crosses a volume and is therefore atomic; the
    // extension is the target's, because a writer that picks its encoder from
    // the file name (libvips does) must see the format it was asked for; and
    // the name itself is short and starts with a dot, so it stays hidden on
    // POSIX while it exists and cannot push a deep path over Windows' MAX_PATH.
    inline std::filesystem::path AtomicWriteTempPath(const std::filesystem::path& target) {
        namespace fs = std::filesystem;
        static std::atomic<unsigned> counter{0};
        const fs::path dir = target.has_parent_path() ? target.parent_path() : fs::path(".");
        const auto stamp = static_cast<unsigned long long>(
                std::chrono::steady_clock::now().time_since_epoch().count()) & 0xffffffu;
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            const fs::path candidate = dir / (".ucsave-" + std::to_string(stamp) + "-" +
                                              std::to_string(counter.fetch_add(1)) +
                                              target.extension().string());
            std::error_code ec;
            if (!fs::exists(candidate, ec)) return candidate;
        }
        return dir / (".ucsave" + target.extension().string());
    }

    // Removes the staged file unless the write was committed - including when
    // the writer throws.
    struct AtomicWriteTemp {
        std::filesystem::path path;
        bool committed = false;
        ~AtomicWriteTemp() {
            if (committed) return;
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };

} // namespace Detail

// Writes `path` through a temporary file in its own folder, and moves the
// result over the target only once `writer` says it finished.
//
//   std::string error = WriteFileAtomically(path, [&](const std::string& out) {
//       return Encode(out) ? std::string() : std::string("the encoder failed");
//   });
//   if (!error.empty()) ...   // nothing was written; the old file is intact
//
// A writer handed the target directly truncates it before the first byte of
// the new content exists, so anything that then goes wrong - no space left, an
// encoder error, a destination another program holds open - costs the user the
// file they had. Staging the write costs a rename and removes that whole class
// of loss.
//
// `writer` returns "" for success, or the reason it failed. Any mention of the
// staged path in that reason is replaced by the caller's own path, which is
// the only one the person reading it knows about. An exception from `writer`
// propagates with the staged file removed.
//
// The target's permissions are carried across, so replacing a file does not
// widen it to whatever a new file is created with, and a symlink is written
// through rather than replaced.
inline std::string WriteFileAtomically(
        const std::string& path,
        const std::function<std::string(const std::string&)>& writer) {
    namespace fs = std::filesystem;
    if (path.empty()) return "No file name was given.";
    if (!writer) return "Nothing was given to write " + path + " with.";

    std::error_code ec;
    fs::path target(path);
    if (fs::is_symlink(target, ec)) {
        std::error_code resolveEc;
        const fs::path resolved = fs::weakly_canonical(target, resolveEc);
        if (!resolveEc && !resolved.empty()) target = resolved;
    }
    ec.clear();

    Detail::AtomicWriteTemp temp{ Detail::AtomicWriteTempPath(target) };
    const std::string staged = temp.path.string();

    auto retarget = [&staged, &path](std::string message) {
        for (size_t at = message.find(staged); at != std::string::npos;
             at = message.find(staged, at + path.size())) {
            message.replace(at, staged.size(), path);
        }
        return message;
    };

    const std::string failure = writer(staged);
    if (!failure.empty()) return retarget(failure);

    if (fs::exists(target, ec)) {
        const fs::perms mode = fs::status(target, ec).permissions();
        if (!ec) fs::permissions(temp.path, mode, ec);
    }
    ec.clear();

    fs::rename(temp.path, target, ec);
    if (ec) {
        // The content was written and the destination is what refused it: it
        // is held by another program, or on a volume that will not take the
        // replacement. Say which, and leave nothing behind.
        const std::string why = DescribeFileWriteError(path);
        return !why.empty() ? why
                            : ("Could not put the saved file in place: " + path +
                               " (" + ec.message() + ")");
    }
    temp.committed = true;
    return std::string();
}

} // namespace UltraCanvas
