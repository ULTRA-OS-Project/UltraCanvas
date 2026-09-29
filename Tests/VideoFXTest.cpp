// Tests/VideoFXTest.cpp
// Tests for the VideoFX module.
//
// Part 1 (always): the effect -> filter text translation - number formatting
// under a comma-decimal locale, filter-value escaping of Windows / quoted
// paths, atempo splitting, rotation, fade placement, and value validation.
//
// Part 2 (when the module is built on FFmpeg): the engine end to end on clips
// it generates itself - probe, frame extraction and pixel checks, trim, speed,
// joining segments of different sizes, GIF / WAV / WebM-free outputs, the
// lossless cut, cancellation, the background job, a UTF-8 file name, and the
// error codes. No media file from the repository is needed.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFX/VideoFX.h"
#include "VideoFXFilterBuilder.h"
#include "VideoFXPlatform.h"

#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <locale>
#include <string>
#include <thread>

using namespace VideoFX;
using namespace VideoFX::Internal;

static int failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (cond) {                                                         \
            std::printf("  PASS  %s\n", msg);                               \
        } else {                                                            \
            std::printf("  FAIL  %s (line %d)\n", msg, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

#define CHECK_EQ(actual, expected, msg)                                     \
    do {                                                                    \
        const std::string a = (actual);                                     \
        const std::string e = (expected);                                   \
        if (a == e) {                                                       \
            std::printf("  PASS  %s\n", msg);                               \
        } else {                                                            \
            std::printf("  FAIL  %s (line %d): got \"%s\", want \"%s\"\n",  \
                        msg, __LINE__, a.c_str(), e.c_str());               \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

#define CHECK_OK(expr, msg)                                                 \
    do {                                                                    \
        const VideoFXResult r_ = (expr);                                    \
        if (r_ == VideoFXResult::Ok) {                                      \
            std::printf("  PASS  %s\n", msg);                               \
        } else {                                                            \
            std::printf("  FAIL  %s (line %d): %s - %s\n", msg, __LINE__,   \
                        VideoFX_ResultToString(r_),                         \
                        VideoFX_GetLastError().c_str());                    \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

// ============================================================================
// PART 1 - FILTER TEXT
// ============================================================================

static void TestFormatNumber() {
    std::printf("FormatNumber\n");
    CHECK_EQ(FormatNumber(0.25), "0.25", "fraction");
    CHECK_EQ(FormatNumber(2.0), "2", "integer has no trailing dot");
    CHECK_EQ(FormatNumber(-1.5), "-1.5", "negative");
    CHECK_EQ(FormatNumber(-1e-12), "0", "no negative zero");
    CHECK_EQ(FormatNumber(1.0 / 3.0), "0.333333", "six decimals");

    // A comma-decimal desktop must not leak into filter text: "0,25" would be
    // read by FFmpeg's option parser as two values.
    const char* commaLocales[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "nl_NL.UTF-8"};
    bool switched = false;
    for (const char* name : commaLocales) {
        if (std::setlocale(LC_ALL, name)) {
            try { std::locale::global(std::locale(name)); } catch (...) {}
            switched = true;
            break;
        }
    }
    CHECK_EQ(FormatNumber(0.25), "0.25", switched ? "dot-decimal under a comma locale"
                                                   : "dot-decimal (no comma locale installed)");
    std::string chain, error;
    BuildVideoEffectChain({VideoFXEffect::Brightness(0.5)}, 0.0, chain, error);
    CHECK_EQ(chain, "eq=brightness=0.5", "effect chain under the same locale");
    std::setlocale(LC_ALL, "C");
    std::locale::global(std::locale::classic());
}

static void TestEscaping() {
    std::printf("EscapeFilterValue\n");
    CHECK_EQ(EscapeFilterValue("look.cube"), "look.cube", "plain name unchanged");
    // ':' once for the option parser, backslash-escaped again for the graph parser
    CHECK_EQ(EscapeFilterValue("C:\\LUTs\\a.cube"), "C\\\\:\\\\\\\\LUTs\\\\\\\\a.cube", "Windows path");
    CHECK_EQ(EscapeFilterValue("it's[1],x;y"), "it\\\\\\'s\\[1\\]\\,x\\;y", "quote, brackets, comma, semicolon");
}

static void TestTempoAndRotation() {
    std::printf("AtempoChain / AutoRotateChain\n");
    CHECK_EQ(AtempoChain(1.0), "", "speed 1 needs nothing");
    CHECK_EQ(AtempoChain(1.5), "atempo=1.5", "within one stage");
    CHECK_EQ(AtempoChain(4.0), "atempo=2,atempo=2", "4x splits into two stages");
    CHECK_EQ(AtempoChain(0.25), "atempo=0.5,atempo=0.5", "quarter speed splits too");
    CHECK_EQ(AtempoChain(3.0), "atempo=2,atempo=1.5", "3x = 2 x 1.5");
    CHECK_EQ(AutoRotateChain(0), "", "upright");
    CHECK_EQ(AutoRotateChain(90), "transpose=clock", "90 clockwise");
    CHECK_EQ(AutoRotateChain(-90), "transpose=cclock", "-90 = 270");
    CHECK_EQ(AutoRotateChain(180), "hflip,vflip", "180");
    CHECK_EQ(AutoRotateChain(89), "transpose=clock", "matrix rounding snaps to quarter turns");
}

static void TestEffectChains() {
    std::printf("BuildVideoEffectChain / BuildAudioEffectChain\n");
    std::string chain, error;

    CHECK(BuildVideoEffectChain({VideoFXEffect::Grayscale(), VideoFXEffect::FlipHorizontal(),
                                 VideoFXEffect::Crop(10, 20, 300, 200)}, 10.0, chain, error),
          "chain of three builds");
    CHECK_EQ(chain, "hue=s=0,hflip,crop=300:200:10:20", "effects in order");

    CHECK(BuildVideoEffectChain({VideoFXEffect::FadeIn(1.0), VideoFXEffect::FadeOut(2.0)}, 10.0, chain, error),
          "fades build");
    CHECK_EQ(chain, "fade=t=in:st=0:d=1,fade=t=out:st=8:d=2", "fade out placed at the end");

    CHECK(BuildVideoEffectChain({VideoFXEffect::FadeOut(5.0)}, 3.0, chain, error), "long fade on short clip");
    CHECK_EQ(chain, "fade=t=out:st=0:d=3", "fade clamped to the clip");

    CHECK(BuildVideoEffectChain({VideoFXEffect::FadeOut(1.0)}, 0.0, chain, error), "fade with unknown length");
    CHECK_EQ(chain, "", "fade out skipped when the end is unknown");

    CHECK(BuildVideoEffectChain({VideoFXEffect::Volume(0.5)}, 5.0, chain, error), "audio effect in video chain");
    CHECK_EQ(chain, "", "audio-only effect adds no video filter");

    CHECK(BuildAudioEffectChain({VideoFXEffect::Volume(0.5), VideoFXEffect::FadeOut(1.0),
                                 VideoFXEffect::Grayscale()}, 4.0, chain, error), "audio chain builds");
    CHECK_EQ(chain, "volume=0.5,afade=t=out:st=3:d=1", "volume and fade, no video filters");

    CHECK(BuildVideoEffectChain({VideoFXEffect::LUT("a.cube", 0.5)}, 5.0, chain, error), "LUT with strength");
    CHECK(chain.find("lut3d=file=a.cube") != std::string::npos && chain.find("blend") != std::string::npos,
          "partial LUT = graded branch blended over the source");

    CHECK(!BuildVideoEffectChain({VideoFXEffect::Brightness(2.0)}, 5.0, chain, error), "brightness 2 refused");
    CHECK(!BuildVideoEffectChain({VideoFXEffect::Crop(0, 0, 0, 10)}, 5.0, chain, error), "empty crop refused");
    CHECK(!BuildVideoEffectChain({VideoFXEffect::Gamma(0.0)}, 5.0, chain, error), "gamma 0 refused");
    CHECK(!BuildVideoEffectChain({VideoFXEffect::LUT("", 1.0)}, 5.0, chain, error), "LUT without file refused");
    CHECK(!BuildAudioEffectChain({VideoFXEffect::NormalizeAudio(0.0)}, 5.0, chain, error), "0 LUFS refused");
}

static void TestTransitionAndOverlayText() {
    std::printf("Transitions / overlays (filter text)\n");
    CHECK_EQ(TransitionName(VideoFXTransitionType::Crossfade), "fade", "crossfade = xfade fade");
    CHECK_EQ(TransitionName(VideoFXTransitionType::FadeThroughBlack), "fadeblack", "through black");
    CHECK_EQ(TransitionName(VideoFXTransitionType::Cut), "", "a cut has no filter");
    CHECK(!TransitionName(VideoFXTransitionType::SqueezeVertical).empty(), "every type has a name");

    VideoFXOverlay o = VideoFXOverlay::Text("x");
    CHECK_EQ(OverlayEnableExpr(o, 5.0), "lt(t,5)", "whole segment: until its end");
    CHECK_EQ(OverlayEnableExpr(o, 0.0), "", "unknown length: always on");
    o.start = 1.0;
    o.end = 3.0;
    CHECK_EQ(OverlayEnableExpr(o, 5.0), "between(t,1,3)", "a window");
    o.end = 9.0;
    CHECK_EQ(OverlayEnableExpr(o, 5.0), "between(t,1,5)", "end clamped to the segment");
    o.end = 3.0;
    CHECK_EQ(OverlayAlphaExpr(o, 5.0), "1", "no fades: opaque");
    o.fadeIn = 0.5;
    o.fadeOut = 1.0;
    o.opacity = 0.5;
    CHECK_EQ(OverlayAlphaExpr(o, 5.0), "0.5*min(min(1,max(0,(t-1)/0.5)),min(1,max(0,(3-t)/1)))",
             "fade in, fade out, times opacity");

    VideoFXOverlay t = VideoFXOverlay::Text("a:b, 'c' 100%", VideoFXAnchor::Bottom, 0.1);
    const std::string dt = BuildTextOverlayFilter(t, 640, 360, 4.0, "");
    CHECK(dt.rfind("drawtext=expansion=none:text=", 0) == 0, "drawtext, literal text");
    CHECK(dt.find("a\\\\:b\\, \\\\\\'c\\\\\\' 100%") != std::string::npos, "':' ',' and quotes escaped, '%' literal");
    CHECK(dt.find(":font=Sans") != std::string::npos, "no font file: fontconfig Sans");
    CHECK(dt.find(":fontsize=36") != std::string::npos, "size 0.1 of 360 px");
    CHECK(dt.find(":x=(w-text_w)/2:y=h-text_h-18") != std::string::npos, "bottom centre, 5% margin");
    CHECK(dt.find("fontfile=") == std::string::npos, "no fontfile without a path");
    VideoFXOverlay own = t;
    own.fontPath = "/fonts/own.ttf";
    CHECK(BuildTextOverlayFilter(own, 640, 360, 4.0, "/fonts/default.ttf").find(":fontfile=/fonts/own.ttf") !=
              std::string::npos, "the overlay's own font wins over the default");
    CHECK(BuildTextOverlayFilter(t, 640, 360, 4.0, "/fonts/default.ttf").find(":fontfile=/fonts/default.ttf") !=
              std::string::npos, "no font of its own: the export's default");
    CHECK(BuildTextOverlayFilter(t, 640, 360, 4.0, "C:/Fonts/a.ttf").find("fontfile=C\\\\:/Fonts/a.ttf") !=
              std::string::npos, "font path escaped");

    VideoFXOverlay img = VideoFXOverlay::Image("logo.png", VideoFXAnchor::TopRight, 0.25);
    std::string chain, ov;
    BuildImageOverlayFilters(img, 640, 360, 50, "25/1", 4.0, chain, ov);
    CHECK_EQ(chain, "format=rgba,scale=-1:90", "image scaled to 25% of the height");
    CHECK(ov.find("overlay=x=W-w-18:y=18") == 0 && ov.find("eof_action=repeat") != std::string::npos,
          "still image top right, held for the segment");
    img.fadeIn = 1.0;
    BuildImageOverlayFilters(img, 640, 360, 50, "25/1", 4.0, chain, ov);
    CHECK(chain.find("loop=loop=-1:size=1,setpts=N/(25/1)/TB,fade=t=in:st=0:d=1:alpha=1") != std::string::npos,
          "faded image becomes a timed stream");
    CHECK(ov.find("shortest=1") != std::string::npos, "and ends with the picture below it");

    std::string error;
    CHECK(!ValidateOverlay(VideoFXOverlay::Text(""), 5.0, error), "empty text refused");
    VideoFXOverlay bad = VideoFXOverlay::Text("x");
    bad.opacity = 2.0;
    CHECK(!ValidateOverlay(bad, 5.0, error), "opacity 2 refused");
    bad = VideoFXOverlay::Text("x");
    bad.start = 3.0;
    bad.end = 2.0;
    CHECK(!ValidateOverlay(bad, 5.0, error), "end before start refused");
    CHECK(!ValidateOverlay(VideoFXOverlay::Image(""), 5.0, error), "image without a picture refused");
}

// ============================================================================
// PART 2 - ENGINE
// ============================================================================

#ifdef VIDEOFX_HAS_FFMPEG

namespace fs = std::filesystem;

static bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

static std::string TempPath(const std::string& name) {
    static const fs::path dir = [] {
        fs::path d = fs::temp_directory_path() / ("videofx-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(d);
        return d;
    }();
    return UltraCanvas::PathToUtf8(dir / UltraCanvas::PathFromUtf8(name));
}

static bool Exists(const std::string& p) {
    std::error_code ec;
    return fs::exists(UltraCanvas::PathFromUtf8(p), ec);
}

// Average of a frame's centre 8x8 block
static void CentreColour(const VideoFXFrame& f, double& r, double& g, double& b) {
    r = g = b = 0.0;
    int n = 0;
    for (int y = f.height / 2 - 4; y < f.height / 2 + 4; ++y)
        for (int x = f.width / 2 - 4; x < f.width / 2 + 4; ++x) {
            const uint8_t* p = &f.pixels[(static_cast<size_t>(y) * f.width + x) * 4];
            r += p[0]; g += p[1]; b += p[2];
            ++n;
        }
    r /= n; g /= n; b /= n;
}

static void PixelAt(const VideoFXFrame& f, int x, int y, int& r, int& g, int& b) {
    const uint8_t* p = &f.pixels[(static_cast<size_t>(y) * f.width + x) * 4];
    r = p[0]; g = p[1]; b = p[2];
}

static double StreamDuration(const VideoFXMediaInfo& info, VideoFXStreamKind kind) {
    for (const auto& s : info.streams) if (s.kind == kind) return s.duration;
    return -1.0;
}

static void TestTransitionsAndOverlays(const VideoFXExportSettings& base);

static void TestEngine() {
    std::printf("Engine: %s\n", VideoFX_GetBackendVersion().c_str());
    CHECK(VideoFX_IsAvailable(), "engine available");
    CHECK(VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::MPEG4), "MPEG-4 encoder (always in FFmpeg)");
    CHECK(VideoFX_IsAudioEncoderAvailable(VideoFXAudioCodec::PCM16), "PCM encoder");

    // Clips are MPEG-4 in MKV + AAC so the test does not depend on libx264
    VideoFXExportSettings base;
    base.container = VideoFXContainer::MKV;
    base.videoCodec = VideoFXVideoCodec::MPEG4;
    base.audioCodec = VideoFXAudioCodec::AAC;

    // ---- a 3 s 320x240 test pattern with tone ----
    const std::string clip = TempPath("pattern.mkv");
    {
        VideoFXExportSettings s = base;
        s.width = 320; s.height = 240; s.frameRate = 25.0;
        CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(3.0)}, clip, s), "generate test pattern");
    }
    VideoFXMediaInfo info;
    CHECK_OK(VideoFX_Probe(clip, info), "probe");
    CHECK(info.HasVideo() && info.HasAudio(), "video and audio streams");
    CHECK(info.width == 320 && info.height == 240, "320x240");
    CHECK(Near(info.frameRate, 25.0, 0.01), "25 fps");
    CHECK(Near(info.duration, 3.0, 0.1), "3 s long");
    CHECK(info.channels == 1, "tone is mono");

    // ---- frames ----
    VideoFXFrame frame;
    CHECK_OK(VideoFX_ExtractFrame(clip, 1.5, frame), "extract frame");
    CHECK(frame.IsValid() && frame.width == 320 && frame.height == 240, "frame is 320x240 RGBA");
    CHECK(Near(frame.timestamp, 1.5, 0.05), "frame is the one at 1.5 s");
    CHECK_OK(VideoFX_ExtractFrame(clip, 1.5, frame, 100, 100), "extract scaled frame");
    CHECK(frame.width == 100 && frame.height == 75, "scaled keeping 4:3");

    std::vector<VideoFXFrame> strip;
    CHECK_OK(VideoFX_ExtractThumbnails(clip, 4, strip, 64, 48), "thumbnails");
    CHECK(strip.size() == 4 && strip[0].timestamp < strip[3].timestamp, "four, in time order");

    const std::string png = TempPath("frame.png");
    CHECK_OK(VideoFX_SaveFrameImage(strip[0], png), "save PNG");
    CHECK(Exists(png) && fs::file_size(UltraCanvas::PathFromUtf8(png)) > 100, "PNG written");
    CHECK(VideoFX_SaveFrameImage(strip[0], TempPath("frame.bmp")) == VideoFXResult::InvalidArgument,
          "unknown image extension refused");

    // ---- colour segment: the picture really is the colour ----
    {
        const std::string red = TempPath("red.mkv");
        VideoFXExportSettings s = base;
        s.width = 160; s.height = 120;
        CHECK_OK(VideoFX_Export({VideoFXSegment::SolidColor(0xFF0000, 1.0)}, red, s), "solid red clip");
        VideoFXFrame f;
        CHECK_OK(VideoFX_ExtractFrame(red, 0.5, f), "frame of red clip");
        double r, g, b;
        CentreColour(f, r, g, b);
        CHECK(r > 200 && g < 60 && b < 60, "centre pixel is red");

        // Effects act on the picture: grayscale red has no colour left
        VideoFXSegment grey = VideoFXSegment::FromFile(red);
        grey.effects = {VideoFXEffect::Grayscale()};
        const std::string greyPath = TempPath("grey.mkv");
        CHECK_OK(VideoFX_Export({grey}, greyPath, base), "grayscale effect");
        CHECK_OK(VideoFX_ExtractFrame(greyPath, 0.5, f), "frame of grayscale clip");
        CentreColour(f, r, g, b);
        CHECK(Near(r, g, 12) && Near(g, b, 12), "grayscale: r = g = b");

        // FadeIn starts from black
        VideoFXSegment fade = VideoFXSegment::FromFile(red);
        fade.effects = {VideoFXEffect::FadeIn(0.8)};
        const std::string fadePath = TempPath("fade.mkv");
        CHECK_OK(VideoFX_Export({fade}, fadePath, base), "fade-in effect");
        CHECK_OK(VideoFX_ExtractFrame(fadePath, 0.0, f), "first frame of fade");
        CentreColour(f, r, g, b);
        CHECK(r < 40, "fade-in begins black");
    }

    // ---- trim ----
    {
        const std::string out = TempPath("trim.mkv");
        CHECK_OK(VideoFX_Trim(clip, out, 0.5, 2.0, base), "trim 0.5..2.0");
        VideoFXMediaInfo t;
        CHECK_OK(VideoFX_Probe(out, t), "probe trimmed");
        CHECK(Near(t.duration, 1.5, 0.1), "trimmed to 1.5 s");
        VideoFXFrame f;
        CHECK_OK(VideoFX_ExtractFrame(out, 0.0, f), "first frame of trim");
        CHECK(f.IsValid(), "trimmed clip decodes");
    }

    // ---- speed ----
    {
        VideoFXSegment fast = VideoFXSegment::FromFile(clip);
        fast.speed = 2.0;
        const std::string out = TempPath("fast.mkv");
        CHECK_OK(VideoFX_Export({fast}, out, base), "double speed");
        VideoFXMediaInfo t;
        CHECK_OK(VideoFX_Probe(out, t), "probe fast");
        CHECK(Near(t.duration, 1.5, 0.1), "twice as fast = half as long");
    }

    // ---- joining different shapes, with a silent colour card between ----
    {
        const std::string wide = TempPath("wide.mkv");
        VideoFXExportSettings s = base;
        s.width = 640; s.height = 360;
        CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(2.0)}, wide, s), "generate 16:9 clip");

        const std::string out = TempPath("joined.mkv");
        CHECK_OK(VideoFX_Export({VideoFXSegment::FromFile(clip), VideoFXSegment::SolidColor(0x0000FF, 1.0),
                                 VideoFXSegment::FromFile(wide)}, out, base),
                 "join 4:3 + colour card + 16:9");
        VideoFXMediaInfo j;
        CHECK_OK(VideoFX_Probe(out, j), "probe joined");
        CHECK(j.width == 320 && j.height == 240, "output takes the first segment's size");
        CHECK(Near(j.duration, 6.0, 0.15), "3 + 1 + 2 = 6 s");
        const double vd = StreamDuration(j, VideoFXStreamKind::Video);
        const double ad = StreamDuration(j, VideoFXStreamKind::Audio);
        CHECK(vd <= 0.0 || ad <= 0.0 || Near(vd, ad, 0.1), "audio and video end together");

        VideoFXFrame f;
        CHECK_OK(VideoFX_ExtractFrame(out, 3.5, f), "frame inside the colour card");
        double r, g, b;
        CentreColour(f, r, g, b);
        CHECK(b > 200 && r < 60, "colour card is blue");
        CHECK_OK(VideoFX_ExtractFrame(out, 5.0, f), "frame of the letterboxed 16:9 part");
        const uint8_t* top = &f.pixels[(static_cast<size_t>(2) * f.width + f.width / 2) * 4];
        CHECK(top[0] < 30 && top[1] < 30 && top[2] < 30, "16:9 in 4:3 has a black bar on top");
    }

    // ---- other outputs ----
    {
        const std::string gif = TempPath("anim.gif");
        CHECK_OK(VideoFX_Export({VideoFXSegment::FromFile(clip, 0.0, 1.0)}, gif,
                                VideoFXExportSettings::AnimatedGif(160, 10.0)), "animated GIF");
        VideoFXMediaInfo g;
        CHECK_OK(VideoFX_Probe(gif, g), "probe GIF");
        CHECK(g.width == 160 && g.height == 120 && !g.HasAudio(), "GIF 160x120, no sound");

        const std::string wav = TempPath("sound.wav");
        CHECK_OK(VideoFX_ExtractAudio(clip, wav), "extract audio to WAV");
        VideoFXMediaInfo w;
        CHECK_OK(VideoFX_Probe(wav, w), "probe WAV");
        CHECK(!w.HasVideo() && w.HasAudio() && Near(w.duration, 3.0, 0.1), "WAV: sound only, 3 s");
    }

    // ---- lossless cut ----
    {
        const std::string out = TempPath("copy.mkv");
        CHECK_OK(VideoFX_TrimLossless(clip, out, 1.0, 2.0), "lossless cut");
        VideoFXMediaInfo c;
        CHECK_OK(VideoFX_Probe(out, c), "probe lossless cut");
        CHECK(c.HasVideo() && c.duration >= 0.9 && c.duration <= 2.2, "cut starts at a keyframe <= 1 s, ends at 2 s");
        CHECK(c.streams.size() == info.streams.size() && c.streams[0].codecName == info.streams[0].codecName,
              "streams copied, not re-encoded");
    }

    // ---- cancellation removes the partial file ----
    {
        const std::string out = TempPath("cancelled.mkv");
        int calls = 0;
        const VideoFXResult r = VideoFX_Export({VideoFXSegment::TestPattern(30.0)}, out, base,
                                               [&](double) { return ++calls < 2; });
        CHECK(r == VideoFXResult::Cancelled, "progress returning false cancels");
        CHECK(!Exists(out), "cancelled output is removed");
    }

    // ---- background job ----
    {
        VideoFXExportJob job;
        const std::string out = TempPath("job.mkv");
        CHECK(job.Start({VideoFXSegment::TestPattern(1.0)}, out, base), "job starts");
        CHECK(job.Wait() == VideoFXResult::Ok, "job finishes");
        CHECK(!job.IsRunning() && job.GetProgress() >= 1.0 && Exists(out), "job output written, progress 1");

        VideoFXExportJob slow;
        CHECK(slow.Start({VideoFXSegment::TestPattern(60.0)}, TempPath("job2.mkv"), base), "second job starts");
        slow.Cancel();
        CHECK(slow.Wait() == VideoFXResult::Cancelled, "job cancels");
    }

    // ---- UTF-8 file names ----
    {
        const std::string out = TempPath("วิดีโอ 🎬 clip.mkv");
        CHECK_OK(VideoFX_Trim(clip, out, 0.0, 1.0, base), "write to a Thai + emoji file name");
        VideoFXMediaInfo u;
        CHECK_OK(VideoFX_Probe(out, u), "read it back");
    }

    // ---- errors ----
    {
        CHECK(VideoFX_Probe(TempPath("missing.mp4"), info) == VideoFXResult::FileNotFound, "missing file");
        CHECK(!VideoFX_GetLastError().empty(), "missing file has a reason");
        CHECK(VideoFX_Export({}, TempPath("x.mkv")) == VideoFXResult::InvalidArgument, "empty timeline");
        CHECK(VideoFX_Trim(clip, TempPath("x.mkv"), 2.0, 1.0, base) == VideoFXResult::InvalidArgument, "end before start");
        CHECK(VideoFX_Transcode(clip, clip, base) == VideoFXResult::InvalidArgument, "output = input refused");
        CHECK(Exists(clip), "input survives the refused overwrite");
        CHECK(VideoFX_Transcode(clip, TempPath("x.unknownext")) == VideoFXResult::InvalidArgument, "unknown extension");
        VideoFXSegment bad = VideoFXSegment::FromFile(clip);
        bad.effects = {VideoFXEffect::Contrast(5.0)};
        CHECK(VideoFX_Export({bad}, TempPath("x.mkv"), base) == VideoFXResult::InvalidArgument, "bad effect value");
        bad.effects = {VideoFXEffect::LUT(TempPath("none.cube"))};
        CHECK(VideoFX_Export({bad}, TempPath("x.mkv"), base) == VideoFXResult::FileNotFound, "missing LUT file");
        CHECK(!Exists(TempPath("x.mkv")), "no output left behind by a refused export");
        CHECK(VideoFX_GetLastError().find("LUT") != std::string::npos, "reason names the LUT");
    }

    TestTransitionsAndOverlays(base);

    std::error_code ec;
    fs::remove_all(UltraCanvas::PathFromUtf8(TempPath("")), ec);
}

