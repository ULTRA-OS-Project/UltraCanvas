// OS/MSWindows/UltraCanvasWindowsHostAppearance.cpp
// The desktop's look on Windows, for
// UltraCanvasWindowsApplication::DetectHostAppearanceNative
// (UltraCanvasHostAppearance.h, phase T1 of the host theme proposal):
//   dark          HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
//                 AppsUseLightTheme (0 is dark; missing is light, as before
//                 Windows 10 1607)
//   high contrast SystemParametersInfoW SPI_GETHIGHCONTRAST
//   motion        SPI_GETCLIENTAREAANIMATION off is "reduce"
//   accent        HKCU\...\Explorer\Accent AccentColorMenu (the colour
//                 Settings shows, 0xAABBGGRR), else DwmGetColorizationColor
//   fonts         the message font and Consolas (UltraCanvasWindowsSystemFont.cpp)
//   text scale    HKCU\Software\Microsoft\Accessibility TextScaleFactor (100-225)
// The change event is the window's: WM_SETTINGCHANGE, WM_THEMECHANGED and
// WM_DWMCOLORIZATIONCOLORCHANGED in UltraCanvasWindowsWindow's WndProc call
// ScheduleHostAppearanceRefresh, which reads all of this again once per
// burst and tells the handlers when something differs.
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindowsApplication.h"
#include "UltraCanvasHostAppearance.h"

#include <windows.h>

#include <cstdint>

// Vista's animation preference; absent from a header built for an older
// Windows, the value is the same everywhere.
#ifndef SPI_GETCLIENTAREAANIMATION
#define SPI_GETCLIENTAREAANIMATION 0x1042
#endif

namespace UltraCanvas {

namespace {

// A REG_DWORD under HKEY_CURRENT_USER; false when it is not there.
bool ReadUserDword(const wchar_t* subKey, const wchar_t* value, uint32_t& out) {
    DWORD data = 0;
    DWORD size = sizeof(data);
    DWORD type = 0;
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, subKey, value, RRF_RT_REG_DWORD, &type, &data, &size);
    if (status != ERROR_SUCCESS || type != REG_DWORD) return false;
    out = static_cast<uint32_t>(data);
    return true;
}

bool HighContrastOn() {
    HIGHCONTRASTW contrast = {};
    contrast.cbSize = sizeof(contrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)) return false;
    return (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

bool ClientAreaAnimationOff() {
    BOOL animation = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0)) return false;
    return animation == FALSE;
}

// DwmGetColorizationColor through dwmapi loaded by name, so a system
// without DWM (a server core) costs nothing.
bool DwmColorization(uint32_t& argb) {
    using DwmGetColorizationColorFn = HRESULT(WINAPI*)(DWORD*, BOOL*);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return false;
    auto fn = reinterpret_cast<DwmGetColorizationColorFn>(GetProcAddress(dwm, "DwmGetColorizationColor"));
    DWORD color = 0;
    BOOL opaque = FALSE;
    const bool ok = fn && SUCCEEDED(fn(&color, &opaque));
    FreeLibrary(dwm);
    if (!ok) return false;
    argb = static_cast<uint32_t>(color);
    return true;
}

}  // namespace

HostAppearance UltraCanvasWindowsApplication::DetectHostAppearanceNative() {
    HostAppearance host = DefaultHostAppearance();
    host.source = "windows-registry";

    uint32_t value = 0;
    if (ReadUserDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", value)) {
        host.dark = HostAppearanceWindows::DarkFromAppsUseLightTheme(value);
    }
    host.highContrast = HighContrastOn();
    host.reduceMotion = ClientAreaAnimationOff();

    if (ReadUserDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentColorMenu", value)) {
        host.accent = HostAppearanceWindows::AccentFromAbgr(value);
    } else if (DwmColorization(value)) {
        host.accent = HostAppearanceWindows::AccentFromArgb(value);
    }

    const FontStyle ui = DetectSystemFontStyleNative();
    const FontStyle mono = DetectMonospacedFontStyleNative();
    if (!ui.fontFamily.empty() && ui.fontSize > 0) host.uiFont = ui;
    if (!mono.fontFamily.empty() && mono.fontSize > 0) host.monoFont = mono;

    if (ReadUserDword(L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", value)) {
        host.textScale = HostAppearanceWindows::TextScaleFromPercent(value);
    }
    return host;
}

}  // namespace UltraCanvas
