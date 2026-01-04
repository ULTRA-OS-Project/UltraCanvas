// OS/MacOS/UltraCanvasCursorMacOS.mm
// macOS NSCursor implementation for custom cursor support
// Version: 1.0.1
// Last Modified: 2025-12-30
// Author: UltraCanvas Framework

#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"

#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>

#include <cairo/cairo.h>
#include <vips/vips8>

#include <iostream>
#include <vector>
#include <cstring>

namespace UltraCanvas {

// ===== STATIC STATE =====

    static vips::VImage LoadImageForCursor(const char* filename) {
        // Load image with libvips (supports PNG, JPEG, WebP, TIFF, etc.)
        vips::VImage image = vips::VImage::new_from_file(filename);

        // Convert to sRGB if needed
        if (image.interpretation() != VIPS_INTERPRETATION_sRGB) {
            image = image.colourspace(VIPS_INTERPRETATION_sRGB);
        }

        // Ensure we have 4 channels (RGBA)
        if (image.bands() == 1) {
            // Grayscale to RGB
            image = image.bandjoin({image, image});
        }
        if (image.bands() == 3) {
            // Add alpha channel (fully opaque)
            vips::VImage alpha = vips::VImage::black(image.width(), image.height()).invert();
            image = image.bandjoin(alpha);
        }

        // Ensure 8-bit per channel
        if (image.format() != VIPS_FORMAT_UCHAR) {
            image = image.cast(VIPS_FORMAT_UCHAR);
        }
        return image;
    }

// ===== CREATE NSCURSOR FROM RGBA =====
    static NSCursor* CreateNSCursorFromImage(const vips::VImage& image,
                                            int hotspotX, int hotspotY) {
        int width = image.width();
        int height = image.height();
        @autoreleasepool {
            // Create bitmap representation
            NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
                    initWithBitmapDataPlanes:nil
                                  pixelsWide:width
                                  pixelsHigh:height
                               bitsPerSample:8
                             samplesPerPixel:4
                                    hasAlpha:YES
                                    isPlanar:NO
                              colorSpaceName:NSDeviceRGBColorSpace
                                 bytesPerRow:width * 4
                                bitsPerPixel:32];

            if (!rep) return nil;

            auto pixels = static_cast<const unsigned char*>(image.data());

            // Copy pixel data
            memcpy([rep bitmapData], pixels, width * height * 4);

            // Create image
            NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
            [image addRepresentation:rep];

            // Create cursor
            NSCursor* cursor = [[NSCursor alloc] initWithImage:image
                                                       hotSpot:NSMakePoint(hotspotX, hotspotY)];
            return cursor;
        }
    }

