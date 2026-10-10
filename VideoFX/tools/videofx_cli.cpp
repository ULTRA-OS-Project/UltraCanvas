// VideoFX/tools/videofx_cli.cpp
// `videofx` - command-line front end to the VideoFX module, for scripts and
// for trying the engine without writing code.
//
//   videofx info <file>
//   videofx beats <file>                 tempo and beat times of its sound
//   videofx faces <image>                frontal faces in a photo, as fractions of it
//   videofx render <project.vfxproj> [out]   render a saved project (to its own output by default)
//   videofx project <project.vfxproj>    what a project holds, and which of its media are missing
//   videofx frame <file> <seconds> <out.png|out.jpg> [maxWidth maxHeight]
//   videofx transcode <in> <out> [options]
//   videofx trim <in> <out> <start> <end> [--lossless] [options]
//   videofx concat <out> <in1> <in2> ... [options]
//   videofx effects <in> <out> <effect[=value]>... [options]
//   videofx testclip <out> <seconds> [width height fps]
//   videofx slideshow <out> <image1> <image2> ... [--seconds S] [--motion M]
//                     [--caption TEXT]... [--transition NAME[:SECONDS]]
// motion: auto still zoomin zoomout panleft panright panup pandown
// fit (--fit): auto cover contain blur
//
// options: --width N --height N --fps F --quality 0..100 --speed S
//          --transition NAME[:SECONDS]   between joined files (concat)
//          --title TEXT                  caption at the bottom, faded in and out
//          --watermark IMAGE             logo in the top-right corner
//          --font FONTFILE               font for --title (default: the bundled Ubuntu font)
//          --music FILE                  background music under the whole export; repeat it
//                                        for a song list, played in order
//          --music-crossfade S           seconds each song blends into the next (default 3)
//          --music-volume V --music-start S --duck LEVEL --no-loop
//          --duck-threshold DB --duck-attack S --duck-hold S --duck-release S
//                                        when the clips' sound ducks the music (default
//                                        -36.5 dBFS, 0.12 s, 0.6 s, 0.8 s: tuned for speech)
//          --duck-preset speech|outdoor|loud   all four for the kind of footage; the
//                                        single values above refine it, in any order
//          --fit-music                   slideshow: seconds per image chosen to end with the music
//          --beat-sync                   slideshow: change images on the music's beats
//          --beats-per-image N           slideshow: every image exactly N beats (implies --beat-sync)
//          --keep N:X,Y,W,H              slideshow: keep this region of image N (1 = the first) in
//                                        shot - a face; fractions of the image; repeatable
//          --no-faces                    slideshow: do not look for faces to keep in shot
//          --save-project FILE           write the edit as a project file instead of rendering it
// transitions: crossfade dissolve fadeblack fadewhite wipeleft wiperight
//          wipeup wipedown slideleft slideright slideup slidedown smoothleft
//          smoothright smoothup smoothdown circleopen circleclose circlecrop
//          rectcrop radial pixelize blur distance diagtl diagtr diagbl diagbr
//          squeezeh squeezev
//          --vcodec h264|h265|vp8|vp9|av1|mpeg4|mjpeg|prores|ffv1|gif|none
//          --acodec aac|mp3|opus|vorbis|flac|pcm|none
// effects: brightness=v contrast=v saturation=v gamma=v exposure=v hue=deg
//          temperature=v grayscale sepia invert blur=r sharpen=v denoise=v
//          vignette=v rotate90 rotate180 rotate270 rotate=deg hflip vflip
//          crop=x:y:w:h fadein=s fadeout=s volume=g normalize[=lufs] lut=path
// Version: 0.7.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include <VideoFX/VideoFX.h>

#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

using namespace VideoFX;

namespace {

// Command-line numbers are dot-decimal whatever the locale, like file formats
bool ParseNumber(const std::string& text, double& out) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    in >> out;
    return !in.fail() && in.peek() == std::char_traits<char>::eof();
}

constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();

