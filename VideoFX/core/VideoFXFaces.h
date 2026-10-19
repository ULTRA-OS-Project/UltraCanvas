// VideoFX/core/VideoFXFaces.h
// Internal: the built-in face detector - a Viola-Jones cascade (OpenCV's
// trained frontal-face model, compiled in as VideoFXFaceCascade.inc) run by
// VideoFX's own code. Pure C++, no FFmpeg, no OpenCV, unit-tested.
//
//   gray ─ pyramid (x1.1 a step) ─ integral images ─ 20x20 window, every
//   2nd pixel ─ 22 stages, most windows out after the first few features ─
//   hits grouped: a face is a spot several windows agree on
// Version: 0.6.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFXTypes.h"

#include <cstdint>
#include <vector>

namespace VideoFX {
namespace Internal {

struct FaceBox {
    int x = 0, y = 0, w = 0, h = 0;     // pixels
    int neighbours = 0;                 // windows that agreed on it: how sure
};

// Faces in an 8-bit grey image (rows of `stride` bytes). A face is reported
// where more than `minNeighbours` windows of nearby position and size found
// one - OpenCV's detectMultiScale(scaleFactor, minNeighbors, minSize).
std::vector<FaceBox> DetectFacesGray(const uint8_t* gray, int width, int height, int stride,
                                     double scaleFactor = 1.1, int minNeighbours = 3, int minSize = 20);

// Faces in an RGBA image as fractions of it, largest first. The image is
// first shrunk to at most `maxSide` pixels a side (faces smaller than about
// 1/40 of that side are not found). The cascade's stray hits are thinned
// out: a box too few windows agree on for its size goes, and in a colour
// photo so does one without skin in its middle.
std::vector<VideoFXRect> DetectFaces(const VideoFXFrame& image, int maxSide = 800);

} // namespace Internal
} // namespace VideoFX
