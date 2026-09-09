// core/UltraCanvasVolumeMonitor.cpp
// Platform-independent half of the volume monitor: which volumes are mounted,
// the lifetime of a monitor, and the polling fallback for platforms with no
// native notification. The only operating system calls here are the ones the
// mount test needs (a stat, or GetLogicalDrives on Windows); everything that
// reports a *change* lives in OS/<Platform>/.
//
// Why the enumeration is here and not in each caller: the folder tree of a
// file manager and the "Computer" dropdown of a path strip used to answer the
// question separately and disagreed - one scanned /media and /mnt, the other
// /media, /mnt and /Volumes, and neither looked at /run/media, where udisks2
// mounts on Fedora, RHEL, Arch and openSUSE. A stick was then missing from
// one list, from both, or shown in one and not the other.
// Version: 1.0.0
// Last Modified: 2026-09-01
// Author: UltraCanvas Framework

#include "UltraCanvasVolumeMonitor.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace UltraCanvas {

    namespace fs = std::filesystem;

    namespace {

#ifndef _WIN32
        // Identity of a directory: the pair every filesystem guarantees is
        // unique for a live file. Used both to decide whether something is a
        // mount point and to keep a volume reachable under two names (a
        // /media symlinked to /run/media) from being listed twice.
        struct DirIdentity {
            dev_t device = 0;
            ino_t inode = 0;
            bool valid = false;
            bool operator<(const DirIdentity& o) const {
                return device != o.device ? device < o.device : inode < o.inode;
            }
        };

        DirIdentity IdentityOf(const std::string& path) {
            DirIdentity id;
            struct stat st {};
            // lstat, not stat: a symlink left behind in /media must not be
            // resolved into the volume it once pointed at.
            if (::lstat(path.c_str(), &st) != 0) return id;
            id.device = st.st_dev;
            id.inode = st.st_ino;
            id.valid = true;
            return id;
        }

        // True when `path` is really a mount point. Keeps the empty
        // placeholder directories that udisks and hand-made /mnt entries
        // leave behind from being offered as drives that lead nowhere.
        //
        // Two tests, either of which is enough. The mount table is exact but
        // is not readable on every platform; the device comparison needs no
        // table at all but cannot see a mount that shares a device with what
        // it is mounted on (a bind mount from the same filesystem). Neither
        // produces a false positive - a differing device IS a mount - so the
        // union of the two is strictly better than either.
        bool IsMountPointDir(const std::string& path, const DirIdentity& id,
                             const std::set<std::string>& table) {
            if (!id.valid) return false;
            if (table.count(path)) return true;
            const std::string up = fs::path(path).parent_path().string();
            if (up.empty()) return false;
            const DirIdentity parent = IdentityOf(up);
            return parent.valid && parent.device != id.device;
        }

        // The directories volumes appear under, in the order a user expects
        // to see them. /run/media is where udisks2 mounts on Fedora, RHEL,
        // Arch and openSUSE; /media is the Debian/Ubuntu location; /Volumes is
        // macOS; /mnt is where mounts are made by hand.
        //
        // `nested` marks a base that holds one directory per user with the
        // volumes below it (/media/bob/USB STICK) as well as volumes directly
        // in it (/media/USB STICK) - both spellings are in use.
        struct VolumeBase { const char* path; bool nested; };
        constexpr VolumeBase kVolumeBases[] = {
            {"/media",     true},
            {"/run/media", true},
            {"/Volumes",   false},
            {"/mnt",       false},
        };

        // Subdirectories of `path`, cheaply and without throwing. Hidden
        // entries are kept: a volume labelled ".backup" is still a volume.
        std::vector<fs::path> Subdirectories(const std::string& path) {
            std::vector<fs::path> dirs;
            std::error_code ec;
            fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
            if (ec) return dirs;
            for (fs::directory_iterator end; it != end; it.increment(ec)) {
                if (ec) break;
                std::error_code dec;
                if (it->is_directory(dec) && !dec) dirs.push_back(it->path());
            }
            return dirs;
        }
#endif

    } // namespace

    // ===== WHAT IS MOUNTED =====

    std::vector<MountedVolume> ListMountedVolumes() {
        std::vector<MountedVolume> volumes;

#ifdef _WIN32
        // GetLogicalDrives() answers from the mount table alone: one call, no
        // per-letter media or network access. Probing "A:\" ... "Z:\" with
        // exists() instead spins up empty optical / card readers and waits out
        // the SMB timeout of every disconnected mapped drive - seconds of
        // stall, on a list that is rebuilt on every navigation.
        //
        // The volume's own label is deliberately NOT read here: GetVolume-
        // InformationW touches the medium, which is exactly the stall this
        // avoids. The letter is the name Windows itself sorts by anyway.
        wchar_t systemDir[MAX_PATH] = {};
        wchar_t systemLetter = 0;
        if (::GetSystemDirectoryW(systemDir, MAX_PATH) > 0)
            systemLetter = systemDir[0];

        const DWORD mask = ::GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (!(mask & (DWORD(1) << i))) continue;
            const char letter = char('A' + i);
            MountedVolume v;
            v.path = std::string(1, letter) + ":\\";
            v.label = std::string(1, letter) + ":";
            // GetSystemDirectoryW answers "C:\Windows\system32"; accept
            // either case for the letter rather than pulling in <cwctype>.
            v.isSystemRoot = systemLetter == wchar_t(letter) ||
                             systemLetter == wchar_t(letter - 'A' + 'a');
            volumes.push_back(std::move(v));
        }
        // The system drive first, the rest in letter order (already the case).
        std::stable_partition(volumes.begin(), volumes.end(),
                              [](const MountedVolume& v) { return v.isSystemRoot; });