double NumberOr(const std::string& text, double fallback) {
    double v = 0.0;
    return ParseNumber(text, v) ? v : fallback;
}

int Usage() {
    std::cerr <<
        "usage: videofx info <file>\n"
        "       videofx beats <file>\n"
        "       videofx faces <image>\n"
        "       videofx render <project.vfxproj> [out]\n"
        "       videofx project <project.vfxproj>\n"
        "       videofx frame <file> <seconds> <out.png|out.jpg> [maxWidth maxHeight]\n"
        "       videofx transcode <in> <out> [options]\n"
        "       videofx trim <in> <out> <start> <end> [--lossless] [options]\n"
        "       videofx concat <out> <in1> <in2> ... [options]\n"
        "       videofx effects <in> <out> <effect[=value]>... [options]\n"
        "       videofx testclip <out> <seconds> [width height fps]\n"
        "       videofx slideshow <out> <image>... [--seconds S] [--motion M] [--fit F] [--caption TEXT]...\n"
        "options: --width N --height N --fps F --quality 0..100 --speed S\n"
        "         --transition NAME[:SECONDS] --title TEXT --watermark IMAGE --font FONTFILE\n"
        "         --music FILE [--music FILE2 ...] [--music-crossfade S]\n"
        "         [--music-volume V] [--music-start S] [--duck LEVEL] [--no-loop] [--fit-music]\n"
        "         [--beat-sync] [--beats-per-image N] [--keep N:X,Y,W,H]... [--no-faces]\n"
        "         --save-project FILE   (write the edit as a project instead of rendering it)\n"
        "         [--duck-preset speech|outdoor|loud]\n"
        "         [--duck-threshold DB] [--duck-attack S] [--duck-hold S] [--duck-release S]\n"
        "         --vcodec h264|h265|vp8|vp9|av1|mpeg4|mjpeg|prores|ffv1|gif|none\n"
        "         --acodec aac|mp3|opus|vorbis|flac|pcm|none\n";
    return 2;
}

int Report(VideoFXResult r) {
    if (r == VideoFXResult::Ok) return 0;
    std::cerr << "videofx: " << VideoFX_ResultToString(r) << " - " << VideoFX_GetLastError() << "\n";
    return 1;
}

bool Progress(double f) {
    std::cerr << "\r" << static_cast<int>(f * 100.0) << "%   " << std::flush;
    if (f >= 1.0) std::cerr << "\n";
    return true;
}

bool ParseVideoCodec(const std::string& s, VideoFXVideoCodec& c) {
    static const std::pair<const char*, VideoFXVideoCodec> names[] = {
        {"h264", VideoFXVideoCodec::H264}, {"h265", VideoFXVideoCodec::H265}, {"hevc", VideoFXVideoCodec::H265},
        {"vp8", VideoFXVideoCodec::VP8},   {"vp9", VideoFXVideoCodec::VP9},   {"av1", VideoFXVideoCodec::AV1},
        {"mpeg4", VideoFXVideoCodec::MPEG4}, {"mjpeg", VideoFXVideoCodec::MJPEG},
        {"prores", VideoFXVideoCodec::ProRes}, {"ffv1", VideoFXVideoCodec::FFV1}, {"gif", VideoFXVideoCodec::GIF},
        {"none", VideoFXVideoCodec::Disabled}};
    for (const auto& n : names) if (s == n.first) { c = n.second; return true; }
    return false;
}

bool ParseAudioCodec(const std::string& s, VideoFXAudioCodec& c) {
    static const std::pair<const char*, VideoFXAudioCodec> names[] = {
        {"aac", VideoFXAudioCodec::AAC}, {"mp3", VideoFXAudioCodec::MP3}, {"opus", VideoFXAudioCodec::Opus},
        {"vorbis", VideoFXAudioCodec::Vorbis}, {"flac", VideoFXAudioCodec::FLAC}, {"pcm", VideoFXAudioCodec::PCM16},
        {"none", VideoFXAudioCodec::Disabled}};
    for (const auto& n : names) if (s == n.first) { c = n.second; return true; }
    return false;
}

