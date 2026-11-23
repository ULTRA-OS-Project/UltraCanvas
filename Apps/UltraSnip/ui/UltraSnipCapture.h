// Apps/UltraSnip/ui/UltraSnipCapture.h
// The arithmetic of a snip, with no window in sight so a test can run it:
// where the recording bar goes so it stays out of the recording, the file a
// snip goes to, and a file written whole under its UTF-8 name.
// Version: 0.2.0 - RectBetween, ClipRect, CropToRgba, TopmostWindowAt and SnipWindowBounds moved to
//                  the framework (UltraCanvasScreenAreaPicker.h), named here as before
// Version: 0.1.1 - SnipFilePath
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasScreenAreaPicker.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraSnip {

// The rectangle arithmetic, the crop and the window under a point are the
// framework's (UltraCanvasScreenAreaPicker.h, ScreenArea), where they moved
// with the overlay
using SnipWindowBounds = UltraCanvas::ScreenAreaWindow;
using UltraCanvas::ScreenArea::RectBetween;
using UltraCanvas::ScreenArea::ClipRect;
using UltraCanvas::ScreenArea::CropToRgba;
using UltraCanvas::ScreenArea::TopmostWindowAt;

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
