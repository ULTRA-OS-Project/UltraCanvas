// OS/Linux/UltraCanvasLinuxAccessibility.h
// The AT-SPI bridge: publishes the application's windows and elements on the
// accessibility bus, where Orca and other assistive technology find them.
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework
#pragma once

namespace UltraCanvas {
namespace LinuxAccessibility {

// Called by UltraCanvasLinuxApplication. Start() connects when the desktop
// has accessibility turned on (org.a11y.Status IsEnabled or
// ScreenReaderEnabled), or later when it is turned on, and does nothing
// without a session bus. NO_AT_BRIDGE=1 keeps it off;
// UC_ACCESSIBILITY_ALWAYS_ON=1 connects whatever the desktop says.
void Start();
void Stop();

// True while the application is registered on the accessibility bus.
bool IsConnected();

} // namespace LinuxAccessibility
} // namespace UltraCanvas