struct Options {
    VideoFXExportSettings settings;
    double speed = 1.0;
    bool lossless = false;
    VideoFXTransition transition;
    bool transitionGiven = false;
    std::vector<VideoFXOverlay> overlays;
    VideoFXSlideshowOptions slideshow;
    std::string saveProject;            // --save-project: write the project, do not render
};

bool ParseMotion(const std::string& s, VideoFXImageMotion& m) {
    static const std::pair<const char*, VideoFXMotionStyle> names[] = {
        {"auto", VideoFXMotionStyle::Auto}, {"still", VideoFXMotionStyle::Still},
        {"zoomin", VideoFXMotionStyle::ZoomIn}, {"zoomout", VideoFXMotionStyle::ZoomOut},
        {"panleft", VideoFXMotionStyle::PanLeft}, {"panright", VideoFXMotionStyle::PanRight},
        {"panup", VideoFXMotionStyle::PanUp}, {"pandown", VideoFXMotionStyle::PanDown}};
    for (const auto& n : names) {
        if (s == n.first) { m = VideoFXImageMotion::Make(n.second); return true; }
    }
    return false;
}

bool ParseDuckingPreset(const std::string& s, VideoFXMusic& m) {
    static const std::pair<const char*, VideoFXDuckingPreset> names[] = {
        {"speech", VideoFXDuckingPreset::Speech}, {"outdoor", VideoFXDuckingPreset::Outdoor},
        {"loud", VideoFXDuckingPreset::LoudEvent}};
    for (const auto& n : names) {
        if (s == n.first) { m.SetDuckingPreset(n.second); return true; }
    }
    return false;
}

// "N:X,Y,W,H" - image N (1-based) and a region of it, as fractions
bool ParseKeep(const std::string& spec, std::vector<std::vector<VideoFXRect>>& keep) {
    const size_t colon = spec.find(':');
    if (colon == std::string::npos) return false;
    double n = 0.0;
    if (!ParseNumber(spec.substr(0, colon), n) || n < 1.0 || n > 100000.0 || n != std::floor(n)) return false;
    double v[4];
    size_t from = colon + 1;
    for (int i = 0; i < 4; ++i) {
        const size_t comma = i < 3 ? spec.find(',', from) : spec.size();
        if (comma == std::string::npos || !ParseNumber(spec.substr(from, comma - from), v[i])) return false;
        from = comma + 1;
    }
    const size_t index = static_cast<size_t>(n) - 1;
    if (keep.size() <= index) keep.resize(index + 1);
    keep[index].push_back(VideoFXRect::Make(v[0], v[1], v[2], v[3]));
    return true;
}

