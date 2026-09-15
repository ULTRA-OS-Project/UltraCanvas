// OS/Linux/UltraCanvasLinuxIODeviceWatcher.cpp
// udev hot-plug watcher: notices kernel device arrivals and departures and
// reports which IODeviceManager category they touch.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#if defined(__linux__) && defined(ULTRACANVAS_HAS_UDEV)

#include "../../core/IODeviceManager/UltraCanvasIODeviceBackends.h"

#include <libudev.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {
namespace {

// One physical device can touch several categories - a multifunction printer
// is a printer and a scanner on the same USB interface - so a subsystem maps
// to a list rather than to one category.
std::vector<IODeviceCategory> CategoriesForSubsystem(const std::string& subsystem) {
    if (subsystem == "video4linux") {
        return {IODeviceCategory::Camera};
    }
    if (subsystem == "sound") {
        return {IODeviceCategory::Microphone, IODeviceCategory::Speaker};
    }
    if (subsystem == "usb") {
        // The kernel says a USB device arrived, not what it is: a scanner and
        // a printer look alike at this level, and SANE or CUPS has to be
        // asked. Re-enumerating both is cheap next to getting it wrong, and
        // the manager's merge turns "nothing changed" into no notification.
        return {IODeviceCategory::Scanner, IODeviceCategory::Printer,
                IODeviceCategory::Camera};
    }
    return {};
}

// ============================================================================
// WATCHER
// ============================================================================

class UdevDeviceWatcher : public IDeviceWatcher {
public:
    ~UdevDeviceWatcher() override { Stop(); }

    std::string GetName() const override { return "udev"; }

    IODeviceResult Start(CategoryChangedCallback onCategoryChanged) override {
        if (running) {
            return IODeviceResult::Ok();
        }
        if (!onCategoryChanged) {
            return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                         "Watching needs a callback");
        }

        context = udev_new();
        if (!context) {
            return IODeviceResult::Error(IODeviceResultCode::BackendUnavailable,
                                         "udev is not available on this system");
        }

        monitor = udev_monitor_new_from_netlink(context, "udev");
        if (!monitor) {
            Cleanup();
            return IODeviceResult::Error(IODeviceResultCode::BackendUnavailable,
                                         "Could not open a udev netlink monitor");
        }

        // Filtering in the kernel rather than in this process: without it
        // every uevent on the machine - every block device, every network
        // change - wakes this thread to be discarded.
        for (const char* subsystem : {"video4linux", "usb", "sound"}) {
            udev_monitor_filter_add_match_subsystem_devtype(monitor, subsystem, nullptr);
        }

        if (udev_monitor_enable_receiving(monitor) < 0) {
            Cleanup();
            return IODeviceResult::Error(IODeviceResultCode::BackendError,
                                         "Could not start receiving udev events");
        }

        // A pollable stop signal, so the thread wakes immediately on Stop()
        // rather than after a polling timeout expires.
        stopEvent = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (stopEvent < 0) {
            Cleanup();
            return IODeviceResult::Error(IODeviceResultCode::IOError,
                                         std::string("Could not create the watcher's "
                                                     "stop signal: ") +
                                             std::strerror(errno));
        }

        callback = std::move(onCategoryChanged);
        running = true;
        thread = std::thread([this] { Watch(); });

        return IODeviceResult::Ok();
    }

    void Stop() override {
        if (!running.exchange(false)) {
            // Still clean up a Start() that failed partway.
            Cleanup();
            return;
        }

        if (stopEvent >= 0) {
            const uint64_t one = 1;
            ssize_t written = ::write(stopEvent, &one, sizeof(one));
            (void)written;
        }
        if (thread.joinable()) {
            thread.join();
        }

        Cleanup();
        callback = nullptr;
    }

private:
    void Watch() {
        const int monitorFd = udev_monitor_get_fd(monitor);

        // Plugging in one device produces a burst: a webcam brings up two or
        // more video4linux nodes plus its USB interfaces, each its own
        // uevent. Re-enumerating on each would rescan the hardware several
        // times for one physical act, so events are collected and acted on
        // once the burst goes quiet.
        constexpr int kQuietPeriodMillis = 250;

        std::set<IODeviceCategory> pending;
        bool collecting = false;

        while (running) {
            pollfd fds[2] = {};
            fds[0].fd = monitorFd;
            fds[0].events = POLLIN;
            fds[1].fd = stopEvent;
            fds[1].events = POLLIN;

            // Once something is pending, wait only for the quiet period, so
            // the burst is flushed promptly instead of waiting indefinitely
            // for an event that may not come.
            const int timeout = collecting ? kQuietPeriodMillis : -1;

            int ready = 0;
            do {
                ready = ::poll(fds, 2, timeout);
            } while (ready == -1 && errno == EINTR);

            if (ready < 0) {
                break;
            }
            if (fds[1].revents & POLLIN) {
                break;      // Stop() asked
            }

            if (ready == 0) {
                // The burst went quiet: act on what it touched.
                Flush(pending);
                collecting = false;
                continue;
            }

            if (fds[0].revents & POLLIN) {
                if (Collect(pending)) {
                    collecting = true;
                }
            }
        }

        // Anything collected but not yet flushed is dropped deliberately: the
        // caller is stopping, and firing a re-enumeration into a manager
        // that is shutting down is worse than missing it.
    }

    // Returns true when the event touched a category worth re-enumerating.
    bool Collect(std::set<IODeviceCategory>& pending) {
        udev_device* device = udev_monitor_receive_device(monitor);
        if (!device) {
            return false;
        }

        const char* action = udev_device_get_action(device);
        const char* subsystem = udev_device_get_subsystem(device);

        bool interesting = false;
        // "change" is deliberately ignored: a device reporting a property
        // change has not appeared or disappeared, and re-enumerating on it
        // makes a busy machine rescan constantly.
        if (action && subsystem &&
            (std::strcmp(action, "add") == 0 || std::strcmp(action, "remove") == 0)) {
            for (IODeviceCategory category : CategoriesForSubsystem(subsystem)) {
                pending.insert(category);
                interesting = true;
            }
        }

        udev_device_unref(device);
        return interesting;
    }

    void Flush(std::set<IODeviceCategory>& pending) {
        if (pending.empty()) {
            return;
        }
        std::set<IODeviceCategory> categories;
        categories.swap(pending);

        for (IODeviceCategory category : categories) {
            if (!running) {
                return;
            }
            if (callback) {
                callback(category);
            }
        }
    }

    void Cleanup() {
        if (stopEvent >= 0) {
            ::close(stopEvent);
            stopEvent = -1;
        }
        if (monitor) {
            udev_monitor_unref(monitor);
            monitor = nullptr;
        }
        if (context) {
            udev_unref(context);
            context = nullptr;
        }
    }

    udev* context = nullptr;
    udev_monitor* monitor = nullptr;
    int stopEvent = -1;
    std::atomic<bool> running{false};
    std::thread thread;
    CategoryChangedCallback callback;
};

}  // namespace

namespace Internal {

IDeviceWatcherPtr CreateDeviceWatcher() {
    return std::make_shared<UdevDeviceWatcher>();
}

}  // namespace Internal
}  // namespace UltraCanvas

#endif  // __linux__ && ULTRACANVAS_HAS_UDEV
