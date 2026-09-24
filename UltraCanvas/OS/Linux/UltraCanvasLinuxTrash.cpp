// OS/Linux/UltraCanvasLinuxTrash.cpp
// MoveToTrash for freedesktop.org desktops (Linux, and the BSDs, which build
// from this directory): the Trash specification 1.0.
//
//   * A file on the same file system as the home trash ($XDG_DATA_HOME/Trash,
//     ~/.local/share/Trash by default) goes there, and its .trashinfo records
//     the absolute path it came from.
//   * A file on any other file system - a USB stick, a second partition - goes
//     to that drive's own trash: $topdir/.Trash/$uid when an administrator has
//     set up a shared, sticky $topdir/.Trash, otherwise $topdir/.Trash-$uid,
//     created on demand. Its .trashinfo path is relative to $topdir, so the
//     drive can be restored from under any mount point. A trash never copies
//     a file across drives: that would be slow, would fill the home drive,
//     and is not what "Restore" in the desktop's file manager expects.
//   * The name in the trash is claimed by creating info/<name>.trashinfo with
//     O_EXCL, so two programs trashing "Report.pdf" at once cannot collide; a
//     taken name becomes "Report.2.pdf", "Report.3.pdf", ...
//   * The move itself is one rename(2): atomic, and a failure leaves the file
//     where it was (the claimed .trashinfo is removed again).
// Version: 1.0.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework

#include "UltraCanvasTrash.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace UltraCanvas {
namespace {

    std::string ErrnoText(int err) { return std::strerror(err); }

    // `path` without trailing slashes ("/" stays "/").
    std::string StripTrailingSlashes(std::string path) {
        while (path.size() > 1 && path.back() == '/') path.pop_back();
        return path;
    }

    // The folder holding `path` ("/a/b" -> "/a", "/a" -> "/").
    std::string ParentOf(const std::string& path) {
        const size_t slash = path.find_last_of('/');
        if (slash == std::string::npos) return ".";
        if (slash == 0) return "/";
        return path.substr(0, slash);
    }

    std::string BaseNameOf(const std::string& path) {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    std::string JoinPath(const std::string& dir, const std::string& name) {
        return dir == "/" ? "/" + name : dir + "/" + name;
    }

    // `path` made absolute against the working directory, with "." and ".."
    // resolved lexically - never through symbolic links: trashing a link
    // moves the link, not what it points at.
    std::string AbsolutePath(const std::string& path) {
        std::string full = path;
        if (full.empty() || full.front() != '/') {
            char cwd[4096];
            if (!getcwd(cwd, sizeof cwd)) return {};
            full = std::string(cwd) + "/" + full;
        }
        std::string out;
        size_t i = 0;
        while (i < full.size()) {
            while (i < full.size() && full[i] == '/') ++i;
            const size_t end = full.find('/', i);
            const std::string part = full.substr(i, end == std::string::npos
                                                     ? std::string::npos : end - i);
            i = end == std::string::npos ? full.size() : end;
            if (part.empty() || part == ".") continue;
            if (part == "..") {
                const size_t slash = out.find_last_of('/');
                out.erase(slash == std::string::npos ? 0 : slash);
                continue;
            }
            out += "/" + part;
        }
        return out.empty() ? "/" : out;
    }

    // True when `inner` is `outer` itself or lies below it.
    bool IsSameOrInside(const std::string& inner, const std::string& outer) {
        if (inner == outer) return true;
        if (outer == "/") return true;
        return inner.size() > outer.size() &&
               inner.compare(0, outer.size(), outer) == 0 &&
               inner[outer.size()] == '/';
    }

    std::string HomeDir() {
        if (const char* home = std::getenv("HOME"); home && *home) return home;
        if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_dir) return pw->pw_dir;
        return {};
    }