bool ParseTransition(const std::string& spec, VideoFXTransition& t) {
    static const std::pair<const char*, VideoFXTransitionType> names[] = {
        {"crossfade", VideoFXTransitionType::Crossfade}, {"dissolve", VideoFXTransitionType::Dissolve},
        {"fadeblack", VideoFXTransitionType::FadeThroughBlack}, {"fadewhite", VideoFXTransitionType::FadeThroughWhite},
        {"wipeleft", VideoFXTransitionType::WipeLeft}, {"wiperight", VideoFXTransitionType::WipeRight},
        {"wipeup", VideoFXTransitionType::WipeUp}, {"wipedown", VideoFXTransitionType::WipeDown},
        {"slideleft", VideoFXTransitionType::SlideLeft}, {"slideright", VideoFXTransitionType::SlideRight},
        {"slideup", VideoFXTransitionType::SlideUp}, {"slidedown", VideoFXTransitionType::SlideDown},
        {"smoothleft", VideoFXTransitionType::SmoothLeft}, {"smoothright", VideoFXTransitionType::SmoothRight},
        {"smoothup", VideoFXTransitionType::SmoothUp}, {"smoothdown", VideoFXTransitionType::SmoothDown},
        {"circleopen", VideoFXTransitionType::CircleOpen}, {"circleclose", VideoFXTransitionType::CircleClose},
        {"circlecrop", VideoFXTransitionType::CircleCrop}, {"rectcrop", VideoFXTransitionType::RectCrop},
        {"radial", VideoFXTransitionType::Radial}, {"pixelize", VideoFXTransitionType::Pixelize},
        {"blur", VideoFXTransitionType::Blur}, {"distance", VideoFXTransitionType::Distance},
        {"diagtl", VideoFXTransitionType::DiagonalTopLeft}, {"diagtr", VideoFXTransitionType::DiagonalTopRight},
        {"diagbl", VideoFXTransitionType::DiagonalBottomLeft}, {"diagbr", VideoFXTransitionType::DiagonalBottomRight},
        {"squeezeh", VideoFXTransitionType::SqueezeHorizontal}, {"squeezev", VideoFXTransitionType::SqueezeVertical}};
    const size_t colon = spec.find(':');
    const std::string name = spec.substr(0, colon);
    t.duration = colon == std::string::npos ? 1.0 : NumberOr(spec.substr(colon + 1), -1.0);
    for (const auto& n : names) {
        if (name == n.first) { t.type = n.second; return true; }
    }
    return false;
}

// Speed, overlays and (after the first) the transition, on every segment
void Decorate(VideoFXSegment& s, const Options& o, bool first) {
    s.speed = o.speed;
    s.overlays = o.overlays;
    if (!first) s.transitionIn = o.transition;
}

