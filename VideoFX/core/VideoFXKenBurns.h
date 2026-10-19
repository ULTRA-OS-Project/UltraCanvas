// VideoFX/core/VideoFXKenBurns.h
// Internal: the camera over a still image - motion presets, the view at a
// point in time, and the frame renderer. Pure C++, no FFmpeg, unit-tested.
// Version: 0.5.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFXTypes.h"

#include <cstdint>
#include <string>
#include <vector>

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

// ---- keeping regions (faces) in shot ----

// Check keep-in-view regions: inside the image (0..1), not empty
bool ValidateKeepInView(const std::vector<VideoFXRect>& regions, std::string& error);

// The box around every region plus headroom (15 % of its size each side,
// faces need room), as fractions, clipped to the image; empty for none
VideoFXRect FocusBounds(const std::vector<VideoFXRect>& regions);

// `resolved` (a Custom or Still motion) fitted so the view can hold `focus`
// at both ends: each zoom capped where the view would grow narrower than the
// box, each centre moved just far enough that the view holds it. `fit` is the
// resolved Cover / Contain / BlurredBackground.
VideoFXImageMotion FitMotionToFocus(const VideoFXImageMotion& resolved, const VideoFXRect& focus,
                                    int imageW, int imageH, int outW, int outH, VideoFXImageFit fit);

// A view rectangle (image pixels) slid, never resized, to hold `focus`
// wherever it is big enough - centred on it where it is not - and kept
// inside the image along each side where it is smaller than the image.
// Applied to every frame, so the move between the ends holds it too.
void KeepFocusInView(const VideoFXRect& focus, int imageW, int imageH, double& x, double& y, double w, double h);

// Auto turned into Cover or BlurredBackground for this image and frame
VideoFXImageFit ResolveImageFit(VideoFXImageFit fit, int imageW, int imageH, int outW, int outH);

// Contain / BlurredBackground: at zoom 1 a frame-shaped window just big
// enough to hold the whole image; zooming narrows it. Along a side where the
// window is still wider than the image it stays centred, elsewhere it
// follows the view's centre and is kept inside the image.
void ContainViewRect(const KenBurnsView& view, int imageW, int imageH, int outW, int outH,
                     double& x, double& y, double& w, double& h);

// The blurred, darkened backdrop for BlurredBackground: the image covering
// an outW x outH frame, heavily blurred. Made once per image.
void MakeBlurredBackdrop(const VideoFXFrame& image, int outW, int outH, std::vector<uint8_t>& rgba);

// Render the image rectangle (x, y, w, h) into an outW x outH RGBA buffer,
// bilinearly. Where the rectangle reaches outside the image, and under
// transparent pixels, `background` shows (outW x outH RGBA, tightly packed),
// or black when it is null. Rows are split over up to `threads` threads
// (0 = hardware concurrency).
void RenderView(const VideoFXFrame& image, double x, double y, double w, double h,
                int outW, int outH, uint8_t* dst, int dstStride, int threads = 0,
                const uint8_t* background = nullptr);

} // namespace Internal
} // namespace VideoFX
