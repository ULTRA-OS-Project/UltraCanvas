// OS/MacOS/UltraCanvasMacOSScreenCapture.mm
// The screen captured on macOS, for UltraCanvasDesktopShell::CaptureScreen,
// CaptureScreenImage and CaptureScreenArea (ULTRACANVAS_DESKTOPSHELL_NATIVE_CAPTURE):
// UltraSnip, ChatCube's screenshot button, UltraAuthenticator's QR scan.
//
//   - How: ScreenCaptureKit's SCScreenshotManager (macOS 14, the oldest
//     macOS the suite runs on). CGDisplayCreateImage and
//     CGWindowListCreateImage are obsolete from the macOS 15 SDK on.
//   - What: the main display - the one with the menu bar - whole, at its
//     own pixels (a Retina display at twice its points), without the
//     pointer. Its top-left corner is 0,0. Other displays are not captured
//     yet.
//   - The permission: macOS asks the person once (Screen & System Audio
//     Recording, in Privacy & Security). Until it is given the capture
//     fails with the reason, and the first try opens the system's request;
//     the program needs starting again after it is granted.
//   - The pixels: B, G, R, unused, rows top to bottom - DesktopScreenImage's
//     layout, drawn into a bitmap context of that format.
//
// ScreenCaptureKit answers on a queue of its own; the call waits for it
// (at most ten seconds), so it may be made from the UI thread.
// Version: 1.0.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"

#ifdef ULTRACANVAS_DESKTOPSHELL_NATIVE_CAPTURE

#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasScreenAreaPicker.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#if __has_include(<ScreenCaptureKit/ScreenCaptureKit.h>)
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#define UC_HAVE_SCREENCAPTUREKIT 1
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {

constexpr int64_t kWaitSeconds = 10;

const char* const kNoPermission =
    "macOS has not allowed this program to record the screen. Allow it in System Settings > "
    "Privacy & Security > Screen & System Audio Recording, then start the program again.";

// `image` into `out` as B G R x rows, top to bottom
bool ImageToBgrx(CGImageRef image, DesktopScreenImage& out) {
    const size_t width = CGImageGetWidth(image);
    const size_t height = CGImageGetHeight(image);
    if (width == 0 || height == 0) return false;
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.stride = static_cast<int>(width * 4);
    out.pixels.assign(height * width * 4, 0);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    // A 32-bit word x R G B, little-endian: B G R x in memory
    CGContextRef context = CGBitmapContextCreate(out.pixels.data(), width, height, 8, width * 4, space,
                                                 kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(space);
    if (!context) {
        out = DesktopScreenImage();
        return false;
    }
    // A bitmap context's first row is the top of what is drawn
    CGContextDrawImage(context, CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), image);
    CGContextRelease(context);
    return true;
}

std::string Describe(NSError* error, const char* fallback) {
    if (error && error.localizedDescription) return std::string([error.localizedDescription UTF8String]);
    return fallback;
}

#ifdef UC_HAVE_SCREENCAPTUREKIT
API_AVAILABLE(macos(14.0))
bool CaptureMainDisplay(DesktopScreenImage& out, std::string& error) {
    @autoreleasepool {
        // Asking first: without the permission ScreenCaptureKit fails with
        // a less helpful error, and this opens the system's request once
        if (!CGPreflightScreenCaptureAccess()) {
            CGRequestScreenCaptureAccess();
            error = kNoPermission;
            return false;
        }

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block SCShareableContent* content = nil;
        __block NSError* failure = nil;
        [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                   onScreenWindowsOnly:YES
                                                     completionHandler:^(SCShareableContent* found, NSError* e) {
            content = found;
            failure = e;
            dispatch_semaphore_signal(done);
        }];
        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, kWaitSeconds * NSEC_PER_SEC)) != 0) {
            error = "macOS did not say which displays there are in time.";
            return false;
        }
        if (!content) {
            error = Describe(failure, kNoPermission);
            return false;
        }

        const CGDirectDisplayID mainId = CGMainDisplayID();
        SCDisplay* display = nil;
        for (SCDisplay* d in content.displays) {
            if (d.displayID == mainId) {
                display = d;
                break;
            }
        }
        if (!display) display = content.displays.firstObject;
        if (!display) {
            error = "macOS lists no display to capture.";
            return false;
        }

        // The display's own pixels, not its points
        size_t pixelWidth = static_cast<size_t>(display.width);
        size_t pixelHeight = static_cast<size_t>(display.height);
        if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display.displayID)) {
            pixelWidth = CGDisplayModeGetPixelWidth(mode);
            pixelHeight = CGDisplayModeGetPixelHeight(mode);
            CGDisplayModeRelease(mode);
        }

        SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
        SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
        configuration.width = pixelWidth;
        configuration.height = pixelHeight;
        configuration.showsCursor = NO;

        __block CGImageRef captured = NULL;
        failure = nil;
        [SCScreenshotManager captureImageWithFilter:filter
                                      configuration:configuration
                                  completionHandler:^(CGImageRef image, NSError* e) {
            if (image) captured = CGImageRetain(image);
            failure = e;
            dispatch_semaphore_signal(done);
        }];
        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, kWaitSeconds * NSEC_PER_SEC)) != 0) {
            // The handler may still come: it releases nothing then, the
            // image leaks once rather than the program waiting forever
            error = "macOS did not capture the screen in time.";
            return false;
        }
        if (!captured) {
            error = Describe(failure, "macOS did not capture the screen.");
            return false;
        }
        const bool ok = ImageToBgrx(captured, out);
        CGImageRelease(captured);
        if (!ok) {
            error = "The screen picture could not be read.";
            return false;
        }
        out.x = 0;
        out.y = 0;
        return true;
    }
}
#endif

} // namespace