// Pulls --options out of args; what remains are positional arguments
bool ParseOptions(std::vector<std::string>& args, Options& o) {
    VideoFXExportSettings& settings = o.settings;
    double& speed = o.speed;
    bool& lossless = o.lossless;
    std::vector<std::string> rest;
    // A preset first, wherever it is written, so --duck-threshold and the
    // other single values always refine it rather than being overwritten
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--duck-preset" && !ParseDuckingPreset(args[i + 1], settings.music)) return false;
    }
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](std::string& v) {
            if (i + 1 >= args.size()) return false;
            v = args[++i];
            return true;
        };
        std::string v;
        if (a == "--lossless") lossless = true;
        else if (a == "--width" && next(v)) settings.width = static_cast<int>(NumberOr(v, 0));
        else if (a == "--height" && next(v)) settings.height = static_cast<int>(NumberOr(v, 0));
        else if (a == "--fps" && next(v)) settings.frameRate = NumberOr(v, 0);
        else if (a == "--quality" && next(v)) settings.quality = static_cast<int>(NumberOr(v, -1));
        else if (a == "--speed" && next(v)) speed = NumberOr(v, 1.0);
        else if (a == "--vcodec" && next(v)) { if (!ParseVideoCodec(v, settings.videoCodec)) return false; }
        else if (a == "--acodec" && next(v)) { if (!ParseAudioCodec(v, settings.audioCodec)) return false; }
        else if (a == "--transition" && next(v)) {
            if (!ParseTransition(v, o.transition)) return false;
            o.transitionGiven = true;
        }
        else if (a == "--seconds" && next(v)) o.slideshow.secondsPerImage = NumberOr(v, -1.0);
        else if (a == "--motion" && next(v)) { if (!ParseMotion(v, o.slideshow.motion)) return false; }
        else if (a == "--caption" && next(v)) o.slideshow.captions.push_back(v);
        else if (a == "--music" && next(v)) {
            if (settings.music.path.empty()) settings.music.path = v;
            else settings.music.playlist.push_back(v);
        }
        else if (a == "--music-crossfade" && next(v)) settings.music.crossfade = NumberOr(v, -1.0);
        else if (a == "--music-volume" && next(v)) settings.music.volume = NumberOr(v, -1.0);
        else if (a == "--music-start" && next(v)) settings.music.start = NumberOr(v, -1.0);
        else if (a == "--duck" && next(v)) settings.music.duckingLevel = NumberOr(v, -1.0);
        // NaN, not -1, for a value that does not parse: -1 dBFS is a valid threshold
        else if (a == "--duck-preset" && next(v)) {}             // applied above
        else if (a == "--duck-threshold" && next(v)) settings.music.duckingThresholdDb = NumberOr(v, kNotANumber);
        else if (a == "--duck-attack" && next(v)) settings.music.duckingAttack = NumberOr(v, kNotANumber);
        else if (a == "--duck-hold" && next(v)) settings.music.duckingHold = NumberOr(v, kNotANumber);
        else if (a == "--duck-release" && next(v)) settings.music.duckingRelease = NumberOr(v, kNotANumber);
        else if (a == "--no-loop") settings.music.loop = false;
        else if (a == "--fit-music") o.slideshow.matchMusicLength = true;
        else if (a == "--beat-sync") o.slideshow.beatSync = true;
        else if (a == "--no-faces") o.slideshow.keepFacesInView = false;
        else if (a == "--save-project" && next(v)) o.saveProject = v;
        else if (a == "--keep" && next(v)) { if (!ParseKeep(v, o.slideshow.keepInView)) return false; }
        else if (a == "--beats-per-image" && next(v)) o.slideshow.beatsPerImage = static_cast<int>(NumberOr(v, -1.0));
        else if (a == "--fit" && next(v)) {
            if (v == "auto") o.slideshow.imageFit = VideoFXImageFit::Auto;
            else if (v == "cover") o.slideshow.imageFit = VideoFXImageFit::Cover;
            else if (v == "contain") o.slideshow.imageFit = VideoFXImageFit::Contain;
            else if (v == "blur") o.slideshow.imageFit = VideoFXImageFit::BlurredBackground;
            else return false;
        }
        else if (a == "--title" && next(v)) {
            VideoFXOverlay t = VideoFXOverlay::Text(v, VideoFXAnchor::Bottom, 0.06);
            t.box = true;
            t.fadeIn = 0.5;
            t.fadeOut = 0.5;
            o.overlays.push_back(t);
        } else if (a == "--font" && next(v)) {
            if (!VideoFX_SetDefaultFontPath(v)) {
                std::cerr << "videofx: font file not found: " << v << "\n";
                return false;
            }
        } else if (a == "--watermark" && next(v)) {
            VideoFXOverlay w = VideoFXOverlay::Image(v, VideoFXAnchor::TopRight, 0.12);
            w.opacity = 0.85;
            o.overlays.push_back(w);
        }
        else if (a.rfind("--", 0) == 0) return false;
        else rest.push_back(a);
    }
    args = rest;
    return true;
}

