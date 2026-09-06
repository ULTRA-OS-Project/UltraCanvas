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
#include "UltraCanvasIconResource.h"
#include "UltraCanvasMacBundle.h"
#include "UltraCanvasShellLink.h"

#include <windows.h>
#include <objbase.h>  // CoInitializeEx for the worker-thread apartment
#include <shlobj.h>   // SHDefExtractIconW (WIN32_LEAN_AND_MEAN keeps it
                      // out of windows.h; shellapi.h does not declare it)
#include <shellapi.h> // SHGetFileInfoW - the icon an association provides

#include <cairo/cairo.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace UltraCanvas {

    // The same set on every platform - the icon-carrying file kinds plus
    // shortcuts, which name one of them. Kept in one place
    // (UltraCanvasIconResource / UltraCanvasShellLink) so a file display
    // shows the same icons wherever the disk is being read from.
    bool NativeFileIconAvailable(const std::string& path) {
        // Bundles too: a Mac disk read from Windows should show its
        // applications with their own icons, which is a matter of reading
        // files and needs nothing from the shell.
        return HasIconResourceExtension(path) || IsShellLinkPath(path) ||
               IsBundlePath(path);
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

    namespace {

        // The icon the shell shows for a file, from its association rather
        // than from its own resources: what a shortcut to a document or a
        // folder is drawn with. Smaller than an extracted resource icon (the
        // shell serves the system icon sizes), so it is only a fallback.
        std::shared_ptr<UCPixmap> AssociatedIconPixmap(const std::wstring& path,
                                                       int desiredSize) {
            if (path.empty()) return nullptr;
            SHFILEINFOW info{};
            const UINT flags = SHGFI_ICON |
                               (desiredSize > 16 ? SHGFI_LARGEICON : SHGFI_SMALLICON);
            if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), flags) ||
                !info.hIcon)
                return nullptr;
            std::shared_ptr<UCPixmap> pixmap = WindowsIcons::PixmapFromIcon(info.hIcon);
            DestroyIcon(info.hIcon);
            return pixmap;
        }

    } // namespace

    std::shared_ptr<UCPixmap> LoadNativeFileIconPixmap(const std::string& path,
                                                       int desiredSize) {
        if (!NativeFileIconAvailable(path)) return nullptr;

        if (IsBundlePath(path)) {
            UCAppBundle bundle;
            if (!ReadApplicationBundle(path, bundle) || bundle.iconFile.empty())
                return nullptr;
            return LoadIconResource(bundle.iconFile, 0, desiredSize);
        }

        if (IsShellLinkPath(path)) {
            // A shortcut carries no icon of its own: it names one, in a file
            // somewhere else - the program it starts, or the .ico a browser
            // wrote for a web shortcut. Read the link, then extract from
            // whatever it named, and from its target when that fails.
            UCShellLink link;
            if (ReadShellLink(path, link)) {
                if (!link.hostIconLocation.empty()) {
                    if (auto pixmap = WindowsIcons::LoadIconResourcePixmap(
                                Utf8ToWide(link.hostIconLocation),
                                link.iconIndex, desiredSize))
                        return pixmap;
                }
                if (!link.hostTargetPath.empty()) {
                    const std::wstring target = Utf8ToWide(link.hostTargetPath);
                    if (auto pixmap = WindowsIcons::LoadIconResourcePixmap(
                                target, 0, desiredSize))
                        return pixmap;
                    // The target holds no icon resource (a shortcut to a
                    // document, a folder, a data file): its association has
                    // one, and that is what Explorer draws.
                    if (auto pixmap = AssociatedIconPixmap(target, desiredSize))
                        return pixmap;
                }
            }
            // An unreadable link, or a target this machine no longer has:
            // the shell still knows what Explorer draws for the link itself.
            return AssociatedIconPixmap(Utf8ToWide(path), desiredSize);
        }

        // Icon 0 is the one Explorer shows for the file itself.
        if (auto pixmap = WindowsIcons::LoadIconResourcePixmap(Utf8ToWide(path), 0,
                                                               desiredSize))
            return pixmap;
        // The shell can decline a file it has no handler for (an icon
        // library, a binary from another architecture). The portable reader
        // walks the resource directory itself and often still finds it.
        return LoadIconResource(path, 0, desiredSize);
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
