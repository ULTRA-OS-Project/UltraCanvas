// OS/MSWindows/UltraCanvasWindowsHostFileIcons.cpp
// The Windows backend of UltraCanvasHostFileIcons: the icon Explorer draws
// for a file of this type, taken from the shell's own system image list - the
// list Explorer itself draws from, so a ".txt" in an UltraCanvas file display
// and a ".txt" in an Explorer window are the same picture.
//
// The type is decided from the file NAME alone (SHGFI_USEFILEATTRIBUTES): the
// shell answers from the registry without opening, or even finding, the file.
// That is what makes this a type lookup rather than a file lookup - one call
// per extension serves a folder of ten thousand files, and a path on a
// disconnected volume still gets its icon.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#include "UltraCanvasHostFileIcons.h"
#include "UltraCanvasWindowsIcons.h"

#include <windows.h>
#include <shlobj.h>          // SHGetImageList
#include <shellapi.h>        // SHGetFileInfoW
#include <commoncontrols.h>  // IImageList (its IID is defined below)

#include <cairo/cairo.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace UltraCanvas {

    bool HostFileIconsAvailable() { return true; }

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

        // commoncontrols.h only DECLARES IID_IImageList: the value itself
        // lives in a uuid import library, and mingw-w64's does not carry this
        // one, so the Windows build linked with an undefined symbol. The
        // value is a Windows constant and is spelled out here instead, which
        // costs nothing and needs no library on any toolchain. (Verified
        // against the SDK header's own DEFINE_GUID, byte for byte.)
        constexpr GUID kImageListIid = {
            0x46eb5926, 0x582e, 0x4017,
            { 0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50 }
        };

        // The shell's icon lists from the smallest icon to the largest -
        // SHIL_ constants are identifiers, not sizes, so the order has to be
        // written down. SHIL_SYSSMALL is the small list again at the system
        // metric and is left out.
        constexpr int kListsBySize[] = { SHIL_SMALL, SHIL_LARGE,
                                         SHIL_EXTRALARGE, SHIL_JUMBO };

        // The lists worth trying for this size, best first: the smallest one
        // that will not be scaled up, then down through the rest, so a shell
        // that refuses the jumbo list still answers with something.
        std::vector<int> ListsForSize(int desiredSize) {
            // 16 / 32 / 48 / 256 at 100% DPI, in the order above.
            constexpr int kEdges[] = { 16, 32, 48, 256 };
            size_t first = 0;
            while (first + 1 < std::size(kEdges) && kEdges[first] < desiredSize)
                ++first;
            std::vector<int> lists;
            for (size_t i = first + 1; i-- > 0;) lists.push_back(kListsBySize[i]);
            return lists;
        }

        // The jumbo list is a 256x256 canvas, and a file type whose icon only
        // exists at 48 is centred in it with transparent space all round.
        // Drawn into a tile that way it would be a postage stamp in the
        // middle of the icon box, so the empty border comes off and the
        // caller scales what is actually there. Every icon gets this: a
        // resource with a transparent margin of its own was being drawn small
        // for the same reason.
        std::shared_ptr<UCPixmap> TrimTransparentBorder(
                const std::shared_ptr<UCPixmap>& pixmap) {
            if (!pixmap || !pixmap->IsValid()) return pixmap;
            cairo_surface_t* surface = pixmap->GetSurface();
            if (!surface) return pixmap;
            cairo_surface_flush(surface);
            const uint8_t* pixels = cairo_image_surface_get_data(surface);
            if (!pixels) return pixmap;
            const int stride = cairo_image_surface_get_stride(surface);
            const int w = pixmap->GetRawWidth();
            const int h = pixmap->GetRawHeight();
            if (w <= 0 || h <= 0) return pixmap;

            int minX = w, minY = h, maxX = -1, maxY = -1;
            for (int y = 0; y < h; ++y) {
                const uint32_t* row = reinterpret_cast<const uint32_t*>(
                        pixels + static_cast<size_t>(y) * stride);
                for (int x = 0; x < w; ++x) {
                    if ((row[x] >> 24) == 0) continue;   // fully transparent
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
            // Nothing drawn at all, or nothing to trim.
            if (maxX < minX || maxY < minY) return pixmap;
            if (minX == 0 && minY == 0 && maxX == w - 1 && maxY == h - 1)
                return pixmap;

            const int outW = maxX - minX + 1;
            const int outH = maxY - minY + 1;
            auto out = std::make_shared<UCPixmap>(outW, outH);
            cairo_surface_t* outSurface = out->GetSurface();
            if (!outSurface || !out->IsValid()) return pixmap;
            cairo_surface_flush(outSurface);
            uint8_t* outPixels = cairo_image_surface_get_data(outSurface);
            if (!outPixels) return pixmap;
            const int outStride = cairo_image_surface_get_stride(outSurface);
            for (int y = 0; y < outH; ++y) {
                const uint8_t* src = pixels +
                        static_cast<size_t>(minY + y) * stride +
                        static_cast<size_t>(minX) * 4;
                uint8_t* dst = outPixels + static_cast<size_t>(y) * outStride;
                std::memcpy(dst, src, static_cast<size_t>(outW) * 4);
            }
            out->MarkDirty();
            return out;
        }

        // The shell's icon index for a file NAME - no disk access, and no
        // requirement that the file exists.
        bool SystemIconIndex(const std::wstring& path, bool isDirectory,
                             int& outIndex) {
            SHFILEINFOW info{};
            const DWORD attributes = isDirectory ? FILE_ATTRIBUTE_DIRECTORY
                                                 : FILE_ATTRIBUTE_NORMAL;
            if (!SHGetFileInfoW(path.c_str(), attributes, &info, sizeof(info),
                                SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES))
                return false;
            outIndex = info.iIcon;
            return true;
        }

        std::shared_ptr<UCPixmap> IconFromImageList(int list, int index) {
            IImageList* images = nullptr;
            if (FAILED(SHGetImageList(list, kImageListIid,
                                      reinterpret_cast<void**>(&images))) ||
                !images)
                return nullptr;
            HICON icon = nullptr;
            const HRESULT hr = images->GetIcon(index, ILD_TRANSPARENT, &icon);
            // The lists are shell-owned singletons: released, never destroyed.
            images->Release();
            if (FAILED(hr) || !icon) return nullptr;
            std::shared_ptr<UCPixmap> pixmap = WindowsIcons::PixmapFromIcon(icon);
            DestroyIcon(icon);
            return pixmap;
        }

        // Without an image list (a policy-locked shell, an old system): the
        // two icon sizes SHGetFileInfo itself serves.
        std::shared_ptr<UCPixmap> AssociatedIcon(const std::wstring& path,
                                                 bool isDirectory,
                                                 int desiredSize) {
            SHFILEINFOW info{};
            const DWORD attributes = isDirectory ? FILE_ATTRIBUTE_DIRECTORY
                                                 : FILE_ATTRIBUTE_NORMAL;
            const UINT flags = SHGFI_ICON | SHGFI_USEFILEATTRIBUTES |
                               (desiredSize > 16 ? SHGFI_LARGEICON
                                                 : SHGFI_SMALLICON);
            if (!SHGetFileInfoW(path.c_str(), attributes, &info, sizeof(info),
                                flags) ||
                !info.hIcon)
                return nullptr;
            std::shared_ptr<UCPixmap> pixmap =
                    WindowsIcons::PixmapFromIcon(info.hIcon);
            DestroyIcon(info.hIcon);
            return pixmap;
        }

    } // namespace

    std::shared_ptr<UCPixmap> LoadHostFileIconPixmap(const std::string& path,
                                                     bool isDirectory,
                                                     int desiredSize) {
        const std::wstring wide = Utf8ToWide(path);
        if (wide.empty()) return nullptr;

        int index = 0;
        if (SystemIconIndex(wide, isDirectory, index)) {
            for (int list : ListsForSize(desiredSize)) {
                if (auto pixmap = IconFromImageList(list, index))
                    return TrimTransparentBorder(pixmap);
            }
        }
        if (auto pixmap = AssociatedIcon(wide, isDirectory, desiredSize))
            return TrimTransparentBorder(pixmap);
        return nullptr;
    }

    void RefreshHostFileIcons() {
        // The shell keeps the icon cache itself and rebuilds it when the
        // theme or an association changes, so there is nothing of ours to
        // drop - the caller dropping its pixmaps is the whole refresh.
    }

} // namespace UltraCanvas