#else
        // The root is always a volume and always first: everything else is
        // reachable through it.
        MountedVolume root;
        root.path = "/";
        root.label = "/";
        root.isSystemRoot = true;
        volumes.push_back(std::move(root));

        // Read once for the whole pass: on Linux this is a file, and asking
        // for it per candidate directory would re-read it dozens of times.
        const std::set<std::string> table = ListPlatformMountPoints();

        std::set<DirIdentity> seen;
        std::vector<MountedVolume> mounts;
        for (const VolumeBase& base : kVolumeBases) {
            for (const fs::path& entry : Subdirectories(base.path)) {
                const std::string path = entry.string();
                const DirIdentity id = IdentityOf(path);
                if (IsMountPointDir(path, id, table)) {
                    if (!seen.insert(id).second) continue;
                    MountedVolume v;
                    v.path = path;
                    v.label = entry.filename().string();
                    mounts.push_back(std::move(v));
                    continue;
                }
                if (!base.nested) continue;
                // Not itself a mount: on a nested base this is the per-user
                // directory (/media/bob), and the volumes are one level down.
                for (const fs::path& sub : Subdirectories(path)) {
                    const std::string subPath = sub.string();
                    const DirIdentity subId = IdentityOf(subPath);
                    if (!IsMountPointDir(subPath, subId, table)) continue;
                    if (!seen.insert(subId).second) continue;
                    MountedVolume v;
                    v.path = subPath;
                    v.label = sub.filename().string();
                    mounts.push_back(std::move(v));
                }
            }
        }
        std::sort(mounts.begin(), mounts.end(),
                  [](const MountedVolume& a, const MountedVolume& b) {
            return a.path < b.path;
        });
        volumes.insert(volumes.end(), std::make_move_iterator(mounts.begin()),
                       std::make_move_iterator(mounts.end()));
#endif
        return volumes;
    }

    std::vector<std::string> ListVolumeRoots() {
        std::vector<std::string> roots;
        for (const MountedVolume& v : ListMountedVolumes())
            roots.push_back(v.path);
        return roots;
    }

    // ===== THE MONITOR =====

#ifndef ULTRACANVAS_HAS_NATIVE_VOLUME_MONITOR
    // No backend on this platform: Start() runs the polling thread instead,
    // and the mount test falls back to comparing devices. The platforms that
    // do have one define both of these in their own file and CMake sets
    // ULTRACANVAS_HAS_NATIVE_VOLUME_MONITOR there.
    std::unique_ptr<IVolumeMonitorBackend> CreateNativeVolumeMonitorBackend() {
        return nullptr;
    }

    std::set<std::string> ListPlatformMountPoints() {
        return {};
    }
#endif

    UltraCanvasVolumeMonitor::UltraCanvasVolumeMonitor() = default;

    UltraCanvasVolumeMonitor::~UltraCanvasVolumeMonitor() {
        Stop();
    }

    bool UltraCanvasVolumeMonitor::NativeBackendAvailable() {
#ifdef ULTRACANVAS_HAS_NATIVE_VOLUME_MONITOR
        return true;
#else
        return false;
#endif
    }

    void UltraCanvasVolumeMonitor::SetPollIntervalMs(int ms) {
        pollIntervalMs = ms < 250 ? 250 : ms;
    }

    unsigned long long UltraCanvasVolumeMonitor::VolumeSignature() {
        // FNV-1a over every mount point, so a volume appearing, disappearing
        // or being remounted under another name all change the number. Order
        // is fixed by ListMountedVolumes(), so equal sets hash equal.
        unsigned long long h = 1469598103934665603ull;
        for (const MountedVolume& v : ListMountedVolumes()) {
            for (unsigned char c : v.path) h = (h ^ c) * 1099511628211ull;
            h = (h ^ 0xffu) * 1099511628211ull;   // separator: "a","b" != "ab"
        }
        return h;
    }

    bool UltraCanvasVolumeMonitor::Start(ChangedCallback onChanged) {
        Stop();
        if (!onChanged) return false;
        callback = std::move(onChanged);

        // The operating system's own notification where there is one: a mount
        // is then seen the moment it happens and an idle machine costs
        // nothing. A backend that cannot start (no /proc, no window station)
        // is not a failure - it falls through to the same polling every
        // backend-less platform uses.
        backend = CreateNativeVolumeMonitorBackend();
        if (backend) {
            native = backend->Start([this]() { if (callback) callback(); });
            if (!native) backend.reset();
        }

        if (!native) {
            std::lock_guard<std::mutex> lk(pollMutex);
            pollShutdown = false;
            pollWorker = std::thread([this]() { PollLoop(); });
        }
        running = true;
        return true;
    }

    void UltraCanvasVolumeMonitor::Stop() {
        if (backend) {
            backend->Stop();   // joins its thread: no callback survives this
            backend.reset();
        }
        if (pollWorker.joinable()) {
            {
                std::lock_guard<std::mutex> lk(pollMutex);
                pollShutdown = true;
            }
            pollCond.notify_all();
            pollWorker.join();
        }
        callback = nullptr;
        running = false;
        native = false;
    }

    void UltraCanvasVolumeMonitor::PollLoop() {
        // Read once, here: the interval is documented as taking effect on the
        // next Start(), and reading the member every pass would be a race with
        // a caller that changed it anyway.
        const int interval = pollIntervalMs;
        // The list as it was when the caller started watching: the first pass
        // must not report a change that happened before anyone was looking.
        unsigned long long known = VolumeSignature();
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(pollMutex);
                pollCond.wait_for(lk, std::chrono::milliseconds(interval),
                                  [this]() { return pollShutdown; });
                if (pollShutdown) return;
            }
            const unsigned long long now = VolumeSignature();
            if (now == known) continue;
            known = now;
            if (callback) callback();
        }
    }

} // namespace UltraCanvas
