// OS/Linux/UltraCanvasLinuxCursor.cpp
// Linux X11/XCursor implementation for custom cursor support
// Version: 1.0.1
// Last Modified: 2025-12-30
// Author: UltraCanvas Framework

#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"

#include <X11/Xlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/cursorfont.h>
#include <cairo/cairo.h>
#include <vips/vips8>

#include <iostream>
#include <vector>
#include <cstring>

namespace UltraCanvas {

// ===== IMAGE LOADING WITH LIBVIPS =====
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

// ===== CREATE X CURSOR FROM RGBA =====
    static Cursor CreateXCursorFromImage(Display* display,
                                        const vips::VImage& image,
                                        int hotspotX, int hotspotY) {
        int width = image.width();
        int height = image.height();

        XcursorImage* img = XcursorImageCreate(width, height);
        if (!img) return None;

        img->xhot = hotspotX;
        img->yhot = hotspotY;

        auto pixels = static_cast<const unsigned char*>(image.data());

        // Convert RGBA to XCursor ARGB with premultiplied alpha
        for (int i = 0; i < width * height; i++) {
            unsigned char r = pixels[i * 4 + 0];
            unsigned char g = pixels[i * 4 + 1];
            unsigned char b = pixels[i * 4 + 2];
            unsigned char a = pixels[i * 4 + 3];

            // Premultiply alpha
            r = (r * a) / 255;
            g = (g * a) / 255;
            b = (b * a) / 255;

            img->pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }

        Cursor cursor = XcursorImageLoadCursor(display, img);
        XcursorImageDestroy(img);

        return cursor;
    }

    Cursor UltraCanvasLinuxApplication::LoadCursorFromImage(const char* filename, int hotspotX, int hotspotY) {
        if (!filename || !display) return 0;
        try {
            vips::VImage img = LoadImageForCursor(filename);
            // Create new cursor
            return CreateXCursorFromImage(display, img, hotspotX, hotspotY);
        } catch (const vips::VError& e) {
            std::cerr << "UltraCanvas Linux: libvips error loading " << filename << ": " << e.what() << std::endl;
        }
        return 0;
    }


// ===== PUBLIC API =====
    bool UltraCanvasLinuxApplication::SelectMouseCursorNative(UltraCanvasWindowBase *win, UCMouseCursor cur) {
        auto foundCursor = cursors.find(cur);
        auto xWindow = static_cast<Window>(win->GetNativeHandle());

        if (foundCursor != cursors.end()) {
            XDefineCursor(display, xWindow, foundCursor->second);
            XFlush(display);
            return true;
        }

        Cursor newCursor = None;

        // Map MousePointer enum to X11 cursor font shapes
        switch (cur) {
            case UCMouseCursor::Default:
                newCursor = XCreateFontCursor(display, XC_left_ptr);
                break;

            case UCMouseCursor::NoCursor:
                // Create invisible cursor using blank pixmap
            {
                Pixmap cursorPixmap = XCreatePixmap(display, xWindow, 1, 1, 1);
                XColor black;
                black.red = black.green = black.blue = 0;
                black.pixel = BlackPixel(display, GetScreen());
                newCursor = XCreatePixmapCursor(display, cursorPixmap, cursorPixmap,
                                                &black, &black, 0, 0);
                XFreePixmap(display, cursorPixmap);
            }
                break;

            case UCMouseCursor::Hand:
                newCursor = XCreateFontCursor(display, XC_hand2);
                break;

            case UCMouseCursor::Text:
                newCursor = XCreateFontCursor(display, XC_xterm);
                break;

            case UCMouseCursor::Wait:
                newCursor = XCreateFontCursor(display, XC_watch);
                break;

            case UCMouseCursor::Cross:
                newCursor = XCreateFontCursor(display, XC_crosshair);
                break;

            case UCMouseCursor::Help:
                newCursor = XCreateFontCursor(display, XC_question_arrow);
                break;

            case UCMouseCursor::NotAllowed:
                newCursor = XCreateFontCursor(display, XC_X_cursor);
                break;

            case UCMouseCursor::SizeAll:
                newCursor = XCreateFontCursor(display, XC_fleur);
                break;

            case UCMouseCursor::SizeNS:
                newCursor = XCreateFontCursor(display, XC_sb_v_double_arrow);
                break;

            case UCMouseCursor::SizeWE:
                newCursor = XCreateFontCursor(display, XC_sb_h_double_arrow);
                break;

            case UCMouseCursor::SizeNWSE:
                newCursor = XCreateFontCursor(display, XC_sizing);
                break;

            case UCMouseCursor::SizeNESW:
                newCursor = XCreateFontCursor(display, XC_sizing);
                break;

            case UCMouseCursor::LookingGlass:
                // Load cursor from image
                newCursor = LoadCursorFromImage("media/lib/cursor/looking-glass.png", 0, 0);
                break;
            default:
                newCursor = XCreateFontCursor(display, XC_left_ptr);
                break;
        }

        // Apply the cursor to the window
        if (newCursor != 0) {
            cursors[cur] = newCursor;
            XDefineCursor(display, xWindow, newCursor);
            XFlush(display);

//            if (currentXCursor != 0) {
//                XFreeCursor(display, currentXCursor);
//            }
        }
        return true;
    }

    bool UltraCanvasLinuxApplication::SelectMouseCursorNative(UltraCanvasWindowBase *win, UCMouseCursor cur, const char* filename, int hotspotX, int hotspotY) {
        auto foundCursor = cursors.find(cur);

        if (foundCursor != cursors.end()) {
            XDefineCursor(display, static_cast<Window>(win->GetNativeHandle()), foundCursor->second);
            XFlush(display);
            return true;
        }

        Cursor newCursor = LoadCursorFromImage(filename, hotspotX, hotspotY);
        if (!newCursor) {
            return false;
        }
        cursors[cur] = newCursor;

        Window xWin = static_cast<Window>(win->GetNativeHandle());
        XDefineCursor(display, xWin, newCursor);
        XFlush(display);


        std::cout << "UltraCanvas Linux: Custom cursor loaded from " << filename
                  << " (hotspot: " << hotspotX << "," << hotspotY << ")" << std::endl;
        return true;
    }
} // namespace UltraCanvas