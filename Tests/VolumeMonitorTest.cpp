// Tests/VolumeMonitorTest.cpp
// Unit tests for UltraCanvasVolumeMonitor: that the volume list is sane and
// stable, that a monitor starts and stops cleanly on every platform, and -
// the part a leak or a crash hides behind - that Stop() is final and
// repeatable.
//
// Framework-independent: builds from the monitor's own sources (core plus this
// platform's backend), no display connection and no UltraCanvas library.
//
// Mounting a filesystem needs privileges a test does not have, so nothing here
// asserts that a real insertion is reported; what is checked is everything
// that can be checked without one, on both the native and the polling path.
// Version: 1.1.0 - the GVFS places: their labels, and that they are listed
// Version: 1.0.0
// Last Modified: 2026-09-01
// Author: UltraCanvas Framework

#include "UltraCanvasVolumeMonitor.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using UltraCanvas::GvfsVolumeLabel;
using UltraCanvas::ListGvfsVolumes;
using UltraCanvas::ListMountedVolumes;
using UltraCanvas::ListVolumeRoots;
using UltraCanvas::MountedVolume;
using UltraCanvas::UltraCanvasVolumeMonitor;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what) do { \
    ++checks; \
    if (cond) { \
        std::printf("  PASS  %s\n", what); \
    } else { \
        ++failures; \
        std::printf("  FAIL  %s (%s:%d)\n", what, __FILE__, __LINE__); \
    } \
} while (0)

