// VideoFX/core/VideoFXFilterBuilder.cpp
// Effect list -> filter-graph text, and the effect / segment / preset factories.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXFilterBuilder.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace VideoFX {

// ============================================================================
// FACTORIES
// ============================================================================

namespace {
    VideoFXEffect MakeEffect(VideoFXEffectType type, double amount = 0.0) {
        VideoFXEffect e;
        e.type = type;
        e.amount = amount;
        return e;
    }
}

VideoFXEffect VideoFXEffect::Brightness(double amount)  { return MakeEffect(VideoFXEffectType::Brightness, amount); }
VideoFXEffect VideoFXEffect::Contrast(double amount)    { return MakeEffect(VideoFXEffectType::Contrast, amount); }
VideoFXEffect VideoFXEffect::Saturation(double amount)  { return MakeEffect(VideoFXEffectType::Saturation, amount); }
VideoFXEffect VideoFXEffect::Gamma(double gamma)        { return MakeEffect(VideoFXEffectType::Gamma, gamma); }
VideoFXEffect VideoFXEffect::Exposure(double stops)     { return MakeEffect(VideoFXEffectType::Exposure, stops); }
VideoFXEffect VideoFXEffect::Hue(double degrees)        { return MakeEffect(VideoFXEffectType::Hue, degrees); }
VideoFXEffect VideoFXEffect::Temperature(double amount) { return MakeEffect(VideoFXEffectType::Temperature, amount); }
VideoFXEffect VideoFXEffect::Grayscale()                { return MakeEffect(VideoFXEffectType::Grayscale); }
VideoFXEffect VideoFXEffect::Sepia()                    { return MakeEffect(VideoFXEffectType::Sepia); }
VideoFXEffect VideoFXEffect::Invert()                   { return MakeEffect(VideoFXEffectType::Invert); }
VideoFXEffect VideoFXEffect::Blur(double radius)        { return MakeEffect(VideoFXEffectType::Blur, radius); }
VideoFXEffect VideoFXEffect::Sharpen(double amount)     { return MakeEffect(VideoFXEffectType::Sharpen, amount); }
VideoFXEffect VideoFXEffect::Denoise(double strength)   { return MakeEffect(VideoFXEffectType::Denoise, strength); }
VideoFXEffect VideoFXEffect::Vignette(double amount)    { return MakeEffect(VideoFXEffectType::Vignette, amount); }
VideoFXEffect VideoFXEffect::Rotate90()                 { return MakeEffect(VideoFXEffectType::Rotate90); }
VideoFXEffect VideoFXEffect::Rotate180()                { return MakeEffect(VideoFXEffectType::Rotate180); }
VideoFXEffect VideoFXEffect::Rotate270()                { return MakeEffect(VideoFXEffectType::Rotate270); }
VideoFXEffect VideoFXEffect::Rotate(double degrees)     { return MakeEffect(VideoFXEffectType::Rotate, degrees); }
VideoFXEffect VideoFXEffect::FlipHorizontal()           { return MakeEffect(VideoFXEffectType::FlipHorizontal); }
VideoFXEffect VideoFXEffect::FlipVertical()             { return MakeEffect(VideoFXEffectType::FlipVertical); }
VideoFXEffect VideoFXEffect::FadeIn(double seconds)     { return MakeEffect(VideoFXEffectType::FadeIn, seconds); }
VideoFXEffect VideoFXEffect::FadeOut(double seconds)    { return MakeEffect(VideoFXEffectType::FadeOut, seconds); }
VideoFXEffect VideoFXEffect::Volume(double gain)        { return MakeEffect(VideoFXEffectType::Volume, gain); }
VideoFXEffect VideoFXEffect::NormalizeAudio(double targetLufs) {
    return MakeEffect(VideoFXEffectType::NormalizeAudio, targetLufs);
}

VideoFXEffect VideoFXEffect::LUT(const std::string& lutPath, double strength) {
    VideoFXEffect e = MakeEffect(VideoFXEffectType::LUT, strength);
    e.path = lutPath;
    return e;
}

VideoFXEffect VideoFXEffect::Crop(int x, int y, int width, int height) {
    VideoFXEffect e = MakeEffect(VideoFXEffectType::Crop);
    e.x = x;
    e.y = y;
    e.width = width;
    e.height = height;
    return e;
}

VideoFXSegment VideoFXSegment::FromFile(const std::string& path, double start, double end) {
    VideoFXSegment s;
    s.kind = VideoFXSourceKind::File;
    s.path = path;
    s.start = start;
    s.end = end;
    return s;
}

