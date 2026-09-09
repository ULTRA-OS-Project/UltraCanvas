// OS/Linux/UltraCanvasLinuxFileLock.cpp
// Linux backend for UltraCanvasFileLock.
//
// Linux does not work the way the question is usually asked. An open file
// blocks nothing: it can be written, renamed and deleted while another
// process reads it, and that process keeps reading the old contents. So a
// file here is almost never `Locked` - what the display is really reporting
// is `OpenElsewhere`, which is information ("something is reading this"),
// not an obstacle.
//
// Two sources answer it, both read-only and both once per batch:
//
//   /proc/locks   every POSIX / OFD / flock lock in the system, by device and
//                 inode. An advisory lock (all but unheard-of mandatory ones)
//                 stops nothing, so it reports OpenElsewhere; a MANDATORY
//                 write lock really does block a write, and reports Locked.
//   /proc/<pid>/fd  which processes have the file open. Only processes of
//                 this user are readable without privileges - a file open
//                 only by another user's process reads as Free, which is why
//                 nothing here is ever presented as a guarantee.
//
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasFileLock.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>

#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace UltraCanvas {

    namespace {

        // A file as the kernel names it in /proc/locks: device + inode, which
        // survives the path having several names.
        struct FileId {
            unsigned long major = 0;
            unsigned long minor = 0;
            unsigned long inode = 0;
            bool operator<(const FileId& o) const {
                if (major != o.major) return major < o.major;
                if (minor != o.minor) return minor < o.minor;
                return inode < o.inode;
            }
        };

        struct LockRecord {
            bool mandatoryWrite = false;   // actually blocks a write
            bool any = false;              // some program declared it busy
            std::string holderPid;         // the pid the line names, if any
        };

        // The name a pid runs under, for the holder list. /proc/<pid>/comm is
        // readable whoever owns the process, so a lock holder can be named
        // even where its open files cannot be listed.
        std::string ProcessName(const std::string& pid) {
            std::ifstream in("/proc/" + pid + "/comm");
            std::string name;
            if (in && std::getline(in, name) && !name.empty())
                return name + " (" + pid + ")";
            return "pid " + pid;
        }

        // Everything /proc/locks knows, by file. Read once per batch: the file
        // is a few lines on a desktop and re-reading it per entry of a folder
        // listing would be the expensive way to ask the same question.
        std::map<FileId, LockRecord> ReadSystemLocks() {
            std::map<FileId, LockRecord> locks;
            std::ifstream in("/proc/locks");
            if (!in) return locks;

            std::string line;
            while (std::getline(in, line)) {
                // 1: POSIX  ADVISORY  WRITE 1234 08:02:9876543 0 EOF
                std::istringstream ls(line);
                std::string index, kind, mode, access, pidText, target;
                if (!(ls >> index >> kind >> mode >> access >> pidText >> target))
                    continue;

                FileId id;
                if (std::sscanf(target.c_str(), "%lx:%lx:%lu",
                                &id.major, &id.minor, &id.inode) != 3)
                    continue;

                LockRecord& rec = locks[id];
                rec.any = true;
                if (mode == "MANDATORY" && access == "WRITE")
                    rec.mandatoryWrite = true;
                if (rec.holderPid.empty() && pidText != "-1" && pidText != "0")
                    rec.holderPid = pidText;
            }
            return locks;
        }

        // The absolute, symlink-resolved path, which is what /proc/<pid>/fd
        // links read back as. Falls back to the input so a path that cannot be
        // resolved is still compared rather than silently dropped.
        std::string Canonical(const std::string& path) {
            char buf[PATH_MAX];
            if (::realpath(path.c_str(), buf)) return std::string(buf);
            return path;
        }

        bool IsAllDigits(const char* s) {
            if (!s || !*s) return false;
            for (const char* p = s; *p; ++p)
                if (*p < '0' || *p > '9') return false;
            return true;
        }

        // Which of `wanted` some OTHER process has open, and who. One walk of
        // /proc for the whole batch; this process is skipped, so a file the
        // caller itself is reading (a thumbnail, a preview) is not reported
        // back to the caller as busy.
        void CollectOpenFiles(const std::unordered_map<std::string, size_t>& wanted,
                              bool wantHolders,
                              std::vector<FileLockInfo>& out) {
            DIR* proc = ::opendir("/proc");
            if (!proc) return;

            const std::string self = std::to_string(::getpid());
            char link[PATH_MAX];

            while (dirent* entry = ::readdir(proc)) {
                if (!IsAllDigits(entry->d_name)) continue;
                const std::string pid = entry->d_name;
                if (pid == self) continue;

                const std::string fdDir = "/proc/" + pid + "/fd";
                DIR* fds = ::opendir(fdDir.c_str());
                if (!fds) continue;   // another user's process: not readable

                while (dirent* fd = ::readdir(fds)) {
                    if (!IsAllDigits(fd->d_name)) continue;
                    const std::string fdPath = fdDir + "/" + fd->d_name;
                    ssize_t n = ::readlink(fdPath.c_str(), link, sizeof(link) - 1);
                    if (n <= 0) continue;
                    link[n] = '\0';

                    auto it = wanted.find(std::string(link));
                    if (it == wanted.end()) continue;

                    FileLockInfo& info = out[it->second];
                    if (info.state != FileLockState::Locked)
                        info.state = FileLockState::OpenElsewhere;
                    if (wantHolders) {
                        std::string name = ProcessName(pid);
                        bool known = false;
                        for (const std::string& h : info.holders)
                            if (h == name) { known = true; break; }
                        if (!known) info.holders.push_back(std::move(name));
                    }
                }
                ::closedir(fds);
            }
            ::closedir(proc);
        }

    } // namespace

    bool NativeProbeFileLocks(const std::vector<std::string>& paths,
                              bool wantHolders,
                              std::vector<FileLockInfo>& out) {
        out.assign(paths.size(), FileLockInfo{});
        if (paths.empty()) return true;

        const std::map<FileId, LockRecord> locks = ReadSystemLocks();

        // Regular files that exist get an answer; a directory is left Unknown
        // (what holds one is its users' current directories, which this does
        // not look at) and so is anything that vanished under the probe.
        std::unordered_map<std::string, size_t> wanted;
        wanted.reserve(paths.size());
        for (size_t i = 0; i < paths.size(); ++i) {
            if (paths[i].empty()) continue;
            struct stat st{};
            if (::stat(paths[i].c_str(), &st) != 0) continue;
            if (!S_ISREG(st.st_mode)) continue;

            out[i].state = FileLockState::Free;
            wanted.emplace(Canonical(paths[i]), i);

            if (locks.empty()) continue;
            FileId id;
            id.major = ::major(st.st_dev);
            id.minor = ::minor(st.st_dev);
            id.inode = static_cast<unsigned long>(st.st_ino);
            auto it = locks.find(id);
            if (it == locks.end()) continue;

            if (it->second.mandatoryWrite) {
                out[i].state = FileLockState::Locked;
                out[i].writeBlocked = true;
            } else if (it->second.any) {
                // An advisory lock: a declaration of intent that the kernel
                // enforces on nobody. Worth showing, not worth warning about.
                out[i].state = FileLockState::OpenElsewhere;
            }
            // Named here, deduplicated by CollectOpenFiles below: a process
            // that holds a lock usually also has the file open, and saying so
            // twice reads as two programs.
            if (wantHolders && !it->second.holderPid.empty())
                out[i].holders.push_back(ProcessName(it->second.holderPid));
        }

        if (!wanted.empty()) CollectOpenFiles(wanted, wantHolders, out);
        return true;
    }

} // namespace UltraCanvas