bool ParseEffect(const std::string& spec, VideoFXEffect& e) {
    const size_t eq = spec.find('=');
    const std::string name = spec.substr(0, eq);
    const std::string value = eq == std::string::npos ? "" : spec.substr(eq + 1);
    double v = 0.0;
    const bool hasNumber = ParseNumber(value, v);
    if (name == "grayscale") e = VideoFXEffect::Grayscale();
    else if (name == "sepia") e = VideoFXEffect::Sepia();
    else if (name == "invert") e = VideoFXEffect::Invert();
    else if (name == "rotate90") e = VideoFXEffect::Rotate90();
    else if (name == "rotate180") e = VideoFXEffect::Rotate180();
    else if (name == "rotate270") e = VideoFXEffect::Rotate270();
    else if (name == "hflip") e = VideoFXEffect::FlipHorizontal();
    else if (name == "vflip") e = VideoFXEffect::FlipVertical();
    else if (name == "normalize") e = VideoFXEffect::NormalizeAudio(hasNumber ? v : -16.0);
    else if (name == "lut" && !value.empty()) e = VideoFXEffect::LUT(value);
    else if (name == "crop") {
        int x = 0, y = 0, w = 0, h = 0;
        if (std::sscanf(value.c_str(), "%d:%d:%d:%d", &x, &y, &w, &h) != 4) return false;
        e = VideoFXEffect::Crop(x, y, w, h);
    } else if (!hasNumber) return false;
    else if (name == "brightness") e = VideoFXEffect::Brightness(v);
    else if (name == "contrast") e = VideoFXEffect::Contrast(v);
    else if (name == "saturation") e = VideoFXEffect::Saturation(v);
    else if (name == "gamma") e = VideoFXEffect::Gamma(v);
    else if (name == "exposure") e = VideoFXEffect::Exposure(v);
    else if (name == "hue") e = VideoFXEffect::Hue(v);
    else if (name == "temperature") e = VideoFXEffect::Temperature(v);
    else if (name == "blur") e = VideoFXEffect::Blur(v);
    else if (name == "sharpen") e = VideoFXEffect::Sharpen(v);
    else if (name == "denoise") e = VideoFXEffect::Denoise(v);
    else if (name == "vignette") e = VideoFXEffect::Vignette(v);
    else if (name == "rotate") e = VideoFXEffect::Rotate(v);
    else if (name == "fadein") e = VideoFXEffect::FadeIn(v);
    else if (name == "fadeout") e = VideoFXEffect::FadeOut(v);
    else if (name == "volume") e = VideoFXEffect::Volume(v);
    else return false;
    return true;
}

// The arguments as UTF-8. On Windows argv is in the ANSI code page, which
// cannot hold a Thai or emoji file name, so they are read as UTF-16 instead.
std::vector<std::string> Utf8Arguments(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    std::vector<std::string> out;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 0; wide && i < count; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, utf8.data(), n, nullptr, nullptr);
        out.push_back(std::move(utf8));
    }
    if (wide) LocalFree(wide);
    return out;
#else
    return std::vector<std::string>(argv, argv + argc);
#endif
}

const char* KindName(VideoFXStreamKind k) {
    switch (k) {
        case VideoFXStreamKind::Video: return "video";
        case VideoFXStreamKind::Audio: return "audio";
        case VideoFXStreamKind::Subtitle: return "subtitle";
        case VideoFXStreamKind::Data: return "data";
        case VideoFXStreamKind::Attachment: return "attachment";
        default: return "unknown";
    }
}

int Beats(const std::string& path) {
    VideoFXBeatInfo info;
    VideoFXResult r = VideoFX_DetectBeats(path, info);
    if (r != VideoFXResult::Ok) return Report(r);
    std::cout.imbue(std::locale::classic());
    std::cout << std::fixed << std::setprecision(1);
    if (!info.HasBeat()) {
        std::cout << path << ": no steady beat\n";
        return 0;
    }
    std::cout << path << ": " << info.bpm << " BPM (confidence " << std::setprecision(2) << info.confidence << "), "
              << info.beats.size() << " beats\n" << std::setprecision(3);
    for (size_t i = 0; i < info.beats.size(); ++i) std::cout << (i % 8 ? " " : (i ? "\n  " : "  ")) << info.beats[i];
    std::cout << "\n";
    return 0;
}

int Faces(const std::string& path) {
    VideoFXFrame image;
    VideoFXResult r = VideoFX_ExtractFrame(path, 0.0, image);
    std::vector<VideoFXRect> faces;
    if (r == VideoFXResult::Ok) r = VideoFX_DetectFaces(image, faces);
    if (r != VideoFXResult::Ok) return Report(r);
    std::cout.imbue(std::locale::classic());
    std::cout << path << ": " << faces.size() << (faces.size() == 1 ? " face" : " faces") << " ("
              << image.width << "x" << image.height << ")\n" << std::fixed << std::setprecision(3);
    for (const VideoFXRect& f : faces)
        std::cout << "  " << f.x << "," << f.y << "," << f.w << "," << f.h << "   (" << std::lround(f.x * image.width)
                  << ", " << std::lround(f.y * image.height) << ", " << std::lround(f.w * image.width) << " x "
                  << std::lround(f.h * image.height) << " px)\n";
    return 0;
}