static void TestTransitionsAndOverlays(const VideoFXExportSettings& base) {
    std::printf("Engine: transitions and overlays\n");
    VideoFXExportSettings s = base;
    s.width = 160;
    s.height = 120;
    s.frameRate = 25.0;
    VideoFXFrame f;
    VideoFXMediaInfo info;
    double r, g, b;
    int pr, pg, pb;

    // ---- red -> blue, 2 s each, 1 s cross-fade ----
    VideoFXSegment red = VideoFXSegment::SolidColor(0xFF0000, 2.0);
    VideoFXSegment blue = VideoFXSegment::SolidColor(0x0000FF, 2.0);
    blue.transitionIn = VideoFXTransition::Crossfade(1.0);
    const std::string xf = TempPath("xfade.mkv");
    CHECK_OK(VideoFX_Export({red, blue}, xf, s), "cross-fade red -> blue");
    CHECK_OK(VideoFX_Probe(xf, info), "probe cross-fade");
    CHECK(Near(info.duration, 3.0, 0.1), "2 + 2 - 1 s overlap = 3 s");
    VideoFX_ExtractFrame(xf, 0.5, f);
    CentreColour(f, r, g, b);
    CHECK(r > 200 && b < 60, "before the transition: red");
    VideoFX_ExtractFrame(xf, 1.5, f);
    CentreColour(f, r, g, b);
    CHECK(r > 70 && r < 190 && b > 70 && b < 190, "half-way: red and blue mixed");
    VideoFX_ExtractFrame(xf, 2.5, f);
    CentreColour(f, r, g, b);
    CHECK(b > 200 && r < 60, "after the transition: blue");

    // ---- sound is cross-faded over the same span ----
    VideoFXSegment t1 = VideoFXSegment::TestPattern(2.0);
    VideoFXSegment t2 = VideoFXSegment::TestPattern(2.0);
    t2.transitionIn = VideoFXTransition::Make(VideoFXTransitionType::Dissolve, 1.0);
    // MP4 records per-stream durations (MKV does not)
    const std::string av = TempPath("xfade-av.mp4");
    VideoFXExportSettings mp4 = s;
    mp4.container = VideoFXContainer::MP4;
    CHECK_OK(VideoFX_Export({t1, t2}, av, mp4), "dissolve with sound");
    CHECK_OK(VideoFX_Probe(av, info), "probe dissolve");
    CHECK(Near(StreamDuration(info, VideoFXStreamKind::Video), 3.0, 0.1) &&
          Near(StreamDuration(info, VideoFXStreamKind::Audio), 3.0, 0.1), "picture and sound both 3 s");
    const std::string wav = TempPath("xfade.wav");
    CHECK_OK(VideoFX_Export({t1, t2}, wav, VideoFXExportSettings::AudioOnlyWAV()), "cross-fade, sound only");
    CHECK_OK(VideoFX_Probe(wav, info), "probe WAV cross-fade");
    CHECK(Near(info.duration, 3.0, 0.06), "sound-only overlap is not lost");

    // ---- a wipe: two halves ----
    blue.transitionIn = VideoFXTransition::Make(VideoFXTransitionType::WipeLeft, 1.0);
    const std::string wipe = TempPath("wipe.mkv");
    CHECK_OK(VideoFX_Export({red, blue}, wipe, s), "wipe left");
    VideoFX_ExtractFrame(wipe, 1.5, f);
    PixelAt(f, 5, f.height / 2, pr, pg, pb);
    const bool leftRed = pr > 200 && pb < 60;
    PixelAt(f, f.width - 5, f.height / 2, pr, pg, pb);
    const bool rightBlue = pb > 200 && pr < 60;
    CHECK(leftRed != rightBlue || (leftRed && rightBlue), "mid-wipe the two edges differ");
    CHECK(leftRed || rightBlue, "one edge still shows a pure colour");

    // ---- transition longer than the next clip: the overlap shrinks ----
    VideoFXSegment brief = VideoFXSegment::SolidColor(0x00FF00, 0.5);
    brief.transitionIn = VideoFXTransition::Crossfade(2.0);
    const std::string shortOut = TempPath("short.mkv");
    CHECK_OK(VideoFX_Export({red, brief}, shortOut, s), "2 s fade into a 0.5 s clip");
    CHECK_OK(VideoFX_Probe(shortOut, info), "probe short");
    CHECK(Near(info.duration, 2.0, 0.1), "overlap limited to the shorter clip");

    // ---- a transition on the first segment is ignored ----
    VideoFXSegment first = red;
    first.transitionIn = VideoFXTransition::Crossfade(1.0);
    const std::string firstOut = TempPath("first.mkv");
    CHECK_OK(VideoFX_Export({first}, firstOut, s), "first segment with a transition");
    CHECK_OK(VideoFX_Probe(firstOut, info), "probe first");
    CHECK(Near(info.duration, 2.0, 0.1), "nothing to overlap with");

    // ---- GIF: transitions before the palette ----
    blue.transitionIn = VideoFXTransition::Crossfade(1.0);
    const std::string gif = TempPath("xfade.gif");
    CHECK_OK(VideoFX_Export({red, blue}, gif, VideoFXExportSettings::AnimatedGif(80, 10.0)), "GIF with a cross-fade");
    CHECK_OK(VideoFX_Probe(gif, info), "probe GIF");
    CHECK(Near(info.duration, 3.0, 0.15), "GIF 3 s");
    VideoFX_ExtractFrame(gif, 1.5, f);
    CentreColour(f, r, g, b);
    CHECK(r > 60 && b > 60, "GIF mid-fade has both colours");

    // ---- bad transitions ----
    blue.transitionIn = VideoFXTransition::Crossfade(9.0);
    CHECK(VideoFX_Export({red, blue}, TempPath("x.mkv"), s) == VideoFXResult::InvalidArgument, "9 s transition refused");
    blue.transitionIn = VideoFXTransition{};

    // ---- an image overlay from memory, from 1 s on ----
    VideoFXFrame green;
    green.width = 40;
    green.height = 40;
    green.pixels.assign(40 * 40 * 4, 0);
    for (size_t i = 0; i < green.pixels.size(); i += 4) { green.pixels[i + 1] = 255; green.pixels[i + 3] = 255; }
    VideoFXSegment card = VideoFXSegment::SolidColor(0x0000FF, 2.0);
    VideoFXOverlay logo = VideoFXOverlay::ImageFromFrame(green, VideoFXAnchor::TopLeft, 0.0);
    logo.margin = 0.0;
    logo.start = 1.0;
    card.overlays = {logo};
    const std::string imgOut = TempPath("image.mkv");
    CHECK_OK(VideoFX_Export({card}, imgOut, s), "image overlay from memory");
    VideoFX_ExtractFrame(imgOut, 0.5, f);
    PixelAt(f, 10, 10, pr, pg, pb);
    CHECK(pb > 200 && pg < 60, "before its start: no image");
    VideoFX_ExtractFrame(imgOut, 1.5, f);
    PixelAt(f, 10, 10, pr, pg, pb);
    CHECK(pg > 200 && pb < 60, "after: the image, top left, native size");
    PixelAt(f, 100, 100, pr, pg, pb);
    CHECK(pb > 200 && pg < 60, "the rest of the frame untouched");

    // ---- faded, half-transparent image from a PNG file, bottom right ----
    const std::string png = TempPath("green.png");
    CHECK_OK(VideoFX_SaveFrameImage(green, png), "write the overlay PNG");
    VideoFXOverlay faded = VideoFXOverlay::Image(png, VideoFXAnchor::BottomRight, 0.25);
    faded.margin = 0.0;
    faded.fadeIn = 0.5;
    faded.opacity = 0.5;
    card.overlays = {faded};
    const std::string fadeOut = TempPath("image-fade.mkv");
    CHECK_OK(VideoFX_Export({card}, fadeOut, s), "faded image overlay from a file");
    CHECK_OK(VideoFX_Probe(fadeOut, info), "probe faded overlay");
    CHECK(Near(info.duration, 2.0, 0.1), "a looping overlay still ends with its segment");
    VideoFX_ExtractFrame(fadeOut, 1.5, f);
    PixelAt(f, f.width - 5, f.height - 5, pr, pg, pb);
    CHECK(pg > 70 && pb > 70, "half-transparent: green over blue");
    VideoFX_ExtractFrame(fadeOut, 0.0, f);
    PixelAt(f, f.width - 5, f.height - 5, pr, pg, pb);
    CHECK(pg < 40, "fully faded out at the start");

    // ---- text ----
    if (VideoFX_IsTextOverlayAvailable()) {
        VideoFXOverlay title = VideoFXOverlay::Text("VFX", VideoFXAnchor::Center, 0.5);
        title.start = 1.0;
        title.shadow = false;
        card.overlays = {title, VideoFXOverlay::Text("a:b, 'c' [d]; 100% \\ done", VideoFXAnchor::Top, 0.08)};
        const std::string textOut = TempPath("text.mkv");
        CHECK_OK(VideoFX_Export({card}, textOut, s), "text overlays (with every special character)");
        auto whitePixels = [](const VideoFXFrame& fr) {
            int n = 0;
            for (size_t i = 0; i < fr.pixels.size(); i += 4)
                if (fr.pixels[i] > 200 && fr.pixels[i + 1] > 200 && fr.pixels[i + 2] > 200) ++n;
            return n;
        };
        VideoFX_ExtractFrame(textOut, 0.5, f);
        const int before = whitePixels(f);
        VideoFX_ExtractFrame(textOut, 1.5, f);
        const int after = whitePixels(f);
        CHECK(after > before + 200, "the big title appears at its start time");
        // ---- default font: bundled first, application override, reset ----
        const std::string automatic = VideoFX_GetDefaultFontPath();
        CHECK(automatic.empty() || Exists(automatic), "the automatic font exists");
        const std::string exeDir = VideoFX::Internal::ExecutableDir();
        CHECK(!exeDir.empty(), "executable directory found");
        const fs::path bundled = UltraCanvas::PathFromUtf8(exeDir) / ".." / "share" / "media" / "fonts" / "Ubuntu-R.ttf";
        if (fs::exists(bundled)) {
            CHECK(automatic.find("Ubuntu-R.ttf") != std::string::npos, "the framework's bundled font comes first");
        } else {
            std::printf("  SKIP  bundled-font preference (no share/media next to the test)\n");
        }
        CHECK(!VideoFX_SetDefaultFontPath(TempPath("none.ttf")), "a missing default font is refused");
        CHECK_EQ(VideoFX_GetDefaultFontPath(), automatic, "and changes nothing");
        const fs::path repoFont = UltraCanvas::PathFromUtf8(__FILE__).parent_path() / ".." / "media" / "fonts" / "Ubuntu-B.ttf";
        if (fs::exists(repoFont)) {
            const std::string chosen = UltraCanvas::PathToUtf8(repoFont);
            CHECK(VideoFX_SetDefaultFontPath(chosen), "the application picks its font");
            CHECK_EQ(VideoFX_GetDefaultFontPath(), chosen, "and it is the default now");
            card.overlays = {VideoFXOverlay::Text("Bold", VideoFXAnchor::Center, 0.3)};
            CHECK_OK(VideoFX_Export({card}, TempPath("bold.mkv"), s), "text in the application's font");
            CHECK(VideoFX_SetDefaultFontPath(""), "reset to automatic");
            CHECK_EQ(VideoFX_GetDefaultFontPath(), automatic, "automatic again");
        }

        VideoFXOverlay missingFont = VideoFXOverlay::Text("x");
        missingFont.fontPath = TempPath("none.ttf");
        card.overlays = {missingFont};
        CHECK(VideoFX_Export({card}, TempPath("x.mkv"), s) == VideoFXResult::FileNotFound, "missing font file");
    } else {
        std::printf("  SKIP  text overlays (this FFmpeg has no drawtext)\n");
    }

    card.overlays = {VideoFXOverlay::Image(TempPath("none.png"))};
    CHECK(VideoFX_Export({card}, TempPath("x.mkv"), s) == VideoFXResult::FileNotFound, "missing overlay image");
    card.overlays = {VideoFXOverlay::Text("")};
    CHECK(VideoFX_Export({card}, TempPath("x.mkv"), s) == VideoFXResult::InvalidArgument, "empty text");
}

#else

static void TestEngine() {
    std::printf("Engine: not built (no FFmpeg)\n");
    VideoFXMediaInfo info;
    CHECK(!VideoFX_IsAvailable(), "stub says unavailable");
    CHECK(VideoFX_Probe("x.mp4", info) == VideoFXResult::NotAvailable, "stub returns NotAvailable");
}

#endif

int main() {
    std::printf("VideoFX %s\n", VideoFX_GetVersion().c_str());
    TestFormatNumber();
    TestEscaping();
    TestTempoAndRotation();
    TestEffectChains();
    TestTransitionAndOverlayText();
    TestEngine();
    if (failures) {
        std::printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nAll VideoFX checks passed\n");
    return 0;
}
