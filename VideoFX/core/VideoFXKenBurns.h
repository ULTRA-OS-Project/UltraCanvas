// VideoFX/core/VideoFXKenBurns.h
// Internal: the camera over a still image - motion presets, the view at a
// point in time, and the frame renderer. Pure C++, no FFmpeg, unit-tested.
// Version: 0.3.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFXTypes.h"

#include <cstdint>
#include <string>

namespace VideoFX {
namespace Internal {

// Check a motion's values (Custom zooms 1..4, centres 0..1)
bool ValidateMotion(const VideoFXImageMotion& motion, std::string& error);

// Presets and Auto turned into explicit Custom values for an image of
// imageW x imageH shown in an outW x outH frame. `segmentIndex` varies Auto.
// Still stays Still.
VideoFXImageMotion ResolveMotion(const VideoFXImageMotion& motion, int imageW, int imageH,
                                 int outW, int outH, size_t segmentIndex);

struct KenBurnsView {
    double zoom = 1.0;
    double centerX = 0.5, centerY = 0.5;
};

// The view at `fraction` (0..1) of the segment: zoom interpolated
// geometrically (a constant-looking zoom speed), centres linearly, both
// eased when the motion asks for it
KenBurnsView ViewAt(const VideoFXImageMotion& resolved, double fraction);

// The part of the image a view shows, in image pixels: the largest
// rectangle of the frame's shape inside the image, divided by the zoom,
// centred on the view's centre and kept inside the image
void ViewRect(const KenBurnsView& view, int imageW, int imageH, int outW, int outH,
              double& x, double& y, double& w, double& h);

// A still fitted like video: Letterbox (whole image, black bars), Fill
// (cover, cropped) or Stretch
void StillRect(VideoFXFitMode fit, int imageW, int imageH, int outW, int outH,
               double& x, double& y, double& w, double& h);

// Render the image rectangle (x, y, w, h) - which may reach outside the
// image, shown black there - into an outW x outH RGBA buffer, bilinearly.
// Splits the rows over up to `threads` threads (0 = hardware concurrency).
void RenderView(const VideoFXFrame& image, double x, double y, double w, double h,
                int outW, int outH, uint8_t* dst, int dstStride, int threads = 0);

} // namespace Internal
} // namespace VideoFX
