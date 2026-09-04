// OS/MSWindows/UltraCanvasWindowsFileIcons.cpp
// The application icon embedded in .exe / .dll / .ico files, extracted via
// the shell (SHDefExtractIconW picks the nearest embedded size) and
// rasterized into a UCPixmap — what Windows Explorer shows for these files.
// The extraction itself is exposed to the rest of the Windows backend
// through UltraCanvasWindowsIcons.h: the "Open with" service reuses it for
// the icon locations that IAssocHandler reports.
// Version: 1.1.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework
#include "UltraCanvasNativeFileIcons.h"
#include "UltraCanvasWindowsIcons.h"

#include <windows.h>
#include <objbase.h>  // CoInitializeEx for the worker-thread apartment
#include <shlobj.h>   // SHDefExtractIconW (WIN32_LEAN_AND_MEAN keeps it
                      // out of windows.h; shellapi.h does not declare it)

#include <cairo/cairo.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace UltraCanvas {

    bool NativeFileIconAvailable(const std::string& path) {
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return false;
        std::string ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return ext == "exe" || ext == "dll" || ext == "ico";
    }

    namespace {

        std::wstring Utf8ToWide(const std::string& s) {
            if (s.empty()) return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1,
                                              nullptr, 0);
            if (n <= 1) return {};
            std::wstring w(static_cast<size_t>(n - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
            return w;
        }

        // HICON → straight-alpha BGRA rows (top-down). Old icons without an
        // alpha channel get their coverage from the 1-bit AND mask.
        bool IconToBGRA(HICON icon, int& outW, int& outH,
                        std::vector<uint8_t>& outPixels) {
            ICONINFO info{};
            if (!GetIconInfo(icon, &info)) return false;
            // GetIconInfo hands out bitmap copies the caller must delete.
            HBITMAP color = info.hbmColor;
            HBITMAP mask = info.hbmMask;
            bool ok = false;
            BITMAP bm{};
            if (color && GetObject(color, sizeof(bm), &bm) == sizeof(bm) &&
                bm.bmWidth > 0 && bm.bmHeight > 0) {
                outW = bm.bmWidth;
                outH = bm.bmHeight;
                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = outW;
                bi.bmiHeader.biHeight = -outH;   // top-down rows
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                outPixels.resize(static_cast<size_t>(outW) * outH * 4);
                HDC dc = GetDC(nullptr);
                if (dc && GetDIBits(dc, color, 0, static_cast<UINT>(outH),
                                    outPixels.data(), &bi,
                                    DIB_RGB_COLORS) == outH) {
                    // An all-zero alpha channel means "no alpha here": take
                    // the coverage from the AND mask instead (0 = opaque).
                    bool hasAlpha = false;
                    for (size_t i = 3; i < outPixels.size(); i += 4) {
                        if (outPixels[i] != 0) { hasAlpha = true; break; }
                    }
                    if (!hasAlpha) {
                        std::vector<uint8_t> maskPixels(
                                static_cast<size_t>(outW) * outH * 4);
                        if (mask && GetDIBits(dc, mask, 0,
                                              static_cast<UINT>(outH),
                                              maskPixels.data(), &bi,
                                              DIB_RGB_COLORS) == outH) {
                            for (size_t i = 0; i < maskPixels.size(); i += 4)
                                outPixels[i + 3] = maskPixels[i] ? 0 : 255;
                        } else {
                            for (size_t i = 3; i < outPixels.size(); i += 4)
                                outPixels[i] = 255;
                        }
                    }
                    ok = true;
                }
                if (dc) ReleaseDC(nullptr, dc);
            }
            if (color) DeleteObject(color);
            if (mask) DeleteObject(mask);
            return ok;
        }

    } // namespace

    namespace WindowsIcons {

        std::shared_ptr<UCPixmap> PixmapFromIcon(HICON icon) {
            if (!icon) return nullptr;
            int w = 0, h = 0;
            std::vector<uint8_t> bgra;
            if (!IconToBGRA(icon, w, h, bgra) || w <= 0 || h <= 0)
                return nullptr;

            auto pm = std::make_shared<UCPixmap>(w, h);
            cairo_surface_t* surf = pm->GetSurface();
            if (!surf || !pm->IsValid()) return nullptr;
            cairo_surface_flush(surf);
            uint8_t* pixels = cairo_image_surface_get_data(surf);
            const int stride = cairo_image_surface_get_stride(surf);

            // Cairo ARGB32 is premultiplied, native-endian 32-bit words — on
            // little-endian Windows that is B,G,R,A bytes, so only the alpha
            // premultiplication separates it from the DIB rows.
            for (int y = 0; y < h; ++y) {
                const uint8_t* src = bgra.data() + static_cast<size_t>(y) * w * 4;
                uint32_t* row = reinterpret_cast<uint32_t*>(
                        pixels + static_cast<size_t>(y) * stride);
                for (int x = 0; x < w; ++x) {
                    const uint8_t b = src[x * 4 + 0];
                    const uint8_t g = src[x * 4 + 1];
                    const uint8_t r = src[x * 4 + 2];
                    const uint8_t a = src[x * 4 + 3];
                    row[x] = (static_cast<uint32_t>(a) << 24)
                           | (static_cast<uint32_t>(r * a / 255) << 16)
                           | (static_cast<uint32_t>(g * a / 255) << 8)
                           |  static_cast<uint32_t>(b * a / 255);
                }
            }
            pm->MarkDirty();
            return pm;
        }

        std::shared_ptr<UCPixmap> LoadIconResourcePixmap(
                const std::wstring& location, int index, int desiredSize) {
            if (location.empty()) return nullptr;
            // The shell serves the nearest of the file's embedded icon sizes;
            // 256 is the format's ceiling (Vista+ PNG-compressed frames).
            const int size = std::max(16, std::min(desiredSize, 256));
            HICON icon = nullptr;
            const HRESULT hr = SHDefExtractIconW(location.c_str(), index, 0,
                                                 &icon, nullptr,
                                                 static_cast<UINT>(size));
            // S_FALSE = the file exists but holds no icon resource.
            if (hr != S_OK || !icon) return nullptr;
            std::shared_ptr<UCPixmap> pm = PixmapFromIcon(icon);
            DestroyIcon(icon);
            return pm;
        }

    } // namespace WindowsIcons

    std::shared_ptr<UCPixmap> LoadNativeFileIconPixmap(const std::string& path,
                                                       int desiredSize) {
        if (!NativeFileIconAvailable(path)) return nullptr;
        // Icon 0 is the one Explorer shows for the file itself.
        return WindowsIcons::LoadIconResourcePixmap(Utf8ToWide(path), 0,
                                                    desiredSize);
    }

    // The extraction runs on the filer's thumbnail workers, which have no
    // message loop, so they join the multi-threaded apartment rather than
    // creating an STA nobody pumps. RPC_E_CHANGED_MODE means the thread is
    // already in an apartment of the other kind - fine for the shell calls
    // here, and it must not be balanced by a CoUninitialize.
    NativeFileIconThreadScope::NativeFileIconThreadScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        joined = (hr == S_OK || hr == S_FALSE);
    }

    NativeFileIconThreadScope::~NativeFileIconThreadScope() {
        if (joined) CoUninitialize();
    }

} // namespace UltraCanvas
