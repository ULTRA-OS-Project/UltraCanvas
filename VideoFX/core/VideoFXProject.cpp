// VideoFX/core/VideoFXProject.cpp
// VideoFX project files: a VideoFXProject as JSON. Every field is written,
// even at its default, so a project keeps meaning what it meant when a later
// VideoFX changes a default; enumerations are written by name, so reordering
// one cannot change an old file. Unknown keys are ignored and missing ones
// take their defaults, so older and newer files of the same format version
// read cleanly.
//
//   { "format": "VideoFX project", "version": 1, "kind": "timeline",
//     "title": "...", "output": "out.mp4", "settings": {...},
//     "segments": [ {...}, ... ] }                       a timeline, or
//     "slideshow": { "images": [...], "options": {...} } a slideshow
// Version: 0.7.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "VideoFXProject.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"      // PathFromUtf8, PathToUtf8
#include "../../UltraCanvas/third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

namespace VideoFX {
namespace Internal {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;            // keys in the order written: a file reads top to bottom

constexpr const char* kFormatName = "VideoFX project";

// ---------------------------------------------------------------------------
// enumerations by name
// ---------------------------------------------------------------------------

template <class E>
struct Names {
    const std::pair<E, const char*>* table;
    size_t count;
    const char* what;
};

template <class E, size_t N>
constexpr Names<E> MakeNames(const std::pair<E, const char*> (&t)[N], const char* what) {
    return Names<E>{t, N, what};
}

constexpr std::pair<VideoFXSourceKind, const char*> kSourceKinds[] = {
    {VideoFXSourceKind::File, "file"}, {VideoFXSourceKind::Color, "color"},
    {VideoFXSourceKind::TestPattern, "testPattern"}, {VideoFXSourceKind::Image, "image"}};

constexpr std::pair<VideoFXEffectType, const char*> kEffects[] = {
    {VideoFXEffectType::Brightness, "brightness"}, {VideoFXEffectType::Contrast, "contrast"},
    {VideoFXEffectType::Saturation, "saturation"}, {VideoFXEffectType::Gamma, "gamma"},
    {VideoFXEffectType::Exposure, "exposure"}, {VideoFXEffectType::Hue, "hue"},
    {VideoFXEffectType::Temperature, "temperature"}, {VideoFXEffectType::Grayscale, "grayscale"},
    {VideoFXEffectType::Sepia, "sepia"}, {VideoFXEffectType::Invert, "invert"}, {VideoFXEffectType::LUT, "lut"},
    {VideoFXEffectType::Blur, "blur"}, {VideoFXEffectType::Sharpen, "sharpen"},
    {VideoFXEffectType::Denoise, "denoise"}, {VideoFXEffectType::Vignette, "vignette"},
    {VideoFXEffectType::Rotate90, "rotate90"}, {VideoFXEffectType::Rotate180, "rotate180"},
    {VideoFXEffectType::Rotate270, "rotate270"}, {VideoFXEffectType::Rotate, "rotate"},
    {VideoFXEffectType::FlipHorizontal, "flipHorizontal"}, {VideoFXEffectType::FlipVertical, "flipVertical"},
    {VideoFXEffectType::Crop, "crop"}, {VideoFXEffectType::FadeIn, "fadeIn"}, {VideoFXEffectType::FadeOut, "fadeOut"},
    {VideoFXEffectType::Volume, "volume"}, {VideoFXEffectType::NormalizeAudio, "normalizeAudio"}};

constexpr std::pair<VideoFXTransitionType, const char*> kTransitions[] = {
    {VideoFXTransitionType::Cut, "cut"}, {VideoFXTransitionType::Crossfade, "crossfade"},
    {VideoFXTransitionType::Dissolve, "dissolve"}, {VideoFXTransitionType::FadeThroughBlack, "fadeThroughBlack"},
    {VideoFXTransitionType::FadeThroughWhite, "fadeThroughWhite"}, {VideoFXTransitionType::WipeLeft, "wipeLeft"},
    {VideoFXTransitionType::WipeRight, "wipeRight"}, {VideoFXTransitionType::WipeUp, "wipeUp"},
    {VideoFXTransitionType::WipeDown, "wipeDown"}, {VideoFXTransitionType::SlideLeft, "slideLeft"},
    {VideoFXTransitionType::SlideRight, "slideRight"}, {VideoFXTransitionType::SlideUp, "slideUp"},
    {VideoFXTransitionType::SlideDown, "slideDown"}, {VideoFXTransitionType::SmoothLeft, "smoothLeft"},
    {VideoFXTransitionType::SmoothRight, "smoothRight"}, {VideoFXTransitionType::SmoothUp, "smoothUp"},
    {VideoFXTransitionType::SmoothDown, "smoothDown"}, {VideoFXTransitionType::CircleOpen, "circleOpen"},
    {VideoFXTransitionType::CircleClose, "circleClose"}, {VideoFXTransitionType::CircleCrop, "circleCrop"},
    {VideoFXTransitionType::RectCrop, "rectCrop"}, {VideoFXTransitionType::Radial, "radial"},
    {VideoFXTransitionType::Pixelize, "pixelize"}, {VideoFXTransitionType::Blur, "blur"},
    {VideoFXTransitionType::Distance, "distance"}, {VideoFXTransitionType::DiagonalTopLeft, "diagonalTopLeft"},
    {VideoFXTransitionType::DiagonalTopRight, "diagonalTopRight"},
    {VideoFXTransitionType::DiagonalBottomLeft, "diagonalBottomLeft"},
    {VideoFXTransitionType::DiagonalBottomRight, "diagonalBottomRight"},
    {VideoFXTransitionType::SqueezeHorizontal, "squeezeHorizontal"},
    {VideoFXTransitionType::SqueezeVertical, "squeezeVertical"}};

constexpr std::pair<VideoFXOverlayKind, const char*> kOverlayKinds[] = {
    {VideoFXOverlayKind::Text, "text"}, {VideoFXOverlayKind::Image, "image"}};

constexpr std::pair<VideoFXAnchor, const char*> kAnchors[] = {
    {VideoFXAnchor::TopLeft, "topLeft"}, {VideoFXAnchor::Top, "top"}, {VideoFXAnchor::TopRight, "topRight"},
    {VideoFXAnchor::Left, "left"}, {VideoFXAnchor::Center, "center"}, {VideoFXAnchor::Right, "right"},
    {VideoFXAnchor::BottomLeft, "bottomLeft"}, {VideoFXAnchor::Bottom, "bottom"},
    {VideoFXAnchor::BottomRight, "bottomRight"}, {VideoFXAnchor::Custom, "custom"}};

constexpr std::pair<VideoFXMotionStyle, const char*> kMotions[] = {
    {VideoFXMotionStyle::Still, "still"}, {VideoFXMotionStyle::ZoomIn, "zoomIn"},
    {VideoFXMotionStyle::ZoomOut, "zoomOut"}, {VideoFXMotionStyle::PanLeft, "panLeft"},
    {VideoFXMotionStyle::PanRight, "panRight"}, {VideoFXMotionStyle::PanUp, "panUp"},
    {VideoFXMotionStyle::PanDown, "panDown"}, {VideoFXMotionStyle::Auto, "auto"},
    {VideoFXMotionStyle::Custom, "custom"}};

constexpr std::pair<VideoFXImageFit, const char*> kImageFits[] = {
    {VideoFXImageFit::Auto, "auto"}, {VideoFXImageFit::Cover, "cover"}, {VideoFXImageFit::Contain, "contain"},
    {VideoFXImageFit::BlurredBackground, "blurredBackground"}};

constexpr std::pair<VideoFXContainer, const char*> kContainers[] = {
    {VideoFXContainer::Auto, "auto"}, {VideoFXContainer::MP4, "mp4"}, {VideoFXContainer::MOV, "mov"},
    {VideoFXContainer::MKV, "mkv"}, {VideoFXContainer::WebM, "webm"}, {VideoFXContainer::AVI, "avi"},
    {VideoFXContainer::GIF, "gif"}, {VideoFXContainer::MP3, "mp3"}, {VideoFXContainer::M4A, "m4a"},
    {VideoFXContainer::WAV, "wav"}, {VideoFXContainer::FLAC, "flac"}, {VideoFXContainer::OGG, "ogg"}};

constexpr std::pair<VideoFXVideoCodec, const char*> kVideoCodecs[] = {
    {VideoFXVideoCodec::Auto, "auto"}, {VideoFXVideoCodec::H264, "h264"}, {VideoFXVideoCodec::H265, "h265"},
    {VideoFXVideoCodec::VP8, "vp8"}, {VideoFXVideoCodec::VP9, "vp9"}, {VideoFXVideoCodec::AV1, "av1"},
    {VideoFXVideoCodec::MPEG4, "mpeg4"}, {VideoFXVideoCodec::MJPEG, "mjpeg"}, {VideoFXVideoCodec::ProRes, "prores"},
    {VideoFXVideoCodec::FFV1, "ffv1"}, {VideoFXVideoCodec::GIF, "gif"}, {VideoFXVideoCodec::Disabled, "none"}};

constexpr std::pair<VideoFXAudioCodec, const char*> kAudioCodecs[] = {
    {VideoFXAudioCodec::Auto, "auto"}, {VideoFXAudioCodec::AAC, "aac"}, {VideoFXAudioCodec::MP3, "mp3"},
    {VideoFXAudioCodec::Opus, "opus"}, {VideoFXAudioCodec::Vorbis, "vorbis"}, {VideoFXAudioCodec::FLAC, "flac"},
    {VideoFXAudioCodec::PCM16, "pcm16"}, {VideoFXAudioCodec::Disabled, "none"}};

constexpr std::pair<VideoFXFitMode, const char*> kFitModes[] = {
    {VideoFXFitMode::Letterbox, "letterbox"}, {VideoFXFitMode::Fill, "fill"}, {VideoFXFitMode::Stretch, "stretch"}};

constexpr std::pair<VideoFXProjectKind, const char*> kProjectKinds[] = {
    {VideoFXProjectKind::Timeline, "timeline"}, {VideoFXProjectKind::Slideshow, "slideshow"}};

// A value that cannot be read: the message names what and where
struct ReadError {
    std::string message;
};

template <class E>
const char* NameOf(const Names<E>& names, E value) {
    for (size_t i = 0; i < names.count; ++i)
        if (names.table[i].first == value) return names.table[i].second;
    return names.table[0].second;                   // not reached: every value is in its table
}

template <class E>
E ValueOf(const Names<E>& names, const std::string& name, const std::string& where) {
    for (size_t i = 0; i < names.count; ++i)
        if (name == names.table[i].second) return names.table[i].first;
    throw ReadError{where + ": unknown " + names.what + " \"" + name + "\""};
}

// ---------------------------------------------------------------------------
// paths: media inside the project's folder relative to it (the folder can
// move as a whole), everything else absolute (the project file alone can
// move); '/' between parts
// ---------------------------------------------------------------------------

struct Paths {
    fs::path base;                                  // empty = paths kept as they are

