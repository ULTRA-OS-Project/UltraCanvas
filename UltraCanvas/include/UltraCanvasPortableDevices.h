// include/UltraCanvasPortableDevices.h
// Phones, cameras and media players plugged in over USB that are NOT a drive:
// a device that speaks MTP or PTP gets no drive letter and no mount point, so
// nothing that lists mounted volumes (UltraCanvasVolumeMonitor.h) ever sees
// it. Windows Explorer reaches such a device through Windows Portable Devices
// (WPD); this is the same access for an UltraCanvas application - which
// devices are connected, what is in a folder on one, and copying files off
// it and onto it.
//
//   for (const PortableDeviceInfo& d : UltraCanvasPortableDevices::List())
//       AddDeviceRow(d.id, d.name, d.kind);
//
//   std::vector<PortableDeviceEntry> entries;
//   std::string error;
//   UltraCanvasPortableDevices::ListFolder(id, "/Internal shared storage/DCIM",
//                                          entries, error);
//
// A device is addressed by paths, the way a disk is: "/" is the device, its
// folders are the storages it offers ("Internal shared storage", "SD card"),
// and below them the folders and files as the device names them. WPD itself
// knows objects by id, not by path; the backend resolves a path one name at
// a time and keeps what it learned, so a folder opened twice is not walked
// twice.
//
// Every call blocks until the device has answered, which over USB can take a
// while (a DCIM folder of thousands of photos, a large video), so a caller
// runs them off its UI thread - as UltraFiler's drive worker does. Calls are
// safe from any thread; one device answers one request at a time, and so
// does the backend.
//
// UltraCanvasPortableDeviceMonitor reports devices being plugged in and
// pulled out, on a thread of its own, like the volume monitor.
//
// Backends: Windows (WPD - the platform's own MTP / PTP access, built in
// since Windows 7, no extra library). Elsewhere Available() is false, List()
// is empty and every call fails with a message saying so: Linux desktops
// mount such a device themselves (GNOME's gvfs under /run/user/<uid>/gvfs,
// which ListMountedVolumes reports), and macOS has no MTP support of its own.
// Version: 1.1.0 - GetSpace: a device's capacity and free space
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {

    // What a device says it is, for the icon a file manager draws.
    enum class PortableDeviceKind {
        Phone,          // a phone (MTP)
        Camera,         // a still or video camera (PTP)
        MediaPlayer,    // a music or video player
        Other           // anything else, or a device that did not say
    };

    // One connected device.
    struct PortableDeviceInfo {
        // How every call names the device: stable while it stays plugged in,
        // and the same the next time the same device is plugged into the same
        // port (WPD's device interface path). Not for showing.
        std::string id;
        // What to call it: "realme 12 Pro+ 5G", "Canon EOS R6".
        std::string name;
        std::string manufacturer;
        std::string description;
        PortableDeviceKind kind = PortableDeviceKind::Other;
    };

    // One file or folder on a device.
    struct PortableDeviceEntry {
        std::string name;
        bool isDirectory = false;
        uint64_t size = 0;            // 0 for a folder, or when the device did not say
        std::time_t modifiedTime = 0; // seconds since the epoch, UTC; 0 when unknown
    };

    // Asked as a transfer moves: bytes done, total (0 when not known).
    // Returning false calls the transfer off; the call then fails with
    // "cancelled".
    using PortableDeviceProgress = std::function<bool(uint64_t done, uint64_t total)>;

    // ===== BACKEND INTERFACE (implemented per platform under OS/<Platform>/) =====
    // Not part of the public surface: callers use UltraCanvasPortableDevices.
    // Paths reach a backend normalised - a leading '/', no trailing one, no
    // empty segments (NormalizePortableDevicePath).
    class IPortableDeviceBackend {
    public:
        virtual ~IPortableDeviceBackend() = default;
        virtual std::vector<PortableDeviceInfo> List() = 0;
        virtual bool ListFolder(const std::string& deviceId, const std::string& path,
                                std::vector<PortableDeviceEntry>& out, std::string& error) = 0;
        virtual bool Download(const std::string& deviceId, const std::string& path,
                              const std::string& localFile, const PortableDeviceProgress& progress,
                              std::string& error) = 0;
        virtual bool Upload(const std::string& deviceId, const std::string& localFile,
                            const std::string& path, const PortableDeviceProgress& progress,
                            std::string& error) = 0;
        virtual bool MakeFolder(const std::string& deviceId, const std::string& path,
                                std::string& error) = 0;
        virtual bool Delete(const std::string& deviceId, const std::string& path,
                            std::string& error) = 0;
        virtual bool Rename(const std::string& deviceId, const std::string& path,
                            const std::string& newName, std::string& error) = 0;
        virtual bool Space(const std::string& deviceId, uint64_t& totalBytes,
                           uint64_t& freeBytes, std::string& error) = 0;
    };

    // Provided by the platform backend where one exists; the core file
    // supplies a null-returning definition on every other platform.
    std::unique_ptr<IPortableDeviceBackend> CreatePortableDeviceBackend();

    // "/" plus the path's names joined by '/': backslashes read as '/', and
    // empty segments and a trailing '/' dropped. "" and "/" are the device
    // itself.
    std::string NormalizePortableDevicePath(const std::string& path);
    // The names of a path, outermost first: "/a/b" -> {"a", "b"}; "/" -> {}.
    std::vector<std::string> SplitPortableDevicePath(const std::string& path);

    // ===== THE DEVICES =====
    class UltraCanvasPortableDevices {
    public:
        // Whether this build can reach portable devices at all.
        static bool Available();

        // The devices connected right now. Empty where there is no backend.
        static std::vector<PortableDeviceInfo> List();

        // What is in the folder `path` of the device: "/" lists its storages.
        // False with a reason when the device is gone, the path names no
        // folder, or the device refused.
        static bool ListFolder(const std::string& deviceId, const std::string& path,
                               std::vector<PortableDeviceEntry>& out, std::string& error);

        // Copies the file `path` off the device into `localFile` (UTF-8),
        // replacing it. A copy that fails part way is removed.
        static bool Download(const std::string& deviceId, const std::string& path,
                             const std::string& localFile, std::string& error,
                             const PortableDeviceProgress& progress = {});

        // Copies the local file `localFile` onto the device as `path` (the
        // folder it goes into must exist). A file of that name already there is
        // replaced - a device would otherwise keep both under one name.
        static bool Upload(const std::string& deviceId, const std::string& localFile,
                           const std::string& path, std::string& error,
                           const PortableDeviceProgress& progress = {});

        // Creates the folder `path`; its parent must exist.
        static bool MakeFolder(const std::string& deviceId, const std::string& path,
                               std::string& error);

        // Deletes the file or folder `path`, a folder with everything in it.
        static bool Delete(const std::string& deviceId, const std::string& path,
                           std::string& error);

        // Renames `path` in place; `newName` is a bare name.
        static bool Rename(const std::string& deviceId, const std::string& path,
                           const std::string& newName, std::string& error);

        // The capacity and free space of the device, its storages added up -
        // a phone's internal storage and its SD card are one device to the
        // user. False with a reason when the device is gone or shows no
        // storage (locked, or on "charging only").
        static bool GetSpace(const std::string& deviceId, uint64_t& totalBytes,
                             uint64_t& freeBytes, std::string& error);
    };

    // ===== THE MONITOR =====
    // Reports a device being plugged in or pulled out. The callback runs on
    // the monitor's own thread, so it must do nothing but hand the news over
    // (set an atomic, post to the UI thread) and return.
    //
    // It compares the connected devices every interval: asking Windows which
    // portable devices are present is a cheap look at its device list - the
    // devices themselves are not opened for it - and needs no window to
    // receive notifications on.
    class UltraCanvasPortableDeviceMonitor {
    public:
        using ChangedCallback = std::function<void()>;

        UltraCanvasPortableDeviceMonitor() = default;
        ~UltraCanvasPortableDeviceMonitor();

        UltraCanvasPortableDeviceMonitor(const UltraCanvasPortableDeviceMonitor&) = delete;
        UltraCanvasPortableDeviceMonitor& operator=(const UltraCanvasPortableDeviceMonitor&) = delete;

        // False when there is no backend (nothing could ever change) or no
        // callback. The first look is taken at once, so a device already
        // connected is not reported as arriving.
        bool Start(ChangedCallback onChanged);
        // Stops and joins: no callback runs after this returns. Safe twice.
        void Stop();
        bool IsRunning() const { return running; }

        // How often the device list is compared (default 2000 ms, at least
        // 250). Takes effect on the next Start().
        void SetPollIntervalMs(int ms) { pollIntervalMs = ms < 250 ? 250 : ms; }

    private:
        std::thread worker;
        std::mutex mutex;
        std::condition_variable wake;
        bool stopping = false;      // under mutex
        bool running = false;
        int pollIntervalMs = 2000;
    };

} // namespace UltraCanvas
