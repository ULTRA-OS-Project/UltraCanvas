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

} // namespace Internal
} // namespace VideoFX