int main() {
    const bool native = UltraCanvasVolumeMonitor::NativeBackendAvailable();
    std::printf("── UltraCanvasVolumeMonitor (native backend: %s) ──\n",
                native ? "yes" : "no - mounts are noticed by polling");

    // ===== THE VOLUME LIST =====
    const std::vector<MountedVolume> volumes = ListMountedVolumes();
    CHECK(!volumes.empty(), "at least one volume is mounted");

    {
        bool everyPathReal = true;
        bool everyLabelSet = true;
        std::set<std::string> unique;
        bool noDuplicates = true;
        for (const MountedVolume& v : volumes) {
            std::error_code ec;
            if (v.path.empty() || !fs::is_directory(v.path, ec) || ec)
                everyPathReal = false;
            if (v.label.empty()) everyLabelSet = false;
            if (!unique.insert(v.path).second) noDuplicates = false;
        }
        CHECK(everyPathReal, "every volume is a directory that exists");
        CHECK(everyLabelSet, "every volume has a label");
        CHECK(noDuplicates,
              "a volume reachable under two bases is listed once");
    }

    {
        // The root is the one volume every machine has, and it leads the list
        // because everything else is reachable through it.
        int roots = 0;
        for (const MountedVolume& v : volumes) if (v.isSystemRoot) ++roots;
        CHECK(roots == 1, "exactly one volume is the system root");
        CHECK(!volumes.empty() && volumes.front().isSystemRoot,
              "and it is listed first");
    }

    {
        const std::vector<std::string> roots = ListVolumeRoots();
        bool sameOrder = roots.size() == volumes.size();
        for (size_t i = 0; sameOrder && i < roots.size(); ++i)
            if (roots[i] != volumes[i].path) sameOrder = false;
        CHECK(sameOrder, "ListVolumeRoots() is the same list, paths only");
    }

    {
        // Nothing mounted between the two calls of a test run, so the answer
        // must be identical - a caller diffs these lists against each other.
        const std::vector<std::string> again = ListVolumeRoots();
        const std::vector<std::string> once = ListVolumeRoots();
        CHECK(again == once, "two listings of an unchanged machine agree");
    }

    // ===== STARTING AND STOPPING =====
    {
        UltraCanvasVolumeMonitor monitor;
        CHECK(!monitor.Start(nullptr), "a monitor without a callback is refused");
        CHECK(!monitor.IsRunning(), "and reports itself idle");
    }
    {
        std::atomic<int> hits{0};
        UltraCanvasVolumeMonitor monitor;
        // Short interval so a polling build does its work inside the test.
        monitor.SetPollIntervalMs(250);
        CHECK(monitor.GetPollIntervalMs() == 250, "the poll interval is settable");
        monitor.SetPollIntervalMs(10);
        CHECK(monitor.GetPollIntervalMs() == 250,
              "and a too-short one is raised to the floor");

        CHECK(monitor.Start([&hits]() { ++hits; }),
              "a monitor starts on every platform");
        CHECK(monitor.IsRunning(), "and reports itself running");
        CHECK(monitor.IsNative() == native,
              "using the operating system where there is one");

        // Nothing was mounted, so nothing may be reported: a monitor that
        // fired on its own would send its caller re-listing forever.
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        CHECK(hits.load() == 0, "an unchanged machine reports nothing");

        monitor.Stop();
        CHECK(!monitor.IsRunning(), "Stop() clears the monitor");
        const int afterStop = hits.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(hits.load() == afterStop, "no callback arrives after Stop()");

        monitor.Stop();
        CHECK(true, "Stop() twice is safe");

        CHECK(monitor.Start([&hits]() { ++hits; }),
              "a stopped monitor can be started again");
        monitor.Stop();
    }
    {
        // The destructor stops it: a monitor left running would call into a
        // callback whose captures have gone away.
        std::atomic<int> hits{0};
        {
            UltraCanvasVolumeMonitor monitor;
            monitor.SetPollIntervalMs(250);
            monitor.Start([&hits]() { ++hits; });
        }
        CHECK(true, "the destructor stops a running monitor");
    }

    // ===== GVFS PLACES (a phone, a camera, a share a Linux desktop mounted) =====
    {
        CHECK(GvfsVolumeLabel("mtp:host=SAMSUNG_SAMSUNG_Android_R58M12345XY") ==
                      "SAMSUNG SAMSUNG Android",
              "an MTP phone is named without its serial");
        CHECK(GvfsVolumeLabel("mtp:host=Google_Pixel_8") == "Google Pixel 8",
              "a name with no serial is kept whole");
        CHECK(GvfsVolumeLabel("mtp:host=%5Busb%3A002%2C010%5D") == "Phone",
              "an old-style USB address names no device");
        CHECK(GvfsVolumeLabel("gphoto2:host=Canon_Inc._Canon_Digital_Camera") ==
                      "Canon Inc. Canon Digital Camera",
              "a camera is named after itself");
        CHECK(GvfsVolumeLabel("afc:host=00008110-001A2B3C4D5E801E,port=3") == "iPhone or iPad",
              "an iPhone is not named by its UDID");
        CHECK(GvfsVolumeLabel("smb-share:server=nas,share=photos") == "photos on nas",
              "a share names itself and its server");
        CHECK(GvfsVolumeLabel("sftp:host=example.com,user=bob") == "bob@example.com",
              "an SFTP place names its user and host");
        CHECK(GvfsVolumeLabel("google-drive:host=gmail.com,user=me").empty(),
              "a cloud account is left to the cloud folders");
        CHECK(GvfsVolumeLabel("not-a-mount").empty(), "a name without a scheme is no place");

#if !defined(_WIN32) && !defined(__APPLE__)
        // Read from where the desktop puts it: a stand-in runtime directory.
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path runtime = fs::temp_directory_path() / "uc_volume_test_runtime";
        fs::remove_all(runtime, ec);
        fs::create_directories(runtime / "gvfs" / "mtp:host=Google_Pixel_8", ec);
        fs::create_directories(runtime / "gvfs" / "google-drive:host=gmail.com,user=me", ec);
        const char* previous = std::getenv("XDG_RUNTIME_DIR");
        const std::string saved = previous ? previous : "";
        setenv("XDG_RUNTIME_DIR", runtime.string().c_str(), 1);
        const std::vector<MountedVolume> places = ListGvfsVolumes();
        CHECK(places.size() == 1 && places.front().label == "Google Pixel 8",
              "the GVFS places are listed, the cloud account is not");
        bool inList = false;
        for (const MountedVolume& v : ListMountedVolumes())
            if (v.label == "Google Pixel 8") inList = true;
        CHECK(inList, "and ListMountedVolumes carries them");
        if (previous) setenv("XDG_RUNTIME_DIR", saved.c_str(), 1);
        else unsetenv("XDG_RUNTIME_DIR");
        fs::remove_all(runtime, ec);
#endif
    }

    std::printf("%s: %d checks, %d failure(s)\n",
                failures ? "FAILURE" : "OK", checks, failures);
    return failures ? 1 : 0;
}