// ===== PUBLIC API =====
    NSCursor* UltraCanvasMacOSApplication::LoadCursorFromImage(const char* filename, int hotspotX, int hotspotY) {
        if (!filename) return 0;
        try {
            vips::VImage img = LoadImageForCursor(filename);
            // Create new cursor
            return CreateNSCursorFromImage(img, hotspotX, hotspotY);
        } catch (const vips::VError& e) {
            std::cerr << "UltraCanvas MacOS: error loading cursor image " << filename << ": " << e.what() << std::endl;
        }
        return 0;
    }


    bool UltraCanvasMacOSApplication::SelectMouseCursorNative(UltraCanvasWindowBase *win, UCMouseCursor cur) {
        @autoreleasepool {
            auto foundCursor = cursors.find(cur);
            if (foundCursor != cursors.end()) {
                [foundCursor->second set];
                return true;
            }

            NSCursor* cursor = nil;

            // Map MousePointer enum to NSCursor
            switch (cur) {
                case UCMouseCursor::Default:
                    cursor = [NSCursor arrowCursor];
                    break;

                case UCMouseCursor::NoCursor:
                    // Hide cursor - use an invisible cursor
                    // Note: NSCursor doesn't have a built-in invisible cursor
                    // We use hideCursor but need to be careful with show/hide balance
                    //[NSCursor hide];
                    return true;

                case UCMouseCursor::Hand:
                    cursor = [NSCursor pointingHandCursor];
                    break;

                case UCMouseCursor::Text:
                    cursor = [NSCursor IBeamCursor];
                    break;

                case UCMouseCursor::Wait:
                    // macOS doesn't have a standard wait cursor
                    // Use the spinning beach ball effect is system-managed
                    // Fall back to arrow cursor
                    cursor = [NSCursor arrowCursor];
                    break;

                case UCMouseCursor::Cross:
                    cursor = [NSCursor crosshairCursor];
                    break;

                case UCMouseCursor::Help:
                    // macOS 10.15+ has contextualMenuCursor but no help cursor
                    // Fall back to arrow cursor
                    cursor = [NSCursor arrowCursor];
                    break;

                case UCMouseCursor::NotAllowed:
                    cursor = [NSCursor operationNotAllowedCursor];
                    break;

                case UCMouseCursor::LookingGlass:
                    // Load cursor from image
                    cursor = LoadCursorFromImage("media/lib/cursor/looking-glass.png", 0, 0);
                    break;

                case UCMouseCursor::SizeAll:
                    // macOS doesn't have a size-all cursor
                    // Use openHandCursor as closest equivalent
                    cursor = [NSCursor openHandCursor];
                    break;

                case UCMouseCursor::SizeNS:
                    cursor = [NSCursor resizeUpDownCursor];
                    break;

                case UCMouseCursor::SizeWE:
                    cursor = [NSCursor resizeLeftRightCursor];
                    break;

                case UCMouseCursor::SizeNWSE:
                    // macOS doesn't have diagonal resize cursors by default
                    // Use a generic resize cursor if available, otherwise arrow
                    // On macOS 11+, we could use _windowResizeNorthWestSouthEastCursor
                    // but it's private API. Fall back to arrow.
                    cursor = [NSCursor arrowCursor];
                    break;

                case UCMouseCursor::SizeNESW:
                    // Same as above - no diagonal resize cursor available
                    cursor = [NSCursor arrowCursor];
                    break;

                default:
                    cursor = [NSCursor arrowCursor];
                    break;
            }

            // If transitioning from NoCursor, ensure cursor is visible
//            if (currentMousePointer == UCMouseCursor::NoCursor) {
//                [NSCursor unhide];
//            }

            if (cursor) {
                [cursor set];
                return true;
            }
        }
        return false;
    }


    bool UltraCanvasMacOSApplication::SelectMouseCursorNative(UltraCanvasWindowBase *win, UCMouseCursor cur, const char* filename, int hotspotX, int hotspotY) {
        auto foundCursor = cursors.find(cur);

        if (foundCursor != cursors.end()) {
            [foundCursor->second set];
            return true;
        }

        NSCursor* newCursor = LoadCursorFromImage(filename, hotspotX, hotspotY);
        if (!newCursor) {
            return false;
        }
        cursors[cur] = newCursor;

        [newCursor set];


        std::cout << "UltraCanvas MacOS: Custom cursor loaded from " << filename
                  << " (hotspot: " << hotspotX << "," << hotspotY << ")" << std::endl;
        return true;
    }

//    bool SelectCursor(const char* filename, int hotspotX, int hotspotY) {
//        if (!filename) return false;
//
//        @autoreleasepool {
//            // Load image with libvips
//            std::vector<unsigned char> pixels;
//            int width, height;
//            if (!LoadImageToRGBA(filename, pixels, width, height)) return false;
//
//            // Free previous cursor
//            if (g_CustomCursor) {
//                g_CustomCursor = nil;
//            }
//
//            // Create new cursor
//            g_CustomCursor = CreateNSCursorFromRGBA(pixels.data(), width, height, hotspotX, hotspotY);
//            if (!g_CustomCursor) return false;
//
//            g_CustomCursorActive = true;
//            [g_CustomCursor set];
//
//            std::cout << "UltraCanvas macOS: Custom cursor loaded from " << filename
//                      << " (hotspot: " << hotspotX << "," << hotspotY << ")" << std::endl;
//            return true;
//        }
//    }

} // namespace UltraCanvas