VideoFXSegment VideoFXSegment::SolidColor(uint32_t rgb, double seconds) {
    VideoFXSegment s;
    s.kind = VideoFXSourceKind::Color;
    s.color = rgb & 0xFFFFFFu;
    s.duration = seconds;
    return s;
}

VideoFXSegment VideoFXSegment::TestPattern(double seconds) {
    VideoFXSegment s;
    s.kind = VideoFXSourceKind::TestPattern;
    s.duration = seconds;
    return s;
}

VideoFXExportSettings VideoFXExportSettings::WebMP4(int height) {
    VideoFXExportSettings s;
    s.container = VideoFXContainer::MP4;
    s.videoCodec = VideoFXVideoCodec::H264;
    s.audioCodec = VideoFXAudioCodec::AAC;
    s.height = height;
    return s;
}

VideoFXExportSettings VideoFXExportSettings::WebM(int height) {
    VideoFXExportSettings s;
    s.container = VideoFXContainer::WebM;
    s.videoCodec = VideoFXVideoCodec::VP9;
    s.audioCodec = VideoFXAudioCodec::Opus;
    s.height = height;
    return s;
}

VideoFXExportSettings VideoFXExportSettings::AnimatedGif(int width, double fps) {
    VideoFXExportSettings s;
    s.container = VideoFXContainer::GIF;
    s.videoCodec = VideoFXVideoCodec::GIF;
    s.audioCodec = VideoFXAudioCodec::Disabled;
    s.width = width;
    s.frameRate = fps;
    return s;
}

VideoFXExportSettings VideoFXExportSettings::MasterProRes() {
    VideoFXExportSettings s;
    s.container = VideoFXContainer::MOV;
    s.videoCodec = VideoFXVideoCodec::ProRes;
    s.audioCodec = VideoFXAudioCodec::PCM16;
    return s;
}

VideoFXExportSettings VideoFXExportSettings::AudioOnlyMP3(int64_t bitRate) {
    VideoFXExportSettings s;
    s.container = VideoFXContainer::MP3;
    s.videoCodec = VideoFXVideoCodec::Disabled;
    s.audioCodec = VideoFXAudioCodec::MP3;
    s.audioBitRate = bitRate;
    return s;
}

VideoFXExportSettings VideoFXExportSettings::AudioOnlyWAV() {
    VideoFXExportSettings s;
    s.container = VideoFXContainer::WAV;
    s.videoCodec = VideoFXVideoCodec::Disabled;
    s.audioCodec = VideoFXAudioCodec::PCM16;
    return s;
}

// ============================================================================
// FILTER TEXT
// ============================================================================

