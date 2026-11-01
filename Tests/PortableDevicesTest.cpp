// Tests/PortableDevicesTest.cpp
// Unit tests for the platform-independent half of UltraCanvasPortableDevices:
// the path rules every backend is handed paths by, and what a build without a
// backend answers. Built from the core file alone - no device, no display and
// no UltraCanvas library - so it runs the same on every CI row; the Windows
// WPD backend needs a phone on a USB port and is not exercised here.
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#include "UltraCanvasPortableDevices.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

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
    std::printf("── UltraCanvasPortableDevices (core, no backend) ──\n");

    // ===== PATHS =====
    CHECK(NormalizePortableDevicePath("") == "/", "an empty path is the device");
    CHECK(NormalizePortableDevicePath("/") == "/", "\"/\" is the device");
    CHECK(NormalizePortableDevicePath("Internal shared storage/DCIM/") ==
                  "/Internal shared storage/DCIM",
          "a leading '/' is added and a trailing one dropped");
    CHECK(NormalizePortableDevicePath("//a///b") == "/a/b", "empty segments go");
    CHECK(NormalizePortableDevicePath("\\a\\b c") == "/a/b c",
          "a backslash reads as '/', a space stays");

    const std::vector<std::string> names = SplitPortableDevicePath("/a/b c//d/");
    CHECK(names.size() == 3 && names[0] == "a" && names[1] == "b c" && names[2] == "d",
          "a path splits into its names");
    CHECK(SplitPortableDevicePath("/").empty(), "the device has no names");

    // ===== NO BACKEND =====
    // The core file alone supplies the null backend: this is what macOS,
    // Linux, Android and WebAssembly builds answer.
    CHECK(!UltraCanvasPortableDevices::Available(), "no backend: not available");
    CHECK(UltraCanvasPortableDevices::List().empty(), "and no devices");

    std::string error;
    std::vector<PortableDeviceEntry> entries{PortableDeviceEntry{}};
    CHECK(!UltraCanvasPortableDevices::ListFolder("dev", "/", entries, error) && !error.empty(),
          "listing fails with a reason");
    CHECK(entries.empty(), "and leaves no stale entries behind");
    error.clear();
    CHECK(!UltraCanvasPortableDevices::Download("dev", "/a/b", "/tmp/x", error) && !error.empty(),
          "a download fails with a reason");

    UltraCanvasPortableDeviceMonitor monitor;
    CHECK(!monitor.Start([]() {}), "a monitor with nothing to watch is refused");
    CHECK(!monitor.IsRunning(), "and reports itself idle");
    monitor.Stop();
    CHECK(true, "Stop() on a monitor that never ran is safe");

    std::printf("%s: %d checks, %d failure(s)\n",
                failures ? "FAILURE" : "OK", checks, failures);
    return failures ? 1 : 0;
}