// Render, or with --save-project write the project for later
int RunOrSave(VideoFXProject project, const std::string& output, const Options& o) {
    project.outputPath = output;
    if (o.saveProject.empty()) return Report(VideoFX_RenderProject(project, output, Progress));
    VideoFXResult r = VideoFX_SaveProject(project, o.saveProject);
    if (r == VideoFXResult::Ok) std::cout << "videofx: wrote " << o.saveProject << " - render it with videofx render\n";
    return Report(r);
}

int ShowProject(const std::string& path) {
    VideoFXProject p;
    std::vector<std::string> missing;
    VideoFXResult r = VideoFX_LoadProject(path, p, &missing);
    if (r != VideoFXResult::Ok) return Report(r);
    std::cout.imbue(std::locale::classic());
    std::cout << path << (p.title.empty() ? "" : ": " + p.title) << "\n  "
              << (p.kind == VideoFXProjectKind::Slideshow ? std::to_string(p.images.size()) + " photos (slideshow)"
                                                          : std::to_string(p.segments.size()) + " segments")
              << "\n  output: " << (p.outputPath.empty() ? std::string("(none set)") : p.outputPath) << "\n";
    const VideoFXMusic& music = p.kind == VideoFXProjectKind::Slideshow && p.slideshow.music.IsSet()
                                    ? p.slideshow.music : p.settings.music;
    if (music.IsSet()) std::cout << "  music:  " << music.Songs().size() << " song(s)\n";
    if (missing.empty()) {
        std::cout << "  all media present\n";
        return 0;
    }
    std::cout << "  missing (" << missing.size() << "):\n";
    for (const std::string& m : missing) std::cout << "    " << m << "\n";
    return 1;
}

