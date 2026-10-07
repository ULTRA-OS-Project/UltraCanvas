// OS/MSWindows/UltraCanvasWindowsCursor.cpp
// Win32 cursor management implementation
// Version: 1.2.0 - image cursors kept per screen scaling; magnifier from its picture
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework

// The image header goes before windows.h, as in UltraCanvasWindowsWindow.cpp.
#include "../../include/UltraCanvasImage.h"
#include "../../include/UltraCanvasUtils.h"
#include "UltraCanvasWindowsApplication.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    namespace {
        // Win32 reads only .cur / .ani / .ico from a file, so a PNG or SVG
        // cursor has to be decoded by us. UCImage does that (SVG straight from
        // the vector, so it is sharp at every DPI) and the pixels become the
        // 32-bit colour bitmap of an alpha cursor.
        HCURSOR CursorFromDecodedImage(const std::string& path, int hotspotX, int hotspotY,
                                       float scale) {
            auto img = UCImage::Get(path);
            if (!img || !img->IsValid()) return nullptr;
            if (!(scale > 0.0f)) scale = 1.0f;
            const int w = std::max(1, static_cast<int>(std::lround(img->GetWidth() * scale)));
            const int h = std::max(1, static_cast<int>(std::lround(img->GetHeight() * scale)));
            auto pixmap = img->GetPixmap(w, h, ImageFitMode::Fill);
            if (!pixmap || !pixmap->IsValid()) return nullptr;
            return UltraCanvasWindowsApplication::IconFromPixmap(
                    *pixmap, true,
                    static_cast<int>(std::lround(hotspotX * scale)),
                    static_cast<int>(std::lround(hotspotY * scale)));
        }

        float CursorScale(UltraCanvasWindowBase* win) {
            return win ? win->GetDeviceScale() : 1.0f;
        }
    } // namespace

    HICON UltraCanvasWindowsApplication::IconFromPixmap(UCPixmapCairo& pixmap, bool cursor,
                                                        int hotspotX, int hotspotY) {
        if (!pixmap.IsValid()) return nullptr;
        pixmap.Flush();

        cairo_surface_t* surface = pixmap.GetSurface();
        const int pw = cairo_image_surface_get_width(surface);
        const int ph = cairo_image_surface_get_height(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        const unsigned char* src = cairo_image_surface_get_data(surface);
        if (!src || pw <= 0 || ph <= 0) return nullptr;

        BITMAPV5HEADER bi = {};
        bi.bV5Size = sizeof(BITMAPV5HEADER);
        bi.bV5Width = pw;
        bi.bV5Height = -ph;   // top-down, like the cairo surface
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask   = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask  = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;

        void* bits = nullptr;
        HDC screenDC = GetDC(nullptr);
        HBITMAP color = CreateDIBSection(screenDC, reinterpret_cast<BITMAPINFO*>(&bi),
                                         DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screenDC);
        if (!color || !bits) {
            if (color) DeleteObject(color);
            return nullptr;
        }

        // Same byte order (BGRA in memory), but cairo premultiplies and the
        // colour bitmap of an icon or cursor carries straight alpha - Windows
        // premultiplies it itself when drawing. Copied as it was, every soft
        // edge came out too dark. Undo it on the edge pixels.
        auto* dst = static_cast<uint32_t*>(bits);
        for (int y = 0; y < ph; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(src + static_cast<size_t>(y) * stride);
            for (int x = 0; x < pw; ++x) {
                uint32_t p = row[x];
                const uint32_t a = p >> 24;
                if (a != 0 && a != 255) {
                    auto unmul = [a](uint32_t c) {
                        return std::min<uint32_t>(255, (c * 255 + a / 2) / a);
                    };
                    p = (a << 24) | (unmul((p >> 16) & 0xFF) << 16)
                        | (unmul((p >> 8) & 0xFF) << 8) | unmul(p & 0xFF);
                }
                *dst++ = p;
            }
        }

        // Monochrome mask, all zero: the alpha channel decides.
        HBITMAP mask = CreateBitmap(pw, ph, 1, 1, nullptr);

        ICONINFO info = {};
        info.fIcon = cursor ? FALSE : TRUE;
        info.xHotspot = cursor ? static_cast<DWORD>(std::clamp(hotspotX, 0, pw - 1)) : 0;
        info.yHotspot = cursor ? static_cast<DWORD>(std::clamp(hotspotY, 0, ph - 1)) : 0;
        info.hbmMask = mask;
        info.hbmColor = color;
        HICON icon = mask ? CreateIconIndirect(&info) : nullptr;

        DeleteObject(color);
        if (mask) DeleteObject(mask);
        return icon;
    }

    bool UltraCanvasWindowsApplication::SelectMouseCursorNative(
            UltraCanvasWindowBase* win, UCMouseCursor cursor) {

        // A cursor drawn from a picture, at the scaling of the screen this
        // window is on (one is drawn for each scaling it is asked for).
        const float scale = CursorScale(win);
        if (imageCursors.HasSource(cursor)) {
            if (HCURSOR image = imageCursors.Get(cursor, scale)) {
                SetCursor(image);
                return true;
            }
        }

        auto it = cursors.find(cursor);
        if (it != cursors.end()) {
            SetCursor(it->second);
            return true;
        }

        // The first time a picture cursor is asked for: register its file.
        // The first file that can be drawn wins; when none can, the case
        // falls back to a system shape, cached like any other.
        auto useImage = [&](std::initializer_list<const char*> files) {
            for (const char* file : files) {
                if (imageCursors.SetSource(cursor, NormalizePath(GetResourcesDir() + file),
                                           0, 0, scale)) {
                    SetCursor(imageCursors.Get(cursor, scale));
                    return true;
                }
            }
            return false;
        };

        HCURSOR hCursor = nullptr;
        LPCTSTR cursorId = nullptr;

        switch (cursor) {
            case UCMouseCursor::Arrow:
                cursorId = IDC_ARROW;
                break;
            case UCMouseCursor::NoCursor:
                cursors[cursor] = nullptr;
                SetCursor(nullptr);
                return true;
            case UCMouseCursor::Hand:
                cursorId = IDC_HAND;
                break;
            case UCMouseCursor::Text:
                cursorId = IDC_IBEAM;
                break;
            case UCMouseCursor::Wait:
                cursorId = IDC_WAIT;
                break;
            case UCMouseCursor::AppStarting:
                // Arrow plus busy ring - Explorer's "the program is starting"
                // pointer. The window stays clickable while it is up.
                cursorId = IDC_APPSTARTING;
                break;
            case UCMouseCursor::Cross:
                cursorId = IDC_CROSS;
                break;
            case UCMouseCursor::Help:
                cursorId = IDC_HELP;
                break;
            case UCMouseCursor::NotAllowed:
                cursorId = IDC_NO;
                break;
            case UCMouseCursor::LookingGlass:
                // Windows has no magnifier of its own: draw ours, as Linux
                // and macOS do. The crosshair is only the fallback.
                if (useImage({"media/lib/cursor/looking-glass.png"})) return true;
                cursorId = IDC_CROSS;
                break;
            case UCMouseCursor::SizeAll:
                cursorId = IDC_SIZEALL;
                break;
            case UCMouseCursor::SizeNS:
                cursorId = IDC_SIZENS;
                break;
            case UCMouseCursor::SizeWE:
                cursorId = IDC_SIZEWE;
                break;
            case UCMouseCursor::SizeNWSE:
                cursorId = IDC_SIZENWSE;
                break;
            case UCMouseCursor::SizeNESW:
                cursorId = IDC_SIZENESW;
                break;
            case UCMouseCursor::ContextMenu:
                // Drawn from the SVG at the window's DPI so it is sharp at
                // 125 % / 150 % / 200 %; the PNG is the same picture at 32 px,
                // for a build that cannot render SVG.
                if (useImage({"media/lib/cursor/context-menu.svg",
                              "media/lib/cursor/context-menu.png"})) return true;
                cursorId = IDC_ARROW;  // Fallback if image missing
                break;
            default:
                cursorId = IDC_ARROW;
                break;
        }

        if (cursorId) {
            hCursor = LoadCursor(nullptr, cursorId);
        }

        if (hCursor) {
            cursors[cursor] = hCursor;
            SetCursor(hCursor);
            return true;
        }

        // Fallback to arrow
        hCursor = LoadCursor(nullptr, IDC_ARROW);
        cursors[cursor] = hCursor;
        SetCursor(hCursor);
        return true;
    }

    bool UltraCanvasWindowsApplication::SelectMouseCursorNative(
            UltraCanvasWindowBase* win, UCMouseCursor cursor,
            const char* filename, int hotspotX, int hotspotY) {

        if (!filename || filename[0] == '\0') {
            return SelectMouseCursorNative(win, cursor);
        }

        // Registered with its file, so a later scaling - the window moved to
        // another screen - draws it again at the size that screen needs.
        const float scale = CursorScale(win);
        if (imageCursors.SetSource(cursor, filename, hotspotX, hotspotY, scale)) {
            SetCursor(imageCursors.Get(cursor, scale));
            return true;
        }

        debugOutput << "UltraCanvas Cursor: Failed to load cursor from '"
                  << filename << "'" << std::endl;
        return SelectMouseCursorNative(win, cursor);
    }

    HCURSOR UltraCanvasWindowsApplication::LoadCursorFromImageFile(
            const char* filename, int hotspotX, int hotspotY, float scale) {

        if (!filename || filename[0] == '\0') return nullptr;
        std::wstring wpath = Utf8ToUtf16(filename);

        // Try loading as .cur or .ani file first
        HCURSOR hCursor = LoadCursorFromFileW(wpath.c_str());
        if (hCursor) {
            return hCursor;
        }

        // Try loading as an icon file (.ico) and convert to cursor
        HICON hIcon = static_cast<HICON>(LoadImageW(
            nullptr, wpath.c_str(), IMAGE_ICON, 0, 0,
            LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (hIcon) {
            ICONINFO iconInfo = {};
            if (GetIconInfo(hIcon, &iconInfo)) {
                iconInfo.fIcon = FALSE;  // Mark as cursor
                iconInfo.xHotspot = hotspotX;
                iconInfo.yHotspot = hotspotY;
                hCursor = CreateIconIndirect(&iconInfo);
                if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
                if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
            }
            DestroyIcon(hIcon);
            return hCursor;
        }

        // PNG, SVG and the rest: neither call above reads them, which left the
        // context-menu and colour-picker cursors as the plain arrow.
        return CursorFromDecodedImage(filename, hotspotX, hotspotY, scale);
    }

} // namespace UltraCanvas
