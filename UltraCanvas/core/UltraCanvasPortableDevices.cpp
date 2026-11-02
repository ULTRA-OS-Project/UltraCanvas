// core/UltraCanvasPortableDevices.cpp
// Platform-independent half of the portable-device access: the path rules,
// the one backend every call goes through, the monitor, and the fallback for
// platforms without a backend. Every operating system call lives in
// OS/<Platform>/.
// Version: 1.1.0 - GetSpace
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#include "UltraCanvasPortableDevices.h"

#include <algorithm>
#include <chrono>

namespace UltraCanvas {

#ifndef ULTRACANVAS_HAS_PORTABLE_DEVICES
    // No backend on this platform: List() is empty and every call says why.
    // The platforms that do have one define the symbol in their own file and
    // CMake sets ULTRACANVAS_HAS_PORTABLE_DEVICES there.
    std::unique_ptr<IPortableDeviceBackend> CreatePortableDeviceBackend() {
        return nullptr;
    }
#endif

    std::string NormalizePortableDevicePath(const std::string& path) {
        std::string out = "/";
        std::string segment;
        auto flush = [&]() {
            if (segment.empty()) return;
            if (out.size() > 1) out += '/';
            out += segment;
            segment.clear();
        };
        for (char c : path) {
            if (c == '/' || c == '\\') flush();
            else segment += c;
        }
        flush();
        return out;
    }

    std::vector<std::string> SplitPortableDevicePath(const std::string& path) {
        std::vector<std::string> names;
        std::string segment;
        for (char c : path) {
            if (c == '/' || c == '\\') {
                if (!segment.empty()) names.push_back(std::move(segment));
                segment.clear();
            } else {
                segment += c;
            }
        }
        if (!segment.empty()) names.push_back(std::move(segment));
        return names;
    }

    namespace {
        // One backend for the process: it keeps the devices it opened and
        // what it learned about their paths, and a device answers one
        // request at a time however many callers there are. Made on first
        // use and deliberately never destroyed: the backend owns a thread
        // that talks to the device and the system objects it opened there,
        // and tearing those down in a static destructor - after the process
        // has started ending and that thread is gone - is how a program
        // crashes on its way out. The system reclaims them with the process.
        IPortableDeviceBackend* Backend() {
            static IPortableDeviceBackend* backend = CreatePortableDeviceBackend().release();
            return backend;
        }

        const char* kNoBackend =
                "this system has no portable-device access in UltraCanvas";

        // A bare name for Rename and for the last segment of a path: not
        // empty, not "." or "..", no separator.
        bool IsBareName(const std::string& name) {
            return !name.empty() && name != "." && name != ".." &&
                   name.find('/') == std::string::npos &&
                   name.find('\\') == std::string::npos;
        }
    }

    bool UltraCanvasPortableDevices::Available() {
        return Backend() != nullptr;
    }

    std::vector<PortableDeviceInfo> UltraCanvasPortableDevices::List() {
        IPortableDeviceBackend* b = Backend();
        return b ? b->List() : std::vector<PortableDeviceInfo>{};
    }

    bool UltraCanvasPortableDevices::ListFolder(const std::string& deviceId,
                                                const std::string& path,
                                                std::vector<PortableDeviceEntry>& out,
                                                std::string& error) {
        out.clear();
        IPortableDeviceBackend* b = Backend();
        if (!b) { error = kNoBackend; return false; }
        return b->ListFolder(deviceId, NormalizePortableDevicePath(path), out, error);
    }

    bool UltraCanvasPortableDevices::Download(const std::string& deviceId,
                                              const std::string& path,
                                              const std::string& localFile,
                                              std::string& error,
                                              const PortableDeviceProgress& progress) {
        IPortableDeviceBackend* b = Backend();
        if (!b) { error = kNoBackend; return false; }
        const std::string p = NormalizePortableDevicePath(path);
        if (p == "/") { error = "the device itself is not a file"; return false; }
        if (localFile.empty()) { error = "no local file to copy into"; return false; }
        return b->Download(deviceId, p, localFile, progress, error);
    }

