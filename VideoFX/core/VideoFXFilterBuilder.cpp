// VideoFX/core/VideoFXFilterBuilder.cpp
// Effect list -> filter-graph text, and the effect / segment / preset factories.
// Version: 0.2.0
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

VideoFXOverlay VideoFXOverlay::Text(const std::string& text, VideoFXAnchor anchor, double fontSize) {
    VideoFXOverlay o;
    o.kind = VideoFXOverlayKind::Text;
    o.text = text;
    o.anchor = anchor;
    o.fontSize = fontSize;
    return o;
}

VideoFXOverlay VideoFXOverlay::Image(const std::string& path, VideoFXAnchor anchor, double heightFraction) {
    VideoFXOverlay o;
    o.kind = VideoFXOverlayKind::Image;
    o.imagePath = path;
    o.anchor = anchor;
    o.imageHeight = heightFraction;
    return o;
}

VideoFXOverlay VideoFXOverlay::ImageFromFrame(const VideoFXFrame& frame, VideoFXAnchor anchor, double heightFraction) {
    VideoFXOverlay o;
    o.kind = VideoFXOverlayKind::Image;
    o.image = frame;
    o.anchor = anchor;
    o.imageHeight = heightFraction;
    return o;
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

// ============================================================================
// TRANSITIONS
// ============================================================================

std::string TransitionName(VideoFXTransitionType type) {
    // Only names present in FFmpeg 4.4's xfade (the oldest supported)
    switch (type) {
        case VideoFXTransitionType::Crossfade:           return "fade";
        case VideoFXTransitionType::Dissolve:            return "dissolve";
        case VideoFXTransitionType::FadeThroughBlack:    return "fadeblack";
        case VideoFXTransitionType::FadeThroughWhite:    return "fadewhite";
        case VideoFXTransitionType::WipeLeft:            return "wipeleft";
        case VideoFXTransitionType::WipeRight:           return "wiperight";
        case VideoFXTransitionType::WipeUp:              return "wipeup";
        case VideoFXTransitionType::WipeDown:            return "wipedown";
        case VideoFXTransitionType::SlideLeft:           return "slideleft";
        case VideoFXTransitionType::SlideRight:          return "slideright";
        case VideoFXTransitionType::SlideUp:             return "slideup";
        case VideoFXTransitionType::SlideDown:           return "slidedown";
        case VideoFXTransitionType::SmoothLeft:          return "smoothleft";
        case VideoFXTransitionType::SmoothRight:         return "smoothright";
        case VideoFXTransitionType::SmoothUp:            return "smoothup";
        case VideoFXTransitionType::SmoothDown:          return "smoothdown";
        case VideoFXTransitionType::CircleOpen:          return "circleopen";
        case VideoFXTransitionType::CircleClose:         return "circleclose";
        case VideoFXTransitionType::CircleCrop:          return "circlecrop";
        case VideoFXTransitionType::RectCrop:            return "rectcrop";
        case VideoFXTransitionType::Radial:              return "radial";
        case VideoFXTransitionType::Pixelize:            return "pixelize";
        case VideoFXTransitionType::Blur:                return "hblur";
        case VideoFXTransitionType::Distance:            return "distance";
        case VideoFXTransitionType::DiagonalTopLeft:     return "diagtl";
        case VideoFXTransitionType::DiagonalTopRight:    return "diagtr";
        case VideoFXTransitionType::DiagonalBottomLeft:  return "diagbl";
        case VideoFXTransitionType::DiagonalBottomRight: return "diagbr";
        case VideoFXTransitionType::SqueezeHorizontal:   return "squeezeh";
        case VideoFXTransitionType::SqueezeVertical:     return "squeezev";
        case VideoFXTransitionType::Cut:                 return "";
    }
    return "";
}

// ============================================================================
// OVERLAYS
// ============================================================================

namespace {
    // End of the overlay on the segment timeline, 0 = open-ended
    double OverlayEnd(const VideoFXOverlay& o, double segmentDuration) {
        if (o.end > 0.0) return segmentDuration > 0.0 ? std::min(o.end, segmentDuration) : o.end;
        return segmentDuration;
    }

    std::string HexColor(uint32_t rgb) {
        static const char* digits = "0123456789ABCDEF";
        std::string s = "0x";
        for (int shift = 20; shift >= 0; shift -= 4) s += digits[(rgb >> shift) & 0xF];
        return s;
    }
}

bool ValidateOverlay(const VideoFXOverlay& o, double segmentDuration, std::string& error) {
    (void)segmentDuration;
    if (o.kind == VideoFXOverlayKind::Text) {
        if (o.text.empty()) { error = "Text overlay has no text"; return false; }
        if (!InRange(o.fontSize, 0.005, 1.0)) { error = "Text size must be 0.005..1 of the frame height"; return false; }
        if (!InRange(o.boxOpacity, 0.0, 1.0)) { error = "Box opacity must be 0..1"; return false; }
    } else {
        if (!o.image.IsValid() && o.imagePath.empty()) { error = "Image overlay has no image"; return false; }
        if (!InRange(o.imageHeight, 0.0, 1.0)) { error = "Image height must be 0..1 of the frame height"; return false; }
    }
    if (!InRange(o.opacity, 0.0, 1.0)) { error = "Overlay opacity must be 0..1"; return false; }
    if (!InRange(o.margin, 0.0, 0.5)) { error = "Overlay margin must be 0..0.5"; return false; }
    if (o.anchor == VideoFXAnchor::Custom && (!InRange(o.x, -1.0, 1.0) || !InRange(o.y, -1.0, 1.0))) {
        error = "Overlay position must be -1..1";
        return false;
    }
    if (!InRange(o.start, 0.0, 1e6) || !InRange(o.end, 0.0, 1e6) || (o.end > 0.0 && o.end <= o.start)) {
        error = "Overlay times must be start >= 0 and end > start";
        return false;
    }
    if (!InRange(o.fadeIn, 0.0, 3600.0) || !InRange(o.fadeOut, 0.0, 3600.0)) {
        error = "Overlay fades must be >= 0";
        return false;
    }
    return true;
}

std::string OverlayEnableExpr(const VideoFXOverlay& o, double segmentDuration) {
    const double end = OverlayEnd(o, segmentDuration);
    const bool fromStart = o.start <= 0.0;
    if (fromStart && end <= 0.0) return "";
    if (end <= 0.0) return "gte(t," + FormatNumber(o.start) + ")";
    if (fromStart) return "lt(t," + FormatNumber(end) + ")";
    return "between(t," + FormatNumber(o.start) + "," + FormatNumber(end) + ")";
}

std::string OverlayAlphaExpr(const VideoFXOverlay& o, double segmentDuration) {
    const double end = OverlayEnd(o, segmentDuration);
    std::string expr = "1";
    if (o.fadeIn > 0.0) {
        expr = "min(1,max(0,(t-" + FormatNumber(o.start) + ")/" + FormatNumber(o.fadeIn) + "))";
    }
    if (o.fadeOut > 0.0 && end > 0.0) {
        const std::string out = "min(1,max(0,(" + FormatNumber(end) + "-t)/" + FormatNumber(o.fadeOut) + "))";
        expr = expr == "1" ? out : "min(" + expr + "," + out + ")";
    }
    if (o.opacity < 1.0) expr = expr == "1" ? FormatNumber(o.opacity) : FormatNumber(o.opacity) + "*" + expr;
    return expr;
}

void OverlayPosition(const VideoFXOverlay& o, int outHeight,
                     const std::string& W, const std::string& H, const std::string& w, const std::string& h,
                     std::string& x, std::string& y) {
    if (o.anchor == VideoFXAnchor::Custom) {
        x = FormatNumber(o.x) + "*" + W;
        y = FormatNumber(o.y) + "*" + H;
        return;
    }
    const std::string m = std::to_string(static_cast<int>(std::lround(o.margin * outHeight)));
    int col = 1, row = 1;                       // 0 left/top, 1 centre, 2 right/bottom
    switch (o.anchor) {
        case VideoFXAnchor::TopLeft:     col = 0; row = 0; break;
        case VideoFXAnchor::Top:         col = 1; row = 0; break;
        case VideoFXAnchor::TopRight:    col = 2; row = 0; break;
        case VideoFXAnchor::Left:        col = 0; row = 1; break;
        case VideoFXAnchor::Center:      col = 1; row = 1; break;
        case VideoFXAnchor::Right:       col = 2; row = 1; break;
        case VideoFXAnchor::BottomLeft:  col = 0; row = 2; break;
        case VideoFXAnchor::Bottom:      col = 1; row = 2; break;
        case VideoFXAnchor::BottomRight: col = 2; row = 2; break;
        case VideoFXAnchor::Custom:      break;
    }
    x = col == 0 ? m : col == 1 ? "(" + W + "-" + w + ")/2" : W + "-" + w + "-" + m;
    y = row == 0 ? m : row == 1 ? "(" + H + "-" + h + ")/2" : H + "-" + h + "-" + m;
}

std::string BuildTextOverlayFilter(const VideoFXOverlay& o, int outWidth, int outHeight,
                                   double segmentDuration, const std::string& fontFile) {
    (void)outWidth;
    const int size = std::max(4, static_cast<int>(std::lround(o.fontSize * outHeight)));
    std::string x, y;
    OverlayPosition(o, outHeight, "w", "h", "text_w", "text_h", x, y);

    // expansion=none: the text is literal, '%' included
    std::string f = "drawtext=expansion=none:text=" + EscapeFilterValue(o.text);
    // The overlay's own font first, then the export's default
    const std::string& font = o.fontPath.empty() ? fontFile : o.fontPath;
    f += font.empty() ? ":font=Sans" : ":fontfile=" + EscapeFilterValue(font);
    f += ":fontsize=" + std::to_string(size) + ":fontcolor=" + HexColor(o.textColor);
    f += ":x=" + EscapeFilterValue(x) + ":y=" + EscapeFilterValue(y);
    if (o.shadow) {
        const int d = std::max(1, size / 18);
        f += ":shadowcolor=" + EscapeFilterValue("black@0.6") + ":shadowx=" + std::to_string(d) +
             ":shadowy=" + std::to_string(d);
    }
    if (o.box) {
        f += ":box=1:boxcolor=" + EscapeFilterValue(HexColor(o.boxColor) + "@" + FormatNumber(o.boxOpacity)) +
             ":boxborderw=" + std::to_string(std::max(2, size / 3));
    }
    const std::string alpha = OverlayAlphaExpr(o, segmentDuration);
    if (alpha != "1") f += ":alpha=" + EscapeFilterValue(alpha);
    const std::string enable = OverlayEnableExpr(o, segmentDuration);
    if (!enable.empty()) f += ":enable=" + EscapeFilterValue(enable);
    return f;
}

void BuildImageOverlayFilters(const VideoFXOverlay& o, int outWidth, int outHeight, int imageHeight,
                              const std::string& frameRate, double segmentDuration,
                              std::string& inputChain, std::string& overlayFilter) {
    (void)outWidth;
    inputChain = "format=rgba";
    if (o.imageHeight > 0.0) {
        const int h = std::max(2, static_cast<int>(std::lround(o.imageHeight * outHeight)));
        if (h != imageHeight) AppendFilter(inputChain, "scale=-1:" + std::to_string(h));
    }
    if (o.opacity < 1.0) AppendFilter(inputChain, "colorchannelmixer=aa=" + FormatNumber(o.opacity));

    // A still becomes a timed stream so fades can animate it. It never ends
    // by itself; the overlay's shortest=1 ends it with the picture below.
    const double end = OverlayEnd(o, segmentDuration);
    const bool fades = o.fadeIn > 0.0 || (o.fadeOut > 0.0 && end > 0.0);
    if (fades) {
        AppendFilter(inputChain, "loop=loop=-1:size=1,setpts=N/(" + frameRate + ")/TB");
        if (o.fadeIn > 0.0)
            AppendFilter(inputChain, "fade=t=in:st=" + FormatNumber(o.start) + ":d=" + FormatNumber(o.fadeIn) + ":alpha=1");
        if (o.fadeOut > 0.0 && end > 0.0) {
            const double d = std::min(o.fadeOut, std::max(0.0, end - o.start));
            AppendFilter(inputChain, "fade=t=out:st=" + FormatNumber(end - d) + ":d=" + FormatNumber(d) + ":alpha=1");
        }
    }

    std::string x, y;
    OverlayPosition(o, outHeight, "W", "H", "w", "h", x, y);
    // A single still without fades: eof_action=repeat holds it for the whole segment
    overlayFilter = "overlay=x=" + EscapeFilterValue(x) + ":y=" + EscapeFilterValue(y) +
                    (fades ? ":shortest=1" : ":eof_action=repeat");
    const std::string enable = OverlayEnableExpr(o, segmentDuration);
    if (!enable.empty()) overlayFilter += ":enable=" + EscapeFilterValue(enable);
}

} // namespace Internal
} // namespace VideoFX
