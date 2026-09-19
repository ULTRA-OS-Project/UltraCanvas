// OS/MacOS/UltraCanvasMacOSHostFileIcons.mm
// The macOS backend of UltraCanvasHostFileIcons: the icon Finder draws for a
// file of this type, from NSWorkspace.
//
// The type is decided from the file NAME - the icon for the content type the
// extension names, never the icon of the file itself - so one lookup serves
// every file of a kind and a path on an unmounted volume still gets an icon.
// Where the content-type API is not available (an SDK older than macOS 11,
// or a name with no extension) the path itself is asked instead; that reads
// the file's own custom icon when it has one, which is what Finder shows
// there too.
//
// Rendering an NSImage into a bitmap is the one piece of AppKit drawing that
// is safe off the main thread, which is where a file display calls this from
// - the same reason the "Open with" backend extracts its handler icons that
// way (OS/MacOS/UltraCanvasMacOSFileAssociations.mm).
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#include "UltraCanvasHostFileIcons.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#if defined(ULTRACANVAS_HAS_UTTYPE)
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
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

        // The type icon, asked for by kind rather than for this one file.
        NSImage* TypeIcon(const std::string& path, bool isDirectory) {
            NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
            if (isDirectory) {
#if defined(ULTRACANVAS_HAS_UTTYPE)
                if (@available(macOS 11.0, *))
                    return [workspace iconForContentType:UTTypeFolder];
#endif
                return [workspace iconForFile:@"/"];
            }
#if defined(ULTRACANVAS_HAS_UTTYPE)
            if (@available(macOS 11.0, *)) {
                const std::string extension = ExtensionOf(path);
                if (!extension.empty()) {
                    UTType* type = [UTType typeWithFilenameExtension:
                            [NSString stringWithUTF8String:extension.c_str()]];
                    if (type) return [workspace iconForContentType:type];
                }
            }
#endif
            // No content type to ask about: the file's own icon, which for
            // anything without a custom one is its type's icon anyway.
            return [workspace iconForFile:
                    [NSString stringWithUTF8String:path.c_str()]];
        }

        // NSImage → a premultiplied ARGB32 pixmap, by drawing it into a
        // bitmap of the size asked for.
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
