// Tests/DisplayRequired.h
// Whether this run asked for a display: DisplayTest::DisplayRequired().
//
// A window test skips itself without a display, and a skip passes. CI's
// virtual-display steps set ULTRACANVAS_TEST_DISPLAY_REQUIRED=1, and there a
// test that skips for want of a display - or of an application that starts,
// or a window that opens - would read as a pass, so it fails instead.
// scripts/check_window_tests.py holds every window test to asking this.
//
// Its own header, with nothing but the C++ library in it, so that a test can
// ask without taking DisplayTestSupport.h's framework headers along: on
// Windows those include windows.h and then #undef RGB, so DesktopShellTest's
// own RGB(...) for a tray icon became UltraCanvas::RGB, a Color, and stopped
// compiling. DisplayTestSupport.h includes this one.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#pragma once

#include <cstdlib>
#include <string>

namespace UltraCanvas {
namespace DisplayTest {

// Unset, empty or "0" asks for nothing.
inline bool DisplayRequired() {
    const char* required = std::getenv("ULTRACANVAS_TEST_DISPLAY_REQUIRED");
    return required && *required && std::string(required) != "0";
}

} // namespace DisplayTest
} // namespace UltraCanvas