    // $XDG_DATA_HOME/Trash; a relative XDG_DATA_HOME is invalid by the Base
    // Directory specification and ignored.
    std::string HomeTrashDir() {
        if (const char* data = std::getenv("XDG_DATA_HOME"); data && data[0] == '/')
            return StripTrailingSlashes(data) + "/Trash";
        const std::string home = HomeDir();
        return home.empty() ? std::string() : StripTrailingSlashes(home) + "/.local/share/Trash";
    }

    // Creates `dir` and every missing folder above it.
    bool MakeDirectories(const std::string& dir, mode_t mode) {
        struct stat st;
        if (stat(dir.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
        if (dir != "/" && !MakeDirectories(ParentOf(dir), mode)) return false;
        return mkdir(dir.c_str(), mode) == 0 || errno == EEXIST;
    }

    // The device of `path`, or of its nearest existing ancestor - the home
    // trash need not exist before the first file goes into it.
    bool DeviceOf(std::string path, dev_t& device) {
        for (;;) {
            struct stat st;
            if (stat(path.c_str(), &st) == 0) {
                device = st.st_dev;
                return true;
            }
            if (path == "/" || path.empty()) return false;
            path = ParentOf(path);
        }
    }

    // The top directory (mount point) of the file system `path` lives on:
    // the highest ancestor still on `device`.
    std::string TopDirOf(const std::string& path, dev_t device) {
        std::string current = ParentOf(path);
        while (current != "/") {
            const std::string up = ParentOf(current);
            struct stat st;
            if (stat(up.c_str(), &st) != 0 || st.st_dev != device) return current;
            current = up;
        }
        return "/";
    }

    // A trash directory we may use: a real directory (not a link to one)
    // that belongs to this user.
    bool IsOwnDirectory(const std::string& dir) {
        struct stat st;
        return lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
               st.st_uid == getuid();
    }

    // The trash of the drive whose top directory is `topdir`.
    std::string TopDirTrash(const std::string& topdir, std::string& error) {
        const std::string uid = std::to_string(static_cast<unsigned long>(getuid()));
        // (1) An administrator's shared $topdir/.Trash: a real directory with
        // the sticky bit, in which each user keeps a folder named by uid. The
        // specification requires ignoring it when either check fails.
        const std::string shared = JoinPath(topdir, ".Trash");
        struct stat st;
        if (lstat(shared.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
            (st.st_mode & S_ISVTX)) {
            const std::string mine = shared + "/" + uid;
            if ((mkdir(mine.c_str(), 0700) == 0 || errno == EEXIST) && IsOwnDirectory(mine))
                return mine;
        }
        // (2) This user's own $topdir/.Trash-$uid.
        const std::string own = JoinPath(topdir, ".Trash-" + uid);
        if (mkdir(own.c_str(), 0700) != 0 && errno != EEXIST) {
            error = "the drive has no trash, and one cannot be made there (" +
                    ErrnoText(errno) + ")";
            return {};
        }
        if (!IsOwnDirectory(own)) {
            error = "the drive's trash folder \"" + own + "\" is not usable";
            return {};
        }
        return own;
    }

    // The .trashinfo Path= value: percent-encoded as a URL path, bytes and all
    // (a UTF-8 name is encoded byte by byte, as the specification asks).
    std::string PercentEncodePath(const std::string& path) {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(path.size() + 16);
        for (unsigned char c : path) {
            const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                              c == '.' || c == '~' || c == '/';
            if (keep) {
                out += static_cast<char>(c);
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 0x0F];
            }
        }
        return out;
    }

    // DeletionDate=: local time, "YYYY-MM-DDThh:mm:ss".
    std::string DeletionDate() {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char buffer[32];
        std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%S", &local);
        return buffer;
    }

    // The n-th candidate name for `base` in the trash: "Report.pdf",
    // "Report.2.pdf", ... A leading dot is part of the name, not an extension.
    std::string CandidateName(const std::string& base, int n) {
        if (n == 1) return base;
        const size_t dot = base.find_last_of('.');
        if (dot == std::string::npos || dot == 0)
            return base + "." + std::to_string(n);
        return base.substr(0, dot) + "." + std::to_string(n) + base.substr(dot);
    }

    bool WriteAll(int fd, const std::string& text) {
        size_t done = 0;
        while (done < text.size()) {
            const ssize_t n = write(fd, text.data() + done, text.size() - done);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            done += static_cast<size_t>(n);
        }
        return true;
    }

    // Claims a free name in `trash` for `base` by creating its .trashinfo with
    // O_EXCL and writing it. `outName` receives the claimed name.
    bool ClaimTrashName(const std::string& trash, const std::string& base,
                        const std::string& pathValue, std::string& outName,
                        std::string& error) {
        const std::string info = "[Trash Info]\nPath=" + pathValue +
                                 "\nDeletionDate=" + DeletionDate() + "\n";
        for (int n = 1; n < 100000; ++n) {
            const std::string name = CandidateName(base, n);
            const std::string infoPath = trash + "/info/" + name + ".trashinfo";
            const int fd = open(infoPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (fd < 0) {
                if (errno == EEXIST) continue;
                error = "cannot write to the trash (" + ErrnoText(errno) + ")";
                return false;
            }
            // A stray in files/ without its info (left by a crash) still
            // occupies the name.
            struct stat st;
            if (lstat((trash + "/files/" + name).c_str(), &st) == 0) {
                close(fd);
                unlink(infoPath.c_str());
                continue;
            }
            const bool written = WriteAll(fd, info);
            const int err = errno;
            close(fd);
            if (!written) {
                unlink(infoPath.c_str());
                error = "cannot write to the trash (" + ErrnoText(err) + ")";
                return false;
            }
            outName = name;
            return true;
        }
        error = "the trash has no free name left for \"" + base + "\"";
        return false;
    }

} // namespace

    bool NativeMoveToTrash(const std::string& path, std::string& error) {
        const std::string abs = AbsolutePath(path);
        if (abs.empty() || abs == "/") {
            error = "that cannot be moved to the trash";
            return false;
        }
        struct stat st;
        if (lstat(abs.c_str(), &st) != 0) {
            error = ErrnoText(errno);
            return false;
        }

        const std::string homeTrash = HomeTrashDir();
        dev_t homeDevice = 0;
        const bool haveHome = !homeTrash.empty() && DeviceOf(homeTrash, homeDevice);

        std::string trash, pathValue;
        if (haveHome && homeDevice == st.st_dev) {
            trash = homeTrash;
            pathValue = PercentEncodePath(abs);
        } else {
            const std::string topdir = TopDirOf(abs, st.st_dev);
            trash = TopDirTrash(topdir, error);
            if (trash.empty()) return false;
            const std::string relative = topdir == "/" ? abs.substr(1)
                                                       : abs.substr(topdir.size() + 1);
            pathValue = PercentEncodePath(relative);
        }

        // Never the trash itself, anything already in it, or a folder that
        // holds it (the home folder holds the home trash).
        if (IsSameOrInside(abs, trash)) {
            error = "it is already in the trash";
            return false;
        }
        if (IsSameOrInside(trash, abs)) {
            error = "it contains the trash itself";
            return false;
        }

        if (!MakeDirectories(trash + "/files", 0700) ||
            !MakeDirectories(trash + "/info", 0700)) {
            error = "cannot create the trash folder \"" + trash + "\" (" +
                    ErrnoText(errno) + ")";
            return false;
        }

        std::string name;
        if (!ClaimTrashName(trash, BaseNameOf(abs), pathValue, name, error))
            return false;

        if (rename(abs.c_str(), (trash + "/files/" + name).c_str()) != 0) {
            const int err = errno;
            unlink((trash + "/info/" + name + ".trashinfo").c_str());
            error = ErrnoText(err);
            return false;
        }
        return true;
    }

} // namespace UltraCanvas