namespace Internal {

std::string FormatNumber(double value) {
    if (!std::isfinite(value)) value = 0.0;
    if (std::fabs(value) < 1e-9) value = 0.0;          // no "-0"
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6) << value;
    std::string s = out.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

std::string EscapeFilterValue(const std::string& value) {
    // Level 1 - the option parser (av_get_token): backslash-escape its
    // specials and wrap nothing, so ':' inside a path is not a separator.
    std::string level1;
    for (char c : value) {
        if (c == '\\' || c == '\'' || c == ':') level1 += '\\';
        level1 += c;
    }
    // Level 2 - the graph parser: the same once more for its own specials.
    std::string level2;
    for (char c : level1) {
        if (c == '\\' || c == '\'' || c == '[' || c == ']' || c == ',' || c == ';') level2 += '\\';
        level2 += c;
    }
    return level2;
}

void AppendFilter(std::string& chain, const std::string& filter) {
    if (filter.empty()) return;
    if (!chain.empty()) chain += ',';
    chain += filter;
}

std::string AutoRotateChain(int rotation) {
    int r = ((rotation % 360) + 360) % 360;
    // Snap to the nearest quarter turn; display matrices carry -90.000001 etc.
    r = static_cast<int>(std::lround(r / 90.0) * 90) % 360;
    switch (r) {
        case 90:  return "transpose=clock";
        case 180: return "hflip,vflip";
        case 270: return "transpose=cclock";
        default:  return "";
    }
}

std::string AtempoChain(double speed) {
    if (!(speed > 0.0) || std::fabs(speed - 1.0) < 1e-6) return "";
    std::string chain;
    double remaining = speed;
    while (remaining > 2.0 + 1e-9) {
        AppendFilter(chain, "atempo=2");
        remaining /= 2.0;
    }
    while (remaining < 0.5 - 1e-9) {
        AppendFilter(chain, "atempo=0.5");
        remaining /= 0.5;
    }
    if (std::fabs(remaining - 1.0) > 1e-6) AppendFilter(chain, "atempo=" + FormatNumber(remaining));
    return chain;
}

namespace {
    bool InRange(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
}

bool BuildVideoEffectChain(const std::vector<VideoFXEffect>& effects, double segmentDuration,
                           std::string& chain, std::string& error) {
    chain.clear();
    int labelCounter = 0;
    for (const VideoFXEffect& e : effects) {
        const double a = e.amount;
        switch (e.type) {
            case VideoFXEffectType::Brightness:
                if (!InRange(a, -1.0, 1.0)) { error = "Brightness must be -1..1"; return false; }
                AppendFilter(chain, "eq=brightness=" + FormatNumber(a));
                break;
            case VideoFXEffectType::Contrast:
                if (!InRange(a, -1.0, 1.0)) { error = "Contrast must be -1..1"; return false; }
                AppendFilter(chain, "eq=contrast=" + FormatNumber(1.0 + a));
                break;
            case VideoFXEffectType::Saturation:
                if (!InRange(a, -1.0, 1.0)) { error = "Saturation must be -1..1"; return false; }
                // eq saturation is 0..3 with 1 neutral; map -1..1 onto 0..2.5
                AppendFilter(chain, "eq=saturation=" + FormatNumber(a < 0 ? 1.0 + a : 1.0 + a * 1.5));
                break;
            case VideoFXEffectType::Gamma:
                if (!InRange(a, 0.1, 10.0)) { error = "Gamma must be 0.1..10"; return false; }
                AppendFilter(chain, "eq=gamma=" + FormatNumber(a));
                break;
            case VideoFXEffectType::Exposure:
                if (!InRange(a, -3.0, 3.0)) { error = "Exposure must be -3..3 stops"; return false; }
                AppendFilter(chain, "exposure=exposure=" + FormatNumber(a));
                break;
            case VideoFXEffectType::Hue:
                if (!std::isfinite(a)) { error = "Hue angle is not a number"; return false; }
                AppendFilter(chain, "hue=h=" + FormatNumber(std::fmod(a, 360.0)));
                break;
            case VideoFXEffectType::Temperature: {
                if (!InRange(a, -1.0, 1.0)) { error = "Temperature must be -1..1"; return false; }
                const double m = a * 0.3, h = a * 0.15;
                AppendFilter(chain, "colorbalance=rm=" + FormatNumber(m) + ":bm=" + FormatNumber(-m) +
                                    ":rh=" + FormatNumber(h) + ":bh=" + FormatNumber(-h));
                break;
            }
            case VideoFXEffectType::Grayscale:
                AppendFilter(chain, "hue=s=0");
                break;
            case VideoFXEffectType::Sepia:
                AppendFilter(chain, "colorchannelmixer=rr=0.393:rg=0.769:rb=0.189:"
                                    "gr=0.349:gg=0.686:gb=0.168:br=0.272:bg=0.534:bb=0.131");
                break;
            case VideoFXEffectType::Invert:
                AppendFilter(chain, "negate");
                break;
            case VideoFXEffectType::LUT: {
                if (e.path.empty()) { error = "LUT effect needs a file"; return false; }
                if (!InRange(a, 0.0, 1.0)) { error = "LUT strength must be 0..1"; return false; }
                if (a <= 0.0) break;
                const std::string lut = "lut3d=file=" + EscapeFilterValue(e.path);
                if (a >= 1.0) {
                    AppendFilter(chain, lut);
                } else {
                    // Mix graded and original: split, grade one branch, blend.
                    const std::string n = std::to_string(labelCounter++);
                    AppendFilter(chain, "split[vfxsrc" + n + "][vfxlut" + n + "];[vfxlut" + n + "]" + lut +
                                        "[vfxgraded" + n + "];[vfxsrc" + n + "][vfxgraded" + n +
                                        "]blend=all_mode=normal:all_opacity=" + FormatNumber(a));
                }
                break;
            }
            case VideoFXEffectType::Blur:
                if (!InRange(a, 0.0, 100.0)) { error = "Blur radius must be 0..100"; return false; }
                if (a > 0.0) AppendFilter(chain, "gblur=sigma=" + FormatNumber(a));
                break;
            case VideoFXEffectType::Sharpen:
                if (!InRange(a, 0.0, 3.0)) { error = "Sharpen must be 0..3"; return false; }
                if (a > 0.0) AppendFilter(chain, "unsharp=5:5:" + FormatNumber(a) + ":5:5:0");
                break;
            case VideoFXEffectType::Denoise:
                if (!InRange(a, 0.0, 20.0)) { error = "Denoise must be 0..20"; return false; }
                if (a > 0.0) AppendFilter(chain, "hqdn3d=luma_spatial=" + FormatNumber(a));
                break;
            case VideoFXEffectType::Vignette:
                if (!InRange(a, 0.0, 1.0)) { error = "Vignette must be 0..1"; return false; }
                if (a > 0.0) AppendFilter(chain, "vignette=angle=" + FormatNumber(a * 1.5707963));
                break;
            case VideoFXEffectType::Rotate90:
                AppendFilter(chain, "transpose=clock");
                break;
            case VideoFXEffectType::Rotate180:
                AppendFilter(chain, "hflip,vflip");
                break;
            case VideoFXEffectType::Rotate270:
                AppendFilter(chain, "transpose=cclock");
                break;
            case VideoFXEffectType::Rotate:
                if (!std::isfinite(a)) { error = "Rotation angle is not a number"; return false; }
                if (std::fabs(std::fmod(a, 360.0)) > 1e-6)
                    AppendFilter(chain, "rotate=" + FormatNumber(a * 3.14159265358979 / 180.0) + ":fillcolor=black");
                break;
            case VideoFXEffectType::FlipHorizontal:
                AppendFilter(chain, "hflip");
                break;
            case VideoFXEffectType::FlipVertical:
                AppendFilter(chain, "vflip");
                break;
            case VideoFXEffectType::Crop:
                if (e.width <= 0 || e.height <= 0 || e.x < 0 || e.y < 0) {
                    error = "Crop needs a non-empty rectangle at x, y >= 0";
                    return false;
                }
                AppendFilter(chain, "crop=" + std::to_string(e.width) + ":" + std::to_string(e.height) + ":" +
                                    std::to_string(e.x) + ":" + std::to_string(e.y));
                break;
            case VideoFXEffectType::FadeIn:
                if (!InRange(a, 0.0, 3600.0)) { error = "Fade length must be >= 0"; return false; }
                if (a > 0.0) AppendFilter(chain, "fade=t=in:st=0:d=" + FormatNumber(a));
                break;
            case VideoFXEffectType::FadeOut: {
                if (!InRange(a, 0.0, 3600.0)) { error = "Fade length must be >= 0"; return false; }
                if (a <= 0.0 || segmentDuration <= 0.0) break;   // no end to fade towards
                const double d = std::min(a, segmentDuration);
                const double st = std::max(0.0, segmentDuration - d);
                AppendFilter(chain, "fade=t=out:st=" + FormatNumber(st) + ":d=" + FormatNumber(d));
                break;
            }
            case VideoFXEffectType::Volume:
            case VideoFXEffectType::NormalizeAudio:
                break;                                   // audio only
        }
    }
    return true;
}

bool BuildAudioEffectChain(const std::vector<VideoFXEffect>& effects, double segmentDuration,
                           std::string& chain, std::string& error) {
    chain.clear();
    for (const VideoFXEffect& e : effects) {
        const double a = e.amount;
        switch (e.type) {
            case VideoFXEffectType::FadeIn:
                if (!InRange(a, 0.0, 3600.0)) { error = "Fade length must be >= 0"; return false; }
                if (a > 0.0) AppendFilter(chain, "afade=t=in:st=0:d=" + FormatNumber(a));
                break;
            case VideoFXEffectType::FadeOut: {
                if (!InRange(a, 0.0, 3600.0)) { error = "Fade length must be >= 0"; return false; }
                if (a <= 0.0 || segmentDuration <= 0.0) break;   // no end to fade towards
                const double d = std::min(a, segmentDuration);
                const double st = std::max(0.0, segmentDuration - d);
                AppendFilter(chain, "afade=t=out:st=" + FormatNumber(st) + ":d=" + FormatNumber(d));
                break;
            }
            case VideoFXEffectType::Volume:
                if (!InRange(a, 0.0, 20.0)) { error = "Volume gain must be 0..20"; return false; }
                AppendFilter(chain, "volume=" + FormatNumber(a));
                break;
            case VideoFXEffectType::NormalizeAudio:
                if (!InRange(a, -70.0, -5.0)) { error = "Loudness target must be -70..-5 LUFS"; return false; }
                AppendFilter(chain, "loudnorm=I=" + FormatNumber(a) + ":TP=-1.5:LRA=11");
                break;
            default:
                break;                                   // video only
        }
    }
    return true;
}

} // namespace Internal
} // namespace VideoFX
