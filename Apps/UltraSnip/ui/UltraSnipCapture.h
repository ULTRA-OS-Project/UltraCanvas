// Apps/UltraSnip/ui/UltraSnipCapture.h
// The arithmetic and the pixels of a snip, with no window in sight so a test
// can run it: a rectangle from two drag corners, the crop of a screen
// capture into straight RGBA (what the PNG writer, the clipboard and the
// image element take), the topmost window under a point, where the
// recording bar goes so it stays out of the recording, and a file written
// whole under its UTF-8 name.
// Version: 0.1.1 - SnipFilePath
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasDesktopShell.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraSnip {

// A window the overlay can pick: its outer rectangle in screen pixels
struct SnipWindowBounds {
    uint64_t id = 0;
    std::string title;
    UltraCanvas::Rect2Di bounds;
};

// The rectangle between two corners of a drag, either way round, both
// corners inside it (a click without a drag is a 1 x 1 rectangle)
UltraCanvas::Rect2Di RectBetween(const UltraCanvas::Point2Di& a, const UltraCanvas::Point2Di& b);

// `rect` cut down to `bounds`; empty (width or height 0) when they do not meet
UltraCanvas::Rect2Di ClipRect(const UltraCanvas::Rect2Di& rect, const UltraCanvas::Rect2Di& bounds);

// The screen area `area` (screen pixels, as ListScreens counts them) out of
// `shot` - whose own x / y say where it lies - as straight RGBA, opaque.
// The area is clipped to the shot; false when nothing of it is in the shot.
bool CropToRgba(const UltraCanvas::DesktopScreenImage& shot, const UltraCanvas::Rect2Di& area,
                UltraCanvas::ClipboardDib::RgbaImage& out, UltraCanvas::Rect2Di* taken = nullptr);

// The index in `windows` (bottom first, as ListWindows lists them) of the
// topmost window under `point`; -1 when none is
int TopmostWindowAt(const std::vector<SnipWindowBounds>& windows, const UltraCanvas::Point2Di& point);

// Where a bar of barWidth x barHeight goes so it does not lie over
// `region`: centred above it with `gap` between, else below it, else at the
// top of `screen` (the bar is then in the recording, which cannot be
// helped); always within `screen`.
UltraCanvas::Rect2Di PlaceBarOutside(const UltraCanvas::Rect2Di& region, const UltraCanvas::Rect2Di& screen,
                                     int barWidth, int barHeight, int gap);

// The file a new snip goes to: `defaultPath` (the shell's default name and
// folder, "<Pictures>/Screenshots/Screenshot <date> <time>.png") with its
// extension replaced by `extension` (with or without the dot) and, when
// `folder` is not empty, its folder replaced by that one
std::string SnipFilePath(const std::string& defaultPath, const std::string& folder, const std::string& extension);

// `seconds` as "m:ss" / "h:mm:ss"
std::string FormatElapsed(double seconds);

// Writes `bytes` to the UTF-8 path, making the directory first; false with
// the reason
bool WriteFileBytes(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error);

// Copies the file at `from` to `to` (UTF-8 paths), replacing one there;
// false with the reason
bool CopyFileTo(const std::string& from, const std::string& to, std::string& error);

} // namespace UltraSnip