    std::string Out(const std::string& utf8) const {
        if (utf8.empty() || base.empty()) return utf8;
        fs::path p = UltraCanvas::PathFromUtf8(utf8);
        std::error_code ec;
        if (p.is_relative()) p = fs::absolute(p, ec);
        p = p.lexically_normal();
        const fs::path rel = p.lexically_relative(base);
        const bool inside = !rel.empty() && *rel.begin() != "..";
        std::string s = UltraCanvas::PathToUtf8(inside ? rel : p);
#ifdef _WIN32
        std::replace(s.begin(), s.end(), '\\', '/');    // elsewhere '\' is part of a name
#endif
        return s;
    }
    std::string In(const std::string& utf8) const {
        if (utf8.empty() || base.empty()) return utf8;
        const fs::path p = UltraCanvas::PathFromUtf8(utf8);
        return UltraCanvas::PathToUtf8(p.is_absolute() ? p.lexically_normal() : (base / p).lexically_normal());
    }
};

// ---------------------------------------------------------------------------
// reading helpers: missing key -> the default already in `out`
// ---------------------------------------------------------------------------

std::string Where(const std::string& at, const char* key) { return at.empty() ? key : at + "." + key; }

void Get(const Json& j, const char* key, double& out, const std::string& at) {
    if (!j.contains(key)) return;
    if (!j[key].is_number()) throw ReadError{Where(at, key) + ": a number is needed"};
    out = j[key].get<double>();
}
void Get(const Json& j, const char* key, int& out, const std::string& at) {
    if (!j.contains(key)) return;
    if (!j[key].is_number_integer()) throw ReadError{Where(at, key) + ": a whole number is needed"};
    out = j[key].get<int>();
}
void Get(const Json& j, const char* key, int64_t& out, const std::string& at) {
    if (!j.contains(key)) return;
    if (!j[key].is_number_integer()) throw ReadError{Where(at, key) + ": a whole number is needed"};
    out = j[key].get<int64_t>();
}
void Get(const Json& j, const char* key, bool& out, const std::string& at) {
    if (!j.contains(key)) return;
    if (!j[key].is_boolean()) throw ReadError{Where(at, key) + ": true or false is needed"};
    out = j[key].get<bool>();
}
void Get(const Json& j, const char* key, std::string& out, const std::string& at) {
    if (!j.contains(key)) return;
    if (!j[key].is_string()) throw ReadError{Where(at, key) + ": text is needed"};
    out = j[key].get<std::string>();
}
template <class E>
void Get(const Json& j, const char* key, E& out, const Names<E>& names, const std::string& at) {
    std::string name;
    Get(j, key, name, at);
    if (!name.empty()) out = ValueOf(names, name, Where(at, key));
}
void GetPath(const Json& j, const char* key, std::string& out, const Paths& paths, const std::string& at) {
    std::string s;
    Get(j, key, s, at);
    if (j.contains(key)) out = paths.In(s);
}
const Json& Array(const Json& j, const char* key, const std::string& at) {
    static const Json empty = Json::array();
    if (!j.contains(key)) return empty;
    if (!j[key].is_array()) throw ReadError{Where(at, key) + ": a list is needed"};
    return j[key];
}
const Json& Object(const Json& j, const char* key, const std::string& at) {
    static const Json empty = Json::object();
    if (!j.contains(key)) return empty;
    if (!j[key].is_object()) throw ReadError{Where(at, key) + ": an object is needed"};
    return j[key];
}

// Colours as "#RRGGBB"
std::string ColourOut(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06X", static_cast<unsigned>(rgb & 0xFFFFFF));
    return buf;
}
void GetColour(const Json& j, const char* key, uint32_t& out, const std::string& at) {
    std::string s;
    Get(j, key, s, at);
    if (!j.contains(key)) return;
    unsigned v = 0;
    char extra = 0;
    if (s.size() != 7 || s[0] != '#' || std::sscanf(s.c_str() + 1, "%6x%c", &v, &extra) != 1)
        throw ReadError{Where(at, key) + ": a colour \"#RRGGBB\" is needed"};
    out = v;
}

// ---------------------------------------------------------------------------
// writing
// ---------------------------------------------------------------------------

Json RectOut(const VideoFXRect& r) { return Json{{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}}; }

Json TransitionOut(const VideoFXTransition& t) {
    return Json{{"type", NameOf(MakeNames(kTransitions, "transition"), t.type)}, {"duration", t.duration}};
}

Json MotionOut(const VideoFXImageMotion& m) {
    return Json{{"style", NameOf(MakeNames(kMotions, "motion"), m.style)},
                {"startZoom", m.startZoom}, {"startX", m.startX}, {"startY", m.startY},
                {"endZoom", m.endZoom}, {"endX", m.endX}, {"endY", m.endY}, {"easeInOut", m.easeInOut}};
}

Json EffectOut(const VideoFXEffect& e, const Paths& paths) {
    Json j{{"type", NameOf(MakeNames(kEffects, "effect"), e.type)}, {"amount", e.amount}};
    if (e.type == VideoFXEffectType::Crop) {
        j["x"] = e.x; j["y"] = e.y; j["width"] = e.width; j["height"] = e.height;
    }
    if (e.type == VideoFXEffectType::LUT) j["path"] = paths.Out(e.path);
    return j;
}

Json OverlayOut(const VideoFXOverlay& o, const Paths& paths) {
    return Json{{"kind", NameOf(MakeNames(kOverlayKinds, "overlay kind"), o.kind)},
                {"text", o.text}, {"font", paths.Out(o.fontPath)}, {"fontSize", o.fontSize},
                {"textColor", ColourOut(o.textColor)}, {"shadow", o.shadow}, {"box", o.box},
                {"boxColor", ColourOut(o.boxColor)}, {"boxOpacity", o.boxOpacity},
                {"image", paths.Out(o.imagePath)}, {"imageHeight", o.imageHeight},
                {"anchor", NameOf(MakeNames(kAnchors, "anchor"), o.anchor)}, {"margin", o.margin},
                {"x", o.x}, {"y", o.y}, {"opacity", o.opacity},
                {"start", o.start}, {"end", o.end}, {"fadeIn", o.fadeIn}, {"fadeOut", o.fadeOut}};
}

Json SegmentOut(const VideoFXSegment& s, const Paths& paths) {
    Json effects = Json::array(), overlays = Json::array(), keep = Json::array();
    for (const VideoFXEffect& e : s.effects) effects.push_back(EffectOut(e, paths));
    for (const VideoFXOverlay& o : s.overlays) overlays.push_back(OverlayOut(o, paths));
    for (const VideoFXRect& r : s.keepInView) keep.push_back(RectOut(r));
    return Json{{"kind", NameOf(MakeNames(kSourceKinds, "segment kind"), s.kind)},
                {"path", paths.Out(s.path)}, {"start", s.start}, {"end", s.end}, {"duration", s.duration},
                {"color", ColourOut(s.color)}, {"speed", s.speed}, {"mute", s.mute},
                {"transitionIn", TransitionOut(s.transitionIn)}, {"motion", MotionOut(s.motion)},
                {"imageFit", NameOf(MakeNames(kImageFits, "image fit"), s.imageFit)},
                {"keepInView", keep}, {"keepFacesInView", s.keepFacesInView},
                {"effects", effects}, {"overlays", overlays}};
}

Json MusicOut(const VideoFXMusic& m, const Paths& paths) {
    Json playlist = Json::array();
    for (const std::string& song : m.playlist) playlist.push_back(paths.Out(song));
    return Json{{"path", paths.Out(m.path)}, {"playlist", playlist}, {"crossfade", m.crossfade},
                {"volume", m.volume}, {"start", m.start}, {"loop", m.loop},
                {"fadeIn", m.fadeIn}, {"fadeOut", m.fadeOut},
                {"duckingLevel", m.duckingLevel}, {"duckingThresholdDb", m.duckingThresholdDb},
                {"duckingAttack", m.duckingAttack}, {"duckingHold", m.duckingHold},
                {"duckingRelease", m.duckingRelease}};
}

Json SettingsOut(const VideoFXExportSettings& s, const Paths& paths) {
    return Json{{"container", NameOf(MakeNames(kContainers, "container"), s.container)},
                {"videoCodec", NameOf(MakeNames(kVideoCodecs, "video codec"), s.videoCodec)},
                {"audioCodec", NameOf(MakeNames(kAudioCodecs, "audio codec"), s.audioCodec)},
                {"width", s.width}, {"height", s.height}, {"frameRate", s.frameRate},
                {"fitMode", NameOf(MakeNames(kFitModes, "fit mode"), s.fitMode)},
                {"quality", s.quality}, {"videoBitRate", s.videoBitRate}, {"encoderPreset", s.encoderPreset},
                {"sampleRate", s.sampleRate}, {"channels", s.channels}, {"audioBitRate", s.audioBitRate},
                {"threads", s.threads}, {"music", MusicOut(s.music, paths)}};
}

Json SlideshowOut(const VideoFXProject& p, const Paths& paths) {
    const VideoFXSlideshowOptions& o = p.slideshow;
    Json images = Json::array(), captions = Json::array(), keep = Json::array();
    for (const std::string& image : p.images) images.push_back(paths.Out(image));
    for (const std::string& c : o.captions) captions.push_back(c);
    for (const auto& regions : o.keepInView) {
        Json list = Json::array();
        for (const VideoFXRect& r : regions) list.push_back(RectOut(r));
        keep.push_back(list);
    }
    return Json{{"images", images},
                {"options", Json{{"secondsPerImage", o.secondsPerImage}, {"transition", TransitionOut(o.transition)},
                                 {"motion", MotionOut(o.motion)},
                                 {"imageFit", NameOf(MakeNames(kImageFits, "image fit"), o.imageFit)},
                                 {"captions", captions}, {"fadeInOut", o.fadeInOut},
                                 {"music", MusicOut(o.music, paths)}, {"matchMusicLength", o.matchMusicLength},
                                 {"beatSync", o.beatSync}, {"beatsPerImage", o.beatsPerImage},
                                 {"keepInView", keep}, {"keepFacesInView", o.keepFacesInView}}}};
}

// ---------------------------------------------------------------------------
// reading
// ---------------------------------------------------------------------------

VideoFXRect RectIn(const Json& j, const std::string& at) {
    if (!j.is_object()) throw ReadError{at + ": a region {x, y, w, h} is needed"};
    VideoFXRect r;
    Get(j, "x", r.x, at); Get(j, "y", r.y, at); Get(j, "w", r.w, at); Get(j, "h", r.h, at);
    return r;
}

void TransitionIn(const Json& j, VideoFXTransition& t, const std::string& at) {
    Get(j, "type", t.type, MakeNames(kTransitions, "transition"), at);
    Get(j, "duration", t.duration, at);
}

void MotionIn(const Json& j, VideoFXImageMotion& m, const std::string& at) {
    Get(j, "style", m.style, MakeNames(kMotions, "motion"), at);
    Get(j, "startZoom", m.startZoom, at); Get(j, "startX", m.startX, at); Get(j, "startY", m.startY, at);
    Get(j, "endZoom", m.endZoom, at); Get(j, "endX", m.endX, at); Get(j, "endY", m.endY, at);
    Get(j, "easeInOut", m.easeInOut, at);
}

VideoFXEffect EffectIn(const Json& j, const Paths& paths, const std::string& at) {
    if (!j.is_object()) throw ReadError{at + ": an effect object is needed"};
    VideoFXEffect e;
    Get(j, "type", e.type, MakeNames(kEffects, "effect"), at);
    Get(j, "amount", e.amount, at);
    Get(j, "x", e.x, at); Get(j, "y", e.y, at); Get(j, "width", e.width, at); Get(j, "height", e.height, at);
    GetPath(j, "path", e.path, paths, at);
    return e;
}

VideoFXOverlay OverlayIn(const Json& j, const Paths& paths, const std::string& at) {
    if (!j.is_object()) throw ReadError{at + ": an overlay object is needed"};
    VideoFXOverlay o;
    Get(j, "kind", o.kind, MakeNames(kOverlayKinds, "overlay kind"), at);
    Get(j, "text", o.text, at);
    GetPath(j, "font", o.fontPath, paths, at);
    Get(j, "fontSize", o.fontSize, at);
    GetColour(j, "textColor", o.textColor, at);
    Get(j, "shadow", o.shadow, at); Get(j, "box", o.box, at);
    GetColour(j, "boxColor", o.boxColor, at);
    Get(j, "boxOpacity", o.boxOpacity, at);
    GetPath(j, "image", o.imagePath, paths, at);
    Get(j, "imageHeight", o.imageHeight, at);
    Get(j, "anchor", o.anchor, MakeNames(kAnchors, "anchor"), at);
    Get(j, "margin", o.margin, at); Get(j, "x", o.x, at); Get(j, "y", o.y, at); Get(j, "opacity", o.opacity, at);
    Get(j, "start", o.start, at); Get(j, "end", o.end, at);
    Get(j, "fadeIn", o.fadeIn, at); Get(j, "fadeOut", o.fadeOut, at);
    return o;
}

VideoFXSegment SegmentIn(const Json& j, const Paths& paths, const std::string& at) {
    if (!j.is_object()) throw ReadError{at + ": a segment object is needed"};
    VideoFXSegment s;
    Get(j, "kind", s.kind, MakeNames(kSourceKinds, "segment kind"), at);
    GetPath(j, "path", s.path, paths, at);
    Get(j, "start", s.start, at); Get(j, "end", s.end, at); Get(j, "duration", s.duration, at);
    GetColour(j, "color", s.color, at);
    Get(j, "speed", s.speed, at); Get(j, "mute", s.mute, at);
    TransitionIn(Object(j, "transitionIn", at), s.transitionIn, Where(at, "transitionIn"));
    MotionIn(Object(j, "motion", at), s.motion, Where(at, "motion"));
    Get(j, "imageFit", s.imageFit, MakeNames(kImageFits, "image fit"), at);
    const Json& keep = Array(j, "keepInView", at);
    for (size_t i = 0; i < keep.size(); ++i)
        s.keepInView.push_back(RectIn(keep[i], Where(at, "keepInView") + "[" + std::to_string(i) + "]"));
    Get(j, "keepFacesInView", s.keepFacesInView, at);
    const Json& effects = Array(j, "effects", at);
    for (size_t i = 0; i < effects.size(); ++i)
        s.effects.push_back(EffectIn(effects[i], paths, Where(at, "effects") + "[" + std::to_string(i) + "]"));
    const Json& overlays = Array(j, "overlays", at);
    for (size_t i = 0; i < overlays.size(); ++i)
        s.overlays.push_back(OverlayIn(overlays[i], paths, Where(at, "overlays") + "[" + std::to_string(i) + "]"));
    return s;
}

void MusicIn(const Json& j, VideoFXMusic& m, const Paths& paths, const std::string& at) {
    GetPath(j, "path", m.path, paths, at);
    const Json& playlist = Array(j, "playlist", at);
    m.playlist.clear();
    for (size_t i = 0; i < playlist.size(); ++i) {
        if (!playlist[i].is_string())
            throw ReadError{Where(at, "playlist") + "[" + std::to_string(i) + "]: a path is needed"};
        m.playlist.push_back(paths.In(playlist[i].get<std::string>()));
    }
    Get(j, "crossfade", m.crossfade, at); Get(j, "volume", m.volume, at); Get(j, "start", m.start, at);
    Get(j, "loop", m.loop, at); Get(j, "fadeIn", m.fadeIn, at); Get(j, "fadeOut", m.fadeOut, at);
    Get(j, "duckingLevel", m.duckingLevel, at); Get(j, "duckingThresholdDb", m.duckingThresholdDb, at);
    Get(j, "duckingAttack", m.duckingAttack, at); Get(j, "duckingHold", m.duckingHold, at);
    Get(j, "duckingRelease", m.duckingRelease, at);
}

void SettingsIn(const Json& j, VideoFXExportSettings& s, const Paths& paths, const std::string& at) {
    Get(j, "container", s.container, MakeNames(kContainers, "container"), at);
    Get(j, "videoCodec", s.videoCodec, MakeNames(kVideoCodecs, "video codec"), at);
    Get(j, "audioCodec", s.audioCodec, MakeNames(kAudioCodecs, "audio codec"), at);
    Get(j, "width", s.width, at); Get(j, "height", s.height, at); Get(j, "frameRate", s.frameRate, at);
    Get(j, "fitMode", s.fitMode, MakeNames(kFitModes, "fit mode"), at);
    Get(j, "quality", s.quality, at); Get(j, "videoBitRate", s.videoBitRate, at);
    Get(j, "encoderPreset", s.encoderPreset, at);
    Get(j, "sampleRate", s.sampleRate, at); Get(j, "channels", s.channels, at);
    Get(j, "audioBitRate", s.audioBitRate, at); Get(j, "threads", s.threads, at);
    MusicIn(Object(j, "music", at), s.music, paths, Where(at, "music"));
}

void SlideshowIn(const Json& j, VideoFXProject& p, const Paths& paths) {
    const std::string at = "slideshow";
    const Json& images = Array(j, "images", at);
    for (size_t i = 0; i < images.size(); ++i) {
        if (!images[i].is_string()) throw ReadError{"slideshow.images[" + std::to_string(i) + "]: a path is needed"};
        p.images.push_back(paths.In(images[i].get<std::string>()));
    }
    const Json& o = Object(j, "options", at);
    const std::string ot = "slideshow.options";
    VideoFXSlideshowOptions& s = p.slideshow;
    Get(o, "secondsPerImage", s.secondsPerImage, ot);
    TransitionIn(Object(o, "transition", ot), s.transition, ot + ".transition");
    MotionIn(Object(o, "motion", ot), s.motion, ot + ".motion");
    Get(o, "imageFit", s.imageFit, MakeNames(kImageFits, "image fit"), ot);
    const Json& captions = Array(o, "captions", ot);
    for (size_t i = 0; i < captions.size(); ++i) {
        if (!captions[i].is_string()) throw ReadError{ot + ".captions[" + std::to_string(i) + "]: text is needed"};
        s.captions.push_back(captions[i].get<std::string>());
    }
    Get(o, "fadeInOut", s.fadeInOut, ot);
    MusicIn(Object(o, "music", ot), s.music, paths, ot + ".music");
    Get(o, "matchMusicLength", s.matchMusicLength, ot);
    Get(o, "beatSync", s.beatSync, ot);
    Get(o, "beatsPerImage", s.beatsPerImage, ot);
    const Json& keep = Array(o, "keepInView", ot);
    for (size_t i = 0; i < keep.size(); ++i) {
        const std::string where = ot + ".keepInView[" + std::to_string(i) + "]";
        if (!keep[i].is_array()) throw ReadError{where + ": a list of regions is needed"};
        std::vector<VideoFXRect> regions;
        for (size_t k = 0; k < keep[i].size(); ++k)
            regions.push_back(RectIn(keep[i][k], where + "[" + std::to_string(k) + "]"));
        s.keepInView.push_back(std::move(regions));
    }
    Get(o, "keepFacesInView", s.keepFacesInView, ot);
}

// The base directory to resolve against, from a project file's path
fs::path BaseOf(const std::string& projectPath) {
    std::error_code ec;
    fs::path p = fs::absolute(UltraCanvas::PathFromUtf8(projectPath), ec);
    return p.parent_path().lexically_normal();
}

fs::path BaseDir(const std::string& utf8) {
    if (utf8.empty()) return {};
    std::error_code ec;
    return fs::absolute(UltraCanvas::PathFromUtf8(utf8), ec).lexically_normal();
}

} // namespace

std::vector<std::string> ProjectMedia(const VideoFXProject& p) {
    std::vector<std::string> media;
    std::set<std::string> seen;
    auto add = [&](const std::string& path) {
        if (!path.empty() && seen.insert(path).second) media.push_back(path);
    };
    auto music = [&](const VideoFXMusic& m) {
        for (const std::string& song : m.Songs()) add(song);
    };
    if (p.kind == VideoFXProjectKind::Timeline) {
        for (const VideoFXSegment& s : p.segments) {
            if (s.kind == VideoFXSourceKind::File || s.kind == VideoFXSourceKind::Image) add(s.path);
            for (const VideoFXEffect& e : s.effects)
                if (e.type == VideoFXEffectType::LUT) add(e.path);
            for (const VideoFXOverlay& o : s.overlays) {
                if (o.kind == VideoFXOverlayKind::Image) add(o.imagePath);
                add(o.fontPath);
            }
        }
    } else {
        for (const std::string& image : p.images) add(image);
        music(p.slideshow.music);
    }
    music(p.settings.music);
    return media;
}

VideoFXResult ProjectToJsonText(const VideoFXProject& p, const std::string& baseDirectory, std::string& json,
                                std::string& error) {
    // Pixels in memory cannot be pointed to from a file
    for (size_t i = 0; i < p.segments.size(); ++i) {
        const VideoFXSegment& s = p.segments[i];
        if (s.kind == VideoFXSourceKind::Image && s.path.empty() && s.image.IsValid()) {
            error = "Segment " + std::to_string(i + 1) + " is an image in memory; a project needs its file";
            return VideoFXResult::InvalidArgument;
        }
        for (const VideoFXOverlay& o : s.overlays)
            if (o.kind == VideoFXOverlayKind::Image && o.imagePath.empty() && o.image.IsValid()) {
                error = "An overlay of segment " + std::to_string(i + 1) +
                        " is an image in memory; a project needs its file";
                return VideoFXResult::InvalidArgument;
            }
    }
    const Paths paths{BaseDir(baseDirectory)};
    Json j{{"format", kFormatName}, {"version", kVideoFXProjectFormatVersion},
           {"kind", NameOf(MakeNames(kProjectKinds, "project kind"), p.kind)}, {"title", p.title},
           {"output", paths.Out(p.outputPath)}, {"settings", SettingsOut(p.settings, paths)}};
    if (p.kind == VideoFXProjectKind::Timeline) {
        Json segments = Json::array();
        for (const VideoFXSegment& s : p.segments) segments.push_back(SegmentOut(s, paths));
        j["segments"] = segments;
    } else {
        j["slideshow"] = SlideshowOut(p, paths);
    }
    json = j.dump(2) + "\n";
    return VideoFXResult::Ok;
}

VideoFXResult ProjectFromJsonText(const std::string& text, const std::string& baseDirectory, VideoFXProject& out,
                                  std::string& error) {
    Json j;
    try {
        j = Json::parse(text);
    } catch (const nlohmann::json::exception& e) {
        error = std::string("Not a valid project file: ") + e.what();
        return VideoFXResult::InvalidArgument;
    }
    if (!j.is_object() || !j.contains("format") || j["format"] != kFormatName) {
        error = "Not a VideoFX project file";
        return VideoFXResult::InvalidArgument;
    }
    try {
        int version = 0;
        Get(j, "version", version, "");
        if (version < 1) throw ReadError{"version: missing or not a format version"};
        if (version > kVideoFXProjectFormatVersion)
            throw ReadError{"The project was written by a newer VideoFX (format " + std::to_string(version) +
                            "; this one reads up to " + std::to_string(kVideoFXProjectFormatVersion) + ")"};
        const Paths paths{BaseDir(baseDirectory)};
        VideoFXProject p;
        Get(j, "kind", p.kind, MakeNames(kProjectKinds, "project kind"), "");
        Get(j, "title", p.title, "");
        GetPath(j, "output", p.outputPath, paths, "");
        SettingsIn(Object(j, "settings", ""), p.settings, paths, "settings");
        if (p.kind == VideoFXProjectKind::Timeline) {
            const Json& segments = Array(j, "segments", "");
            for (size_t i = 0; i < segments.size(); ++i)
                p.segments.push_back(SegmentIn(segments[i], paths, "segments[" + std::to_string(i) + "]"));
        } else {
            SlideshowIn(Object(j, "slideshow", ""), p, paths);
        }
        out = std::move(p);
    } catch (const ReadError& e) {
        error = "Project file: " + e.message;
        return VideoFXResult::InvalidArgument;
    } catch (const nlohmann::json::exception& e) {
        error = std::string("Project file: ") + e.what();
        return VideoFXResult::InvalidArgument;
    }
    return VideoFXResult::Ok;
}

VideoFXResult SaveProjectFile(const VideoFXProject& project, const std::string& path, std::string& error) {
    if (path.empty()) {
        error = "No project file given";
        return VideoFXResult::InvalidArgument;
    }
    std::string json;
    VideoFXResult r = ProjectToJsonText(project, UltraCanvas::PathToUtf8(BaseOf(path)), json, error);
    if (r != VideoFXResult::Ok) return r;
    // Written beside the old file and moved over it only when complete
    const fs::path target = UltraCanvas::PathFromUtf8(path);
    fs::path temp = target;
    temp += ".saving";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write(json.data(), static_cast<std::streamsize>(json.size())) || !f.flush()) {
            std::error_code ec;
            fs::remove(temp, ec);
            error = "Cannot write the project file " + path;
            return VideoFXResult::WriteError;
        }
    }
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        error = "Cannot write the project file " + path;
        return VideoFXResult::WriteError;
    }
    return VideoFXResult::Ok;
}

VideoFXResult LoadProjectFile(const std::string& path, VideoFXProject& project, std::vector<std::string>* missingMedia,
                              std::string& error) {
    std::ifstream f(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!f) {
        std::error_code ec;
        const bool exists = fs::exists(UltraCanvas::PathFromUtf8(path), ec);
        error = (exists ? "Cannot read the project file " : "No such project file: ") + path;
        return exists ? VideoFXResult::OpenFailed : VideoFXResult::FileNotFound;
    }
    std::ostringstream text;
    text << f.rdbuf();
    VideoFXProject p;
    VideoFXResult r = ProjectFromJsonText(text.str(), UltraCanvas::PathToUtf8(BaseOf(path)), p, error);
    if (r != VideoFXResult::Ok) return r;
    if (missingMedia) {
        missingMedia->clear();
        for (const std::string& m : ProjectMedia(p)) {
            std::error_code ec;
            if (!fs::exists(UltraCanvas::PathFromUtf8(m), ec)) missingMedia->push_back(m);
        }
    }
    project = std::move(p);
    return VideoFXResult::Ok;
}

} // namespace Internal
} // namespace VideoFX
