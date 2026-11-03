// OS/MSWindows/UltraCanvasWindowsSystemFont.cpp
// The desktop's interface and monospace fonts on Windows, for
// UltraCanvasWindowsApplication::DetectSystemFontStyleNative and
// DetectMonospacedFontStyleNative. Until 0.9.362 both returned the literal
// Ubuntu 12, so "the system font" was never the system's.
//
// The interface font is the message font of the non-client metrics
// (Segoe UI, 9 pt on a stock install, whatever the person set under
// Accessibility), asked at 96 dpi through SystemParametersInfoForDpi where
// it exists (Windows 10 1607), else at the system dpi and converted, so the
// size is in points whatever the monitor. The monospace font is Consolas,
// which every Windows since Vista ships, at the same size. The bundled
// Ubuntu / Ubuntu Mono at 12 pt stay the answer when the metrics cannot
// be read.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindowsApplication.h"
#include "UltraCanvasPathUtf8.h"   // PathUtf8Detail::Utf16ToUtf8

#include <windows.h>

#include <cmath>
#include <string>

namespace UltraCanvas {

namespace {

// The message font's face and height in points; false when Windows will
// not say.
bool ReadMessageFont(std::string& family, double& points) {
    constexpr UINT kDpi = 96;
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);

    using SystemParametersInfoForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    using GetDpiForSystemFn = UINT(WINAPI*)();
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto forDpi = user32 ? reinterpret_cast<SystemParametersInfoForDpiFn>(
                               GetProcAddress(user32, "SystemParametersInfoForDpi")) : nullptr;
    auto dpiForSystem = user32 ? reinterpret_cast<GetDpiForSystemFn>(
                                     GetProcAddress(user32, "GetDpiForSystem")) : nullptr;

    UINT dpi = kDpi;
    BOOL ok = FALSE;
    if (forDpi) {
        ok = forDpi(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0, kDpi);
    }
    if (!ok) {
        ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
        if (!ok) {
            // A header newer than the running Windows: the struct grew by
            // iPaddedBorderWidth in Vista, and an older system wants the
            // size it knows.
            metrics.cbSize = sizeof(metrics) - sizeof(int);
            ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
        }
        if (ok) {
            if (dpiForSystem) dpi = dpiForSystem();
            else if (HDC screen = GetDC(nullptr)) {
                dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
                ReleaseDC(nullptr, screen);
            }
            if (dpi == 0) dpi = kDpi;
        }
    }
    if (!ok) return false;

    const LOGFONTW& font = metrics.lfMessageFont;
    if (font.lfFaceName[0] == L'\0' || font.lfHeight == 0) return false;
    family = PathUtf8Detail::Utf16ToUtf8(std::wstring(font.lfFaceName));
    // A negative height is the character height in pixels at `dpi`; a
    // positive one the cell height, taken as the same.
    points = std::fabs(static_cast<double>(font.lfHeight)) * 72.0 / static_cast<double>(dpi);
    if (points < 6.0 || points > 72.0) return false;
    return true;
}

}  // namespace

FontStyle UltraCanvasWindowsApplication::DetectSystemFontStyleNative() {
    FontStyle result;
    result.fontFamily = "Ubuntu";
    result.fontSize = 12.0f;
    std::string family;
    double points = 0;
    if (ReadMessageFont(family, points)) {
        result.fontFamily = family;
        result.fontSize = points;
    }
    return result;
}

FontStyle UltraCanvasWindowsApplication::DetectMonospacedFontStyleNative() {
    FontStyle result;
    result.fontFamily = "Ubuntu Mono";
    result.fontSize = 12.0f;
    std::string family;
    double points = 0;
    if (ReadMessageFont(family, points)) {
        result.fontFamily = "Consolas";
        result.fontSize = points;
    }
    return result;
}

}  // namespace UltraCanvas
