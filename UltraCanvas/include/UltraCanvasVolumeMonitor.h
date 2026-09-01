// include/UltraCanvasVolumeMonitor.h
// The mounted volumes of this machine, and a notification when that set
// changes: a USB stick plugged in or pulled out, a card reader, an optical
// disc, a network share mapped or dropped, a disk image attached.
//
//   for (const MountedVolume& v : ListMountedVolumes())
//       AddDriveRow(v.path, v.label);
//
//   UltraCanvasVolumeMonitor monitor;
//   monitor.Start([this]() { volumesDirty.store(true); });  // background thread!
//
// Two halves that are useful apart: ListMountedVolumes() answers "what is
// mounted right now" and is the single enumeration the whole framework uses
// (the folder tree of a file manager, the "Computer" dropdown of a path
// strip, a save dialog's places list), while the monitor says when to ask
// again. A caller that only ever lists at start-up needs the first alone.
//
// The monitor's callback runs on the monitor's own thread (on macOS, on the
// main thread), so it must do nothing but hand the news over - set an atomic,
// post to the UI thread - and return. One insertion can produce several
// callbacks; the receiver coalesces.
//
// Backends: Linux/BSD (poll() on /proc/self/mountinfo), Windows
// (WM_DEVICECHANGE on a hidden top-level window), macOS (NSWorkspace mount
// notifications). Where none exists the monitor polls the volume list on a
// background thread instead, so Start() succeeds on every platform and the
// caller never needs a fallback of its own.
// Version: 1.0.0
// Last Modified: 2026-09-01
// Author: UltraCanvas Framework
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {

    // ===== WHAT IS MOUNTED =====

    // One mounted volume: where it is reachable and what to call it.
    struct MountedVolume {
        // Mount point / drive root: "C:\\", "/", "/media/bob/USB STICK".
        std::string path;
        // Display name derived from the path: the drive letter with its colon
        // on Windows ("C:"), the mount point's own name elsewhere
        // ("USB STICK"). The system root is labelled "/" - an application that
        // calls it something friendlier ("File System", "Computer") supplies
        // that word itself rather than having it baked in here.
        std::string label;
        // The volume the operating system is running from ("/", or the drive
        // holding the Windows directory). Never removable, always present.
        bool isSystemRoot = false;
    };

    // Every volume mounted right now, system root first, the rest sorted by
    // path. One pass over the mount table (Windows) or over the small set of
    // directories volumes are mounted under (elsewhere) - never a probe of
    // every drive letter, which spins up empty optical drives and waits out
    // the timeout of each disconnected network mapping.
    std::vector<MountedVolume> ListMountedVolumes();

    // Just the mount points of the above, in the same order. The shape a
    // caller that only navigates - a path strip's drive dropdown - wants.
    std::vector<std::string> ListVolumeRoots();

    // ===== PLATFORM MOUNT TABLE (implemented per platform under OS/<Platform>/) =====
    // The mount points the operating system's own mount table lists, or an
    // empty set where this platform has no readable one. Not the public
    // answer to "what is mounted" - that is ListMountedVolumes(), which uses
    // this to decide whether a directory it found is really a mount point.
    //
    // The fallback test - a directory whose device differs from its parent's -
    // cannot see a mount that shares a device with what it is mounted on, and
    // a bind mount from the same filesystem is exactly that. It would then be
    // listed as an ordinary folder and left out of the drive rows.
    std::set<std::string> ListPlatformMountPoints();

    // ===== BACKEND INTERFACE (implemented per platform under OS/<Platform>/) =====
    // Not part of the public surface: callers use UltraCanvasVolumeMonitor.
    class IVolumeMonitorBackend {
    public:
        virtual ~IVolumeMonitorBackend() = default;
        // Begin reporting. `onChanged` is invoked from the backend's own
        // thread (the main thread on macOS) whenever something mounts or
        // unmounts. False when the platform's notification cannot be set up -
        // the monitor then polls instead.
        virtual bool Start(std::function<void()> onChanged) = 0;
        // Stop and join. Safe to call twice, and safe if Start() failed. The
        // callback never runs after this returns.
        virtual void Stop() = 0;
    };

    // Provided by the platform backend where one exists; the core file
    // supplies a null-returning definition on every other platform.
    std::unique_ptr<IVolumeMonitorBackend> CreateNativeVolumeMonitorBackend();

    // ===== THE MONITOR =====
    class UltraCanvasVolumeMonitor {
    public:
        using ChangedCallback = std::function<void()>;

        UltraCanvasVolumeMonitor();
        ~UltraCanvasVolumeMonitor();

        UltraCanvasVolumeMonitor(const UltraCanvasVolumeMonitor&) = delete;
        UltraCanvasVolumeMonitor& operator=(const UltraCanvasVolumeMonitor&) = delete;

        // Report mounts and unmounts to `onChanged` until Stop(). Uses the
        // platform's own notification where there is one and a polling thread
        // where there is not, so this only fails on a missing callback.
        bool Start(ChangedCallback onChanged);

        // Stop reporting. Joins whatever is running, so the callback never
        // runs after this returns - which is what makes it safe for the
        // callback to capture the caller. Safe to call twice.
        void Stop();

        bool IsRunning() const { return running; }
        // True while the operating system is doing the reporting rather than
        // the fallback thread re-listing the volumes.
        bool IsNative() const { return native; }

        // How often the fallback thread re-lists the volumes (default 2000;
        // values below 250 are raised to it). Ignored while a native backend
        // is running. Takes effect on the next Start().
        void SetPollIntervalMs(int ms);
        int  GetPollIntervalMs() const { return pollIntervalMs; }

        // Whether this build has a native backend at all. False means every
        // Start() polls.
        static bool NativeBackendAvailable();

    private:
        void PollLoop();
        // Cheap fingerprint of the volume list: two mounts differing in any
        // path or count give different numbers.
        static unsigned long long VolumeSignature();

        std::unique_ptr<IVolumeMonitorBackend> backend;
        ChangedCallback callback;
        // The fallback: one thread comparing the volume list against the last
        // one it saw, woken early by the condition variable so Stop() does not
        // wait out an interval.
        std::thread             pollWorker;
        std::mutex              pollMutex;
        std::condition_variable pollCond;
        bool                    pollShutdown = false;
        bool running = false;
        bool native = false;
        int  pollIntervalMs = 2000;
    };

} // namespace UltraCanvas
