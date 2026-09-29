// VideoFX/core/VideoFXFilterBuilder.h
// Internal: turns typed VideoFX effects into backend filter-graph text.
// Pure string code with no FFmpeg dependency, so it is unit-tested directly.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFXTypes.h"

#include <string>
#include <vector>

namespace VideoFX {
namespace Internal {

// Dot-decimal, locale-independent, shortest form ("0.25", "2", "-1.5")
std::string FormatNumber(double value);

// Quote a string for use as a filter option value inside a filter graph
// description: escaped once for the option parser and once for the graph
// parser, so ':', ',', ';', '[', ']', '\'' and '\\' (Windows paths) survive.
std::string EscapeFilterValue(const std::string& value);

// Filters that turn a frame stored with `rotation` (clockwise degrees, from
// the file's display matrix) upright. "" for 0.
std::string AutoRotateChain(int rotation);

// "atempo=..." chain for a speed factor (each atempo stage is limited to
// 0.5..2, so larger factors are split). "" for 1.
std::string AtempoChain(double speed);

// Comma-joined video / audio filter chain for the effects of one segment.
// `segmentDuration` is the segment's length in output seconds (after speed),
// needed to place FadeOut (0 = unknown: FadeOut is then skipped). Effects
// that do not touch the stream contribute nothing. Returns false with `error`
// set for an invalid effect value.
bool BuildVideoEffectChain(const std::vector<VideoFXEffect>& effects, double segmentDuration,
                           std::string& chain, std::string& error);
bool BuildAudioEffectChain(const std::vector<VideoFXEffect>& effects, double segmentDuration,
                           std::string& chain, std::string& error);

// Append `filter` to a comma-joined chain
void AppendFilter(std::string& chain, const std::string& filter);

// ===== transitions =====

// The xfade transition name ("fade", "wipeleft", ...); "" for Cut
std::string TransitionName(VideoFXTransitionType type);

// ===== overlays =====

// Check an overlay's values; `segmentDuration` 0 = unknown
bool ValidateOverlay(const VideoFXOverlay& overlay, double segmentDuration, std::string& error);

// The "enable" timeline expression for [start, end) ("" = always on)
std::string OverlayEnableExpr(const VideoFXOverlay& overlay, double segmentDuration);

// Opacity over time as an expression of t: fades in and out, times `opacity`
std::string OverlayAlphaExpr(const VideoFXOverlay& overlay, double segmentDuration);

// x / y position expressions for an anchor. `frameW`/`frameH` and
// `itemW`/`itemH` are the variable names the filter uses for the frame and
// the overlaid item (drawtext: w, h, text_w, text_h; overlay: W, H, w, h).
void OverlayPosition(const VideoFXOverlay& overlay, int outHeight,
                     const std::string& frameW, const std::string& frameH,
                     const std::string& itemW, const std::string& itemH,
                     std::string& x, std::string& y);

// drawtext filter for a text overlay on a frame of outWidth x outHeight, in
// the overlay's fontPath, else `fontFile` (the export's default), else
// fontconfig's "Sans" when both are empty.
std::string BuildTextOverlayFilter(const VideoFXOverlay& overlay, int outWidth, int outHeight,
                                   double segmentDuration, const std::string& fontFile);

// Image overlay: `inputChain` prepares the image stream (scale, opacity, loop
// to a timed stream at `frameRate`, fades), `overlayFilter` places it on the
// frame. `imageHeight` is the source image's pixel height.
void BuildImageOverlayFilters(const VideoFXOverlay& overlay, int outWidth, int outHeight, int imageHeight,
                              const std::string& frameRate, double segmentDuration,
                              std::string& inputChain, std::string& overlayFilter);

} // namespace Internal
} // namespace VideoFX
