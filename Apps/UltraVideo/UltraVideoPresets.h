// Apps/UltraVideo/UltraVideoPresets.h
// The effect presets the bin's Effects tab offers (Docs/Research/UltraVideoDesignProposal.md
// §6.2): a name, a hint and the engine effects it stands for - "Warm film"
// is temperature, saturation and a vignette. A preset goes onto a clip as
// plain VideoFXEffects, which the inspector lists one by one and takes off
// again; nothing in the document remembers the preset.
// Version: 0.2.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFX.h"

#include <string>
#include <vector>

namespace UltraVideo {

struct EffectPreset {
    const char* key;
    const char* label;
    const char* hint;
    std::vector<VideoFX::VideoFXEffect> effects;
};

const std::vector<EffectPreset>& EffectPresets();
const EffectPreset* FindEffectPreset(const std::string& key);   // nullptr when unknown

// What an effect reads as in a list: "Warmth +30 %", "Vignette 35 %", "Fade in 0.5 s"
std::string EffectName(const VideoFX::VideoFXEffect& effect);

// Put a preset on a segment: its effects replace any of the same types
// already there (so a preset applied twice is there once, and "Cool" after
// "Warm film" turns the warmth round rather than stacking it); the
// segment's other effects stay, in their order, before the preset's.
void ApplyEffectPreset(VideoFX::VideoFXSegment& segment, const EffectPreset& preset);

} // namespace UltraVideo
