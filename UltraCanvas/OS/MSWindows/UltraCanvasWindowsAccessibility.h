// OS/MSWindows/UltraCanvasWindowsAccessibility.h
// The UI Automation bridge: answers WM_GETOBJECT with providers for the
// window and its elements, so Narrator, NVDA and JAWS can read them.
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework
#pragma once

#include <windows.h>

namespace UltraCanvas {

class UltraCanvasWindowBase;

namespace WindowsAccessibility {

// WM_GETOBJECT for the window: returns the UI Automation root provider when
// UI Automation asks for it (`handled` true), else leaves the message to
// DefWindowProc. The first request switches the accessibility events on.
LRESULT HandleGetObject(UltraCanvasWindowBase* window, HWND hwnd, WPARAM wParam, LPARAM lParam, bool& handled);

// WM_DESTROY: releases what UI Automation holds for the window.
void WindowDestroyed(HWND hwnd);

// Application shutdown: disconnects every provider.
void Shutdown();

} // namespace WindowsAccessibility
} // namespace UltraCanvas
