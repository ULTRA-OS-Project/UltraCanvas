// include/UltraCanvasFileError.h
// Shared helpers that turn a failed file open/save into a clear, human-readable
// reason (missing, folder, locked/in-use, permission denied, read-only, disk
// full, ...) instead of a generic "failed". Used by every Load*/Save* path so
// error reporting is consistent across the framework.
//
// Header-only and dependency-free (std only) so any module can use it.
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <filesystem>

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

} // namespace UltraCanvas