int Info(const std::string& path) {
    VideoFXMediaInfo info;
    VideoFXResult r = VideoFX_Probe(path, info);
    if (r != VideoFXResult::Ok) return Report(r);
    std::cout.imbue(std::locale::classic());
    std::cout << std::fixed << std::setprecision(3);
    std::cout << info.path << "\n  format:   " << info.formatLongName << " (" << info.formatName << ")\n"
              << "  duration: " << info.duration << " s\n  size:     " << info.fileSize << " bytes\n";
    if (info.HasVideo())
        std::cout << "  picture:  " << info.width << "x" << info.height << " @ " << info.frameRate << " fps\n";
    if (info.HasAudio())
        std::cout << "  sound:    " << info.sampleRate << " Hz, " << info.channels << " ch\n";
    for (const auto& s : info.streams) {
        std::cout << "  #" << s.index << " " << KindName(s.kind) << " " << s.codecName;
        if (s.kind == VideoFXStreamKind::Video)
            std::cout << " " << s.width << "x" << s.height << " " << s.pixelFormat
                      << (s.rotation ? " rotated " + std::to_string(s.rotation) : "");
        if (s.kind == VideoFXStreamKind::Audio) std::cout << " " << s.sampleRate << " Hz " << s.channelLayout;
        if (!s.language.empty()) std::cout << " [" << s.language << "]";
        std::cout << "\n";
    }
    for (const auto& [k, v] : info.metadata) std::cout << "  " << k << ": " << v << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args = Utf8Arguments(argc, argv);
    if (args.size() < 2) return Usage();
    const std::string cmd = args[1];
    args.erase(args.begin(), args.begin() + 2);
    if (cmd == "--version" || cmd == "version") {
        std::cout << "videofx " << VideoFX_GetVersion() << " - " << VideoFX_GetBackendVersion() << "\n";
        return 0;
    }
    if (!VideoFX_IsAvailable()) {
        std::cerr << "videofx: " << VideoFX_GetLastError() << "\n";
        return 1;
    }

    Options options;
    if (!ParseOptions(args, options)) return Usage();
    const VideoFXExportSettings& settings = options.settings;

    if (cmd == "info" && args.size() == 1) return Info(args[0]);
    if (cmd == "beats" && args.size() == 1) return Beats(args[0]);
    if (cmd == "faces" && args.size() == 1) return Faces(args[0]);
    if (cmd == "project" && args.size() == 1) return ShowProject(args[0]);
    if (cmd == "render" && (args.size() == 1 || args.size() == 2)) {
        VideoFXProject p;
        std::vector<std::string> missing;
        VideoFXResult r = VideoFX_LoadProject(args[0], p, &missing);
        if (r != VideoFXResult::Ok) return Report(r);
        for (const std::string& m : missing) std::cerr << "videofx: missing media: " << m << "\n";
        return Report(VideoFX_RenderProject(p, args.size() == 2 ? args[1] : std::string(), Progress));
    }

    if (cmd == "frame" && (args.size() == 3 || args.size() == 5)) {
        VideoFXFrame frame;
        const int maxW = args.size() == 5 ? static_cast<int>(NumberOr(args[3], 0)) : 0;
        const int maxH = args.size() == 5 ? static_cast<int>(NumberOr(args[4], 0)) : 0;
        VideoFXResult r = VideoFX_ExtractFrame(args[0], NumberOr(args[1], 0.0), frame, maxW, maxH);
        if (r == VideoFXResult::Ok) r = VideoFX_SaveFrameImage(frame, args[2]);
        return Report(r);
    }

    if (cmd == "transcode" && args.size() == 2) {
        VideoFXSegment s = VideoFXSegment::FromFile(args[0]);
        Decorate(s, options, true);
        return RunOrSave(VideoFXProject::FromTimeline({s}, settings), args[1], options);
    }

    if (cmd == "trim" && args.size() == 4) {
        const double start = NumberOr(args[2], -1.0), end = NumberOr(args[3], -1.0);
        if (options.lossless) return Report(VideoFX_TrimLossless(args[0], args[1], start, end, Progress));
        VideoFXSegment s = VideoFXSegment::FromFile(args[0], start, end);
        Decorate(s, options, true);
        return RunOrSave(VideoFXProject::FromTimeline({s}, settings), args[1], options);
    }

    if (cmd == "concat" && args.size() >= 3) {
        std::vector<VideoFXSegment> segments;
        for (size_t i = 1; i < args.size(); ++i) {
            segments.push_back(VideoFXSegment::FromFile(args[i]));
            Decorate(segments.back(), options, i == 1);
        }
        return RunOrSave(VideoFXProject::FromTimeline(segments, settings), args[0], options);
    }

    if (cmd == "effects" && args.size() >= 3) {
        VideoFXSegment s = VideoFXSegment::FromFile(args[0]);
        Decorate(s, options, true);
        for (size_t i = 2; i < args.size(); ++i) {
            VideoFXEffect e;
            if (!ParseEffect(args[i], e)) {
                std::cerr << "videofx: unknown effect '" << args[i] << "'\n";
                return 2;
            }
            s.effects.push_back(e);
        }
        return RunOrSave(VideoFXProject::FromTimeline({s}, settings), args[1], options);
    }

    if (cmd == "slideshow" && args.size() >= 2) {
        std::vector<std::string> images(args.begin() + 1, args.end());
        if (options.transitionGiven) options.slideshow.transition = options.transition;
        return RunOrSave(VideoFXProject::FromSlideshow(images, options.slideshow, settings), args[0], options);
    }

    if (cmd == "testclip" && (args.size() == 2 || args.size() == 5)) {
        const int w = args.size() == 5 ? static_cast<int>(NumberOr(args[2], 640)) : 640;
        const int h = args.size() == 5 ? static_cast<int>(NumberOr(args[3], 360)) : 360;
        const double fps = args.size() == 5 ? NumberOr(args[4], 25.0) : 25.0;
        return Report(VideoFX_GenerateTestClip(args[0], NumberOr(args[1], 5.0), w, h, fps));
    }
    return Usage();
}