    bool UltraCanvasPortableDevices::Upload(const std::string& deviceId,
                                            const std::string& localFile,
                                            const std::string& path,
                                            std::string& error,
                                            const PortableDeviceProgress& progress) {
        IPortableDeviceBackend* b = Backend();
        if (!b) { error = kNoBackend; return false; }
        const std::string p = NormalizePortableDevicePath(path);
        const std::vector<std::string> names = SplitPortableDevicePath(p);
        // A file goes into a storage at the least: the device root holds the
        // storages and nothing else.
        if (names.size() < 2 || !IsBareName(names.back())) {
            error = "a file goes into a folder of one of the device's storages";
            return false;
        }
        return b->Upload(deviceId, localFile, p, progress, error);
    }

    bool UltraCanvasPortableDevices::MakeFolder(const std::string& deviceId,
                                                const std::string& path,
                                                std::string& error) {
        IPortableDeviceBackend* b = Backend();
        if (!b) { error = kNoBackend; return false; }
        const std::string p = NormalizePortableDevicePath(path);
        const std::vector<std::string> names = SplitPortableDevicePath(p);
        if (names.size() < 2 || !IsBareName(names.back())) {
            error = "a folder goes into one of the device's storages";
            return false;
        }
        return b->MakeFolder(deviceId, p, error);
    }

    bool UltraCanvasPortableDevices::Delete(const std::string& deviceId,
                                            const std::string& path,
                                            std::string& error) {
        IPortableDeviceBackend* b = Backend();
        if (!b) { error = kNoBackend; return false; }
        const std::string p = NormalizePortableDevicePath(path);
        // The device and its storages are not files: deleting a storage would
        // be erasing a phone's memory, which no file manager command should do.
        if (SplitPortableDevicePath(p).size() < 2) {
            error = "the device and its storages cannot be deleted";
            return false;
        }
        return b->Delete(deviceId, p, error);
    }

    bool UltraCanvasPortableDevices::Rename(const std::string& deviceId,
                                            const std::string& path,
                                            const std::string& newName,
                                            std::string& error) {
        IPortableDeviceBackend* b = Backend();
        if (!b) { error = kNoBackend; return false; }
        const std::string p = NormalizePortableDevicePath(path);
        if (SplitPortableDevicePath(p).size() < 2) {
            error = "the device and its storages cannot be renamed";
            return false;
        }
        if (!IsBareName(newName)) {
            error = "\"" + newName + "\" is not a name";
            return false;
        }
        return b->Rename(deviceId, p, newName, error);
    }

    bool UltraCanvasPortableDevices::GetSpace(const std::string& deviceId,
                                              uint64_t& totalBytes, uint64_t& freeBytes,
                                              std::string& error) {
        totalBytes = 0;
        freeBytes = 0;
        IPortableDeviceBackend* b = Backend();
        if (!b) { error = kNoBackend; return false; }
        return b->Space(deviceId, totalBytes, freeBytes, error);
    }

    // ===== THE MONITOR =====

    UltraCanvasPortableDeviceMonitor::~UltraCanvasPortableDeviceMonitor() {
        Stop();
    }

    bool UltraCanvasPortableDeviceMonitor::Start(ChangedCallback onChanged) {
        Stop();
        if (!onChanged || !UltraCanvasPortableDevices::Available()) return false;
        {
            std::lock_guard<std::mutex> lk(mutex);
            stopping = false;
        }
        running = true;
        const int interval = pollIntervalMs;
        worker = std::thread([this, onChanged = std::move(onChanged), interval]() {
            // The ids only: a renamed device is not news, a device coming or
            // going is. Sorted so the order the system lists them in does not
            // read as a change.
            auto snapshot = []() {
                std::vector<std::string> ids;
                for (const PortableDeviceInfo& d : UltraCanvasPortableDevices::List())
                    ids.push_back(d.id);
                std::sort(ids.begin(), ids.end());
                return ids;
            };
            std::vector<std::string> known = snapshot();
            for (;;) {
                {
                    std::unique_lock<std::mutex> lk(mutex);
                    wake.wait_for(lk, std::chrono::milliseconds(interval),
                                  [this]() { return stopping; });
                    if (stopping) return;
                }
                std::vector<std::string> now = snapshot();
                if (now == known) continue;
                known = std::move(now);
                {
                    std::lock_guard<std::mutex> lk(mutex);
                    if (stopping) return;
                }
                onChanged();
            }
        });
        return true;
    }

    void UltraCanvasPortableDeviceMonitor::Stop() {
        {
            std::lock_guard<std::mutex> lk(mutex);
            stopping = true;
        }
        wake.notify_all();
        if (worker.joinable()) worker.join();
        running = false;
    }

} // namespace UltraCanvas
