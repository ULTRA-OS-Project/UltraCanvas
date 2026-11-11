// OS/Linux/UltraCanvasLinuxDeviceProbes.h
// INTERNAL: the parts of UltraCanvasDesktopShell's device activity that come
// from procfs and sysfs and so owe nothing to the window system - the sound
// cards' streams, the camera, the battery - shared by the X11 and the Wayland
// backends (UltraCanvasLinuxDesktopShell.cpp, Wayland/UltraCanvasWaylandDesktopShell.cpp).
// Not a public header; applications include UltraCanvasDesktopShell.h.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasDesktopShell.h"

#include <string>
#include <vector>

namespace UltraCanvas {
namespace DesktopShellBackend {
namespace LinuxProbes {

    // A small text file's contents, trailing white space dropped; "" when
    // it cannot be read.
    std::string ReadTextFile(const std::string& path);
    // The names in a directory, sorted, "." and ".." left out.
    std::vector<std::string> ListDirectory(const std::string& path);

    // /proc/asound/card*/pcm*{c,p}/sub*/status: a running capture stream is
    // the microphone, a running playback stream the speaker.
    void ProbeAudio(DesktopDeviceActivity& activity);
    // A process of the user's holding a /dev/video* descriptor.
    void ProbeCamera(DesktopDeviceActivity& activity);
    // /sys/class/power_supply/*: the first battery's level and state.
    void ProbeBattery(DesktopDeviceActivity& activity);

} // namespace LinuxProbes
} // namespace DesktopShellBackend
} // namespace UltraCanvas
