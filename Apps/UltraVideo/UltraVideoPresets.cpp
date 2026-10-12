// Apps/UltraVideo/UltraVideoPresets.cpp
// Version: 0.2.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraVideoPresets.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

using namespace VideoFX;

namespace UltraVideo {

namespace {
std::string Number(double v, int decimals) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::fixed << std::setprecision(decimals) << v;
    return o.str();
}
std::string Percent(double v, bool signed_) {   // 0.3 -> "+30 %"
    const long p = std::lround(v * 100.0);
    return (signed_ && p > 0 ? "+" : "") + std::to_string(p) + " %";
}
}

const std::vector<EffectPreset>& EffectPresets() {
    static const std::vector<EffectPreset> presets = {
        {"warm", "Warm film", "Orange warmth, a touch more colour, darker corners.",
         {VideoFXEffect::Temperature(0.3), VideoFXEffect::Saturation(0.1), VideoFXEffect::Vignette(0.35)}},
        {"cool", "Cool morning", "A blue cast and a little more contrast.",
         {VideoFXEffect::Temperature(-0.3), VideoFXEffect::Contrast(0.1)}},
        {"mono", "Black and white", "No colour, a little more contrast.",
         {VideoFXEffect::Grayscale(), VideoFXEffect::Contrast(0.15)}},
        {"old", "Old photo", "Sepia, soft contrast, dark corners.",
         {VideoFXEffect::Sepia(), VideoFXEffect::Contrast(-0.1), VideoFXEffect::Vignette(0.45)}},
        {"vivid", "Vivid", "Stronger colour and contrast.",
         {VideoFXEffect::Saturation(0.35), VideoFXEffect::Contrast(0.15)}},
        {"faded", "Faded", "Lifted blacks, less colour: a washed look.",
         {VideoFXEffect::Contrast(-0.25), VideoFXEffect::Brightness(0.1), VideoFXEffect::Saturation(-0.2)}},
        {"soft", "Soft focus", "A gentle blur and a touch of light.",
         {VideoFXEffect::Blur(2.0), VideoFXEffect::Brightness(0.05)}},
        {"crisp", "Crisp", "Sharper detail.",
         {VideoFXEffect::Sharpen(0.8)}},
        {"fades", "Fade in and out", "Half a second from black at the start, to black at the end.",
         {VideoFXEffect::FadeIn(0.5), VideoFXEffect::FadeOut(0.5)}},
        {"quiet", "Quieter sound", "The clip's own sound at half volume, under a voice or the music.",
         {VideoFXEffect::Volume(0.5)}},
    };
    return presets;
}

const EffectPreset* FindEffectPreset(const std::string& key) {
    for (const EffectPreset& p : EffectPresets()) if (key == p.key) return &p;
    return nullptr;
}

std::string EffectName(const VideoFXEffect& e) {
    switch (e.type) {
        case VideoFXEffectType::Brightness: return "Brightness " + Percent(e.amount, true);
        case VideoFXEffectType::Contrast: return "Contrast " + Percent(e.amount, true);
        case VideoFXEffectType::Saturation: return "Saturation " + Percent(e.amount, true);
        case VideoFXEffectType::Gamma: return "Gamma " + Number(e.amount, 2);
        case VideoFXEffectType::Exposure: return std::string("Exposure ") + (e.amount > 0 ? "+" : "") + Number(e.amount, 1) + " stops";
        case VideoFXEffectType::Hue: return "Hue " + Number(e.amount, 0) + "\xC2\xB0";
        case VideoFXEffectType::Temperature: return (e.amount < 0 ? "Coolness " : "Warmth ") + Percent(std::fabs(e.amount), true);
        case VideoFXEffectType::Grayscale: return "Black and white";
        case VideoFXEffectType::Sepia: return "Sepia";
        case VideoFXEffectType::Invert: return "Inverted";
        case VideoFXEffectType::LUT: return "Look-up table " + Percent(e.amount, false);
        case VideoFXEffectType::Blur: return "Blur " + Number(e.amount, 1) + " px";
        case VideoFXEffectType::Sharpen: return "Sharpen " + Number(e.amount, 1);
        case VideoFXEffectType::Denoise: return "Denoise " + Number(e.amount, 1);
        case VideoFXEffectType::Vignette: return "Vignette " + Percent(e.amount, false);
        case VideoFXEffectType::Rotate90: return "Rotated 90\xC2\xB0";
        case VideoFXEffectType::Rotate180: return "Rotated 180\xC2\xB0";
        case VideoFXEffectType::Rotate270: return "Rotated 270\xC2\xB0";
        case VideoFXEffectType::Rotate: return "Rotated " + Number(e.amount, 0) + "\xC2\xB0";
        case VideoFXEffectType::FlipHorizontal: return "Mirrored";
        case VideoFXEffectType::FlipVertical: return "Flipped";
        case VideoFXEffectType::Crop: return "Crop " + std::to_string(e.width) + " \xC3\x97 " + std::to_string(e.height);
        case VideoFXEffectType::FadeIn: return "Fade in " + Number(e.amount, 1) + " s";
        case VideoFXEffectType::FadeOut: return "Fade out " + Number(e.amount, 1) + " s";
        case VideoFXEffectType::Volume: return "Volume " + Percent(e.amount, false);
        case VideoFXEffectType::NormalizeAudio: return "Loudness " + Number(e.amount, 0) + " LUFS";
    }
    return "Effect";
}

void ApplyEffectPreset(VideoFXSegment& segment, const EffectPreset& preset) {
    std::vector<VideoFXEffect>& effects = segment.effects;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [&](const VideoFXEffect& e) {
                      return std::any_of(preset.effects.begin(), preset.effects.end(),
                                         [&](const VideoFXEffect& p) { return p.type == e.type; }); }),
                  effects.end());
    effects.insert(effects.end(), preset.effects.begin(), preset.effects.end());
}

} // namespace UltraVideo