bool CaptureScreenImage(DesktopScreenImage& out, std::string& error) {
    out = DesktopScreenImage();
#ifdef UC_HAVE_SCREENCAPTUREKIT
    if (@available(macOS 14.0, *)) {
        if (CaptureMainDisplay(out, error)) return true;
        out = DesktopScreenImage();
        return false;
    }
    error = "Screenshots need macOS 14 or later.";
    return false;
#else
    error = "This program was built without ScreenCaptureKit, so it cannot capture the screen.";
    return false;
#endif
}

bool CaptureScreenArea(int x, int y, int width, int height, DesktopScreenImage& out, std::string& error) {
    DesktopScreenImage whole;
    if (!CaptureScreenImage(whole, error)) {
        out = DesktopScreenImage();
        return false;
    }
    const Rect2Di area = ScreenArea::ClipRect(Rect2Di(x, y, width, height),
                                              Rect2Di(whole.x, whole.y, whole.width, whole.height));
    if (area.width <= 0 || area.height <= 0) {
        out = DesktopScreenImage();
        error = "The area is not on the screen.";
        return false;
    }
    out = DesktopScreenImage();
    out.x = area.x;
    out.y = area.y;
    out.width = area.width;
    out.height = area.height;
    out.stride = area.width * 4;
    out.pixels.resize(static_cast<size_t>(out.stride) * static_cast<size_t>(area.height));
    for (int row = 0; row < area.height; ++row) {
        const uint8_t* src = whole.pixels.data() + static_cast<size_t>(area.y - whole.y + row) * whole.stride +
                             static_cast<size_t>(area.x - whole.x) * 4;
        std::memcpy(out.pixels.data() + static_cast<size_t>(row) * out.stride, src, static_cast<size_t>(out.stride));
    }
    return true;
}

bool CaptureScreen(const std::string& pngPath, std::string& error) {
    DesktopScreenImage shot;
    if (!CaptureScreenImage(shot, error)) return false;
    ClipboardDib::RgbaImage image;
    if (!ScreenArea::CropToRgba(shot, Rect2Di(shot.x, shot.y, shot.width, shot.height), image)) {
        error = "The screen picture could not be read.";
        return false;
    }
    std::vector<uint8_t> png;
    if (!ClipboardDib::EncodePng(image, png)) {
        error = "The screen picture could not be written as PNG.";
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path target = PathFromUtf8(pngPath);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    std::FILE* file = OpenFileUtf8(pngPath, "wb");
    if (!file) {
        error = "The file \"" + pngPath + "\" could not be created.";
        return false;
    }
    const bool written = std::fwrite(png.data(), 1, png.size(), file) == png.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        fs::remove(target, ec);
        error = "The file \"" + pngPath + "\" could not be written.";
        return false;
    }
    return true;
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas

#endif // ULTRACANVAS_DESKTOPSHELL_NATIVE_CAPTURE
