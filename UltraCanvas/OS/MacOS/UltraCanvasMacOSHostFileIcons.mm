// OS/MacOS/UltraCanvasMacOSHostFileIcons.mm
// The macOS backend of UltraCanvasHostFileIcons: the icon Finder draws for a
// file of this type, from NSWorkspace.
//
// The type is decided from HostFileIconProbeName - the key's type and nothing
// of the file - so one lookup serves every file of a kind, a path on an
// unmounted volume still gets an icon, and the answer cannot be one file's
// own custom icon standing in for every file of its kind. On macOS 11 and
// later that is the icon for the content type the extension names; before
// that, the same question through the older file-type call. The path itself
// is never handed to NSWorkspace: iconForFile: reads a file's custom icon,
// and the caller holds the answer under a per-type key.
//
// Rendering an NSImage into a bitmap is the one piece of AppKit drawing that
// is safe off the main thread, which is where a file display calls this from
// - the same reason the "Open with" backend extracts its handler icons that
// way (OS/MacOS/UltraCanvasMacOSFileAssociations.mm).
// Version: 1.1.0 - asked by type only (HostFileIconProbeName), never with the
//                  file's path; the content-type branch is compiled in (its
//                  macro was never defined here, so every lookup read the
//                  file itself and a folder drew the startup disk's icon)
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework
#include "UltraCanvasHostFileIcons.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
// The same detection as the "Open with" backend: the macro is this file's
// own, and without it the content-type branch below is compiled out.
#if __has_include(<UniformTypeIdentifiers/UniformTypeIdentifiers.h>)
#  import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#  define ULTRACANVAS_HAS_UTTYPE 1
#endif

#include <cairo/cairo.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace UltraCanvas {

    bool HostFileIconsAvailable() { return true; }

    namespace {

        constexpr int kMinIconEdge = 16;
        constexpr int kMaxIconEdge = 512;

        std::string ExtensionOf(const std::string& path) {
            const size_t slash = path.find_last_of('/');
            const std::string name = slash == std::string::npos
                                     ? path : path.substr(slash + 1);
            const size_t dot = name.find_last_of('.');
            if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size())
                return {};
            return name.substr(dot + 1);
        }

        // The type icon, asked for by kind rather than for this one file:
        // from the name HostFileIconProbeName makes, so two files that share
        // a key cannot get two answers.
        NSImage* TypeIcon(const std::string& path, bool isDirectory) {
            NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
            const std::string extension =
                    isDirectory ? std::string()
                                : ExtensionOf(HostFileIconProbeName(path, false));
            // Nil for bytes that are not UTF-8; then there is no type to ask.
            NSString* extensionText =
                    extension.empty()
                            ? nil
                            : [NSString stringWithUTF8String:extension.c_str()];
#if defined(ULTRACANVAS_HAS_UTTYPE)
            if (@available(macOS 11.0, *)) {
                if (isDirectory) return [workspace iconForContentType:UTTypeFolder];
                if (extensionText) {
                    UTType* type = [UTType typeWithFilenameExtension:extensionText];
                    if (type) return [workspace iconForContentType:type];
                }
                // No extension: the plain document Finder draws for it.
                return [workspace iconForContentType:UTTypeData];
            }
#endif
            // Before macOS 11 (or an SDK without the content types): the
            // file-type call, which takes an extension or a type identifier
            // and, like the call above, never looks at a file.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            NSString* fileType = isDirectory      ? @"public.folder"
                                 : extensionText ? extensionText
                                                 : @"public.data";
            NSImage* icon = [workspace iconForFileType:fileType];
#pragma clang diagnostic pop
            return icon;
        }

        // NSImage → a premultiplied ARGB32 pixmap, by drawing it into a
        // bitmap of the size asked for. That is why this backend needs no
        // FitHostIconToSize: the answer is never larger than `edge`.
        std::shared_ptr<UCPixmap> PixmapFromImage(NSImage* image, int edge) {
            if (!image) return nullptr;
            NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
                    initWithBitmapDataPlanes:NULL
                                  pixelsWide:edge
                                  pixelsHigh:edge
                               bitsPerSample:8
                             samplesPerPixel:4
                                    hasAlpha:YES
                                    isPlanar:NO
                              colorSpaceName:NSCalibratedRGBColorSpace
                                 bytesPerRow:0
                                bitsPerPixel:0];
            if (!rep) return nullptr;
            NSGraphicsContext* context =
                    [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
            if (!context) return nullptr;
            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext:context];
            [image drawInRect:NSMakeRect(0, 0, edge, edge)
                     fromRect:NSZeroRect
                    operation:NSCompositingOperationSourceOver
                     fraction:1.0];
            [NSGraphicsContext restoreGraphicsState];

            const uint8_t* src = [rep bitmapData];
            if (!src) return nullptr;
            const NSInteger srcStride = [rep bytesPerRow];

            auto pixmap = std::make_shared<UCPixmap>(edge, edge);
            cairo_surface_t* surface = pixmap->GetSurface();
            if (!surface || !pixmap->IsValid()) return nullptr;
            cairo_surface_flush(surface);
            uint8_t* pixels = cairo_image_surface_get_data(surface);
            if (!pixels) return nullptr;
            const int stride = cairo_image_surface_get_stride(surface);

            // The rep holds straight-alpha R,G,B,A bytes; Cairo's ARGB32 is
            // premultiplied native-endian words, which on a little-endian Mac
            // is B,G,R,A in memory.
            for (int y = 0; y < edge; ++y) {
                const uint8_t* row = src + static_cast<size_t>(y) * srcStride;
                uint32_t* out = reinterpret_cast<uint32_t*>(
                        pixels + static_cast<size_t>(y) * stride);
                for (int x = 0; x < edge; ++x) {
                    const uint8_t r = row[x * 4 + 0];
                    const uint8_t g = row[x * 4 + 1];
                    const uint8_t b = row[x * 4 + 2];
                    const uint8_t a = row[x * 4 + 3];
                    out[x] = (static_cast<uint32_t>(a) << 24)
                           | (static_cast<uint32_t>(r * a / 255) << 16)
                           | (static_cast<uint32_t>(g * a / 255) << 8)
                           |  static_cast<uint32_t>(b * a / 255);
                }
            }
            pixmap->MarkDirty();
            return pixmap;
        }

    } // namespace

    std::shared_ptr<UCPixmap> LoadHostFileIconPixmap(const std::string& path,
                                                     bool isDirectory,
                                                     int desiredSize) {
        if (path.empty()) return nullptr;
        const int edge = std::max(kMinIconEdge,
                                  std::min(desiredSize, kMaxIconEdge));
        @autoreleasepool {
            return PixmapFromImage(TypeIcon(path, isDirectory), edge);
        }
    }

    void RefreshHostFileIcons() {
        // Launch Services keeps the icons and reloads them when an
        // application registers or the theme changes; the caller dropping its
        // pixmaps is the whole refresh.
    }

} // namespace UltraCanvas
