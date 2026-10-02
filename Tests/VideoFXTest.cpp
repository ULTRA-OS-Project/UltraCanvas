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
// Version: 0.4.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "VideoFX/VideoFX.h"
#include "VideoFXFilterBuilder.h"
#include "VideoFXKenBurns.h"
#include "VideoFXMusic.h"
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

static bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

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

static VideoFXFrame SolidFrame(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    VideoFXFrame f;
    f.width = w;
    f.height = h;
    f.pixels.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < f.pixels.size(); i += 4) {
        f.pixels[i] = r; f.pixels[i + 1] = g; f.pixels[i + 2] = b; f.pixels[i + 3] = a;
    }
    return f;
}

static void TestKenBurnsMath() {
    std::printf("Still images: camera maths and renderer\n");
    std::string error;
    CHECK(ValidateMotion(VideoFXImageMotion{}, error), "Auto is valid");
    CHECK(!ValidateMotion(VideoFXImageMotion::Custom(9.0, 0.5, 0.5, 1.0, 0.5, 0.5), error), "zoom 9 refused");
    CHECK(!ValidateMotion(VideoFXImageMotion::Custom(1.0, 1.5, 0.5, 1.0, 0.5, 0.5), error), "centre 1.5 refused");

    // Auto: pans along the direction the frame crops, different per segment
    const VideoFXImageMotion autoMotion;
    const VideoFXImageMotion a0 = ResolveMotion(autoMotion, 6000, 2000, 1920, 1080, 0);
    const VideoFXImageMotion a1 = ResolveMotion(autoMotion, 6000, 2000, 1920, 1080, 1);
    CHECK(a0.style == VideoFXMotionStyle::Custom && a0.endZoom > a0.startZoom, "Auto #0: zoom in");
    CHECK(a1.startX < a1.endX && a1.startY == a1.endY, "Auto #1: pan sideways along a panorama");
    const VideoFXImageMotion c0 = ResolveMotion(autoMotion, 4000, 3000, 1920, 1080, 0);
    CHECK(c0.startY < c0.endY, "a 4:3 photo in 16:9 is cropped top and bottom: pan down it");
    const VideoFXImageMotion p0 = ResolveMotion(autoMotion, 3000, 4000, 1920, 1080, 0);
    CHECK(p0.startY < p0.endY && p0.startX == p0.endX, "Auto #0: pan down a portrait photo");
    CHECK(ResolveMotion(VideoFXImageMotion::Make(VideoFXMotionStyle::Still), 10, 10, 10, 10, 0).style ==
              VideoFXMotionStyle::Still, "Still stays still");

    VideoFXImageMotion m = VideoFXImageMotion::Custom(1.0, 0.2, 0.5, 4.0, 0.8, 0.5);
    KenBurnsView v = ViewAt(m, 0.0);
    CHECK(Near(v.zoom, 1.0, 1e-9) && Near(v.centerX, 0.2, 1e-9), "start of the move");
    v = ViewAt(m, 1.0);
    CHECK(Near(v.zoom, 4.0, 1e-9) && Near(v.centerX, 0.8, 1e-9), "end of the move");
    v = ViewAt(m, 0.5);
    CHECK(Near(v.zoom, 2.0, 1e-9), "half-way zoom is geometric (1 -> 4 passes 2)");
    CHECK(ViewAt(m, 0.1).zoom < std::exp(0.1 * std::log(4.0)), "eased: slow at the start");

    double x, y, w, h;
    ViewRect(KenBurnsView{}, 4000, 3000, 1920, 1080, x, y, w, h);
    CHECK(Near(w, 4000, 1e-6) && Near(w / h, 1920.0 / 1080.0, 1e-9), "zoom 1: full width, frame-shaped");
    CHECK(y >= 0 && y + h <= 3000 + 1e-9, "inside the image");
    ViewRect(KenBurnsView{2.0, 0.0, 0.0}, 4000, 3000, 1920, 1080, x, y, w, h);
    CHECK(Near(w, 2000, 1e-6) && Near(x, 0, 1e-9) && Near(y, 0, 1e-9), "zoom 2 in the corner: half size, clamped");
    ContainViewRect(KenBurnsView{}, 1000, 1000, 1920, 1080, x, y, w, h);
    CHECK(x < 0 && Near(y, 0, 1e-9) && Near(h, 1000, 1e-9), "contain: whole square image, bars at the sides");
    ContainViewRect(KenBurnsView{2.0, 0.5, 0.0}, 1000, 1000, 1920, 1080, x, y, w, h);
    CHECK(Near(x, (1000 - w) / 2, 1e-9) && Near(y, 0, 1e-9), "zoomed contain: centred across, follows the view down");
    CHECK(ResolveImageFit(VideoFXImageFit::Auto, 3000, 4000, 1920, 1080) == VideoFXImageFit::BlurredBackground,
          "Auto: a portrait photo in 16:9 is shown whole, on a blurred background");
    CHECK(ResolveImageFit(VideoFXImageFit::Auto, 4000, 3000, 1920, 1080) == VideoFXImageFit::Cover,
          "Auto: a 4:3 photo still fills the frame");
    CHECK(ResolveImageFit(VideoFXImageFit::Auto, 6000, 2000, 1920, 1080) == VideoFXImageFit::Cover,
          "Auto: a panorama fills it too (and pans)");
    CHECK(ResolveImageFit(VideoFXImageFit::Contain, 3000, 4000, 1920, 1080) == VideoFXImageFit::Contain,
          "an explicit fit is kept");

    // Renderer: colour kept, black outside the image, transparency on black
    const VideoFXFrame red = SolidFrame(8, 8, 255, 0, 0);
    std::vector<uint8_t> out(16 * 9 * 4);
    RenderView(red, 0, 0, 8, 4.5, 16, 9, out.data(), 16 * 4, 1);
    CHECK(out[0] == 255 && out[1] == 0 && out[3] == 255, "solid image renders solid");
    ContainViewRect(KenBurnsView{}, 8, 8, 16, 9, x, y, w, h);
    RenderView(red, x, y, w, h, 16, 9, out.data(), 16 * 4, 1);
    CHECK(out[0] == 0 && out[3] == 255, "letterbox bar is black");
    CHECK(out[(4 * 16 + 8) * 4] == 255, "centre is the image");
    std::vector<uint8_t> backdrop(16 * 9 * 4, 0);
    for (size_t i = 1; i < backdrop.size(); i += 4) backdrop[i] = 200;          // green
    RenderView(red, x, y, w, h, 16, 9, out.data(), 16 * 4, 1, backdrop.data());
    CHECK(out[0] == 0 && out[1] == 200, "with a background, the bar shows it");
    CHECK(out[(4 * 16 + 8) * 4] == 255 && out[(4 * 16 + 8) * 4 + 1] == 0, "and the image stays in front");
    MakeBlurredBackdrop(red, 64, 36, backdrop);
    CHECK(backdrop.size() == 64u * 36 * 4 && backdrop[0] > 100 && backdrop[0] < 200 && backdrop[1] < 20,
          "blurred backdrop: the image's colour, darkened");
    const VideoFXFrame clear = SolidFrame(8, 8, 255, 255, 255, 0);
    RenderView(clear, 0, 0, 8, 4.5, 16, 9, out.data(), 16 * 4, 1);
    CHECK(out[0] == 0 && out[3] == 255, "transparent pixels come out black");

    // Sub-pixel: a 0.3 px move of the camera changes the picture
    VideoFXFrame ramp = SolidFrame(64, 16, 0, 0, 0);
    for (int yy = 0; yy < 16; ++yy)
        for (int xx = 0; xx < 64; ++xx) ramp.pixels[(static_cast<size_t>(yy) * 64 + xx) * 4] = static_cast<uint8_t>(xx * 4);
    std::vector<uint8_t> a(32 * 8 * 4), b(32 * 8 * 4);
    RenderView(ramp, 10.0, 0, 32, 8, 32, 8, a.data(), 32 * 4, 1);
    RenderView(ramp, 10.3, 0, 32, 8, 32, 8, b.data(), 32 * 4, 1);
    CHECK(b[16 * 4] > a[16 * 4], "no whole-pixel snapping (no jitter)");
    std::vector<uint8_t> c(32 * 8 * 4);
    RenderView(ramp, 10.0, 0, 32, 8, 32, 8, c.data(), 32 * 4, 4);
    CHECK(c == a, "threaded rendering matches single-threaded");
}

static void TestMusicMath() {
    std::printf("Background music: envelope, ducking, slideshow length\n");
    std::string error;
    VideoFXMusic m = VideoFXMusic::FromFile("song.mp3", 0.5);
    CHECK(ValidateMusic(m, error), "defaults are valid");
    m.volume = 9.0;
    CHECK(!ValidateMusic(m, error), "volume 9 refused");
    m.volume = 0.5;
    m.duckingLevel = 1.5;
    CHECK(!ValidateMusic(m, error), "ducking level 1.5 refused");
    CHECK(ValidateMusic(VideoFXMusic{}, error), "no music is valid");

    m = VideoFXMusic::FromFile("song.mp3", 0.5);
    m.fadeIn = 2.0;
    m.fadeOut = 4.0;
    CHECK(Near(MusicEnvelope(m, 0.0, 60.0), 0.0, 1e-9), "silent at the very start");
    CHECK(Near(MusicEnvelope(m, 1.0, 60.0), 0.25, 1e-9), "half-way through the fade-in");
    CHECK(Near(MusicEnvelope(m, 30.0, 60.0), 0.5, 1e-9), "full volume in the middle");
    CHECK(Near(MusicEnvelope(m, 58.0, 60.0), 0.25, 1e-9), "half-way through the fade-out");
    CHECK(Near(MusicEnvelope(m, 30.0, 0.0), 0.5, 1e-9), "unknown length: no fade-out");

    MusicDucker d(0.25);
    for (int i = 0; i < 50; ++i) d.Update(0.2, 0.02);            // 1 s of speech
    CHECK(Near(d.Gain(), 0.25, 0.01), "under speech the music sits at the ducking level");
    d.Update(0.0, 0.3);
    CHECK(Near(d.Gain(), 0.25, 0.01), "a short pause between words: still down");
    for (int i = 0; i < 200; ++i) d.Update(0.0, 0.02);           // 4 s of quiet
    CHECK(d.Gain() > 0.95, "after a real pause it comes back up");
    MusicDucker off(1.0);
    off.Update(0.5, 1.0);
    CHECK(Near(off.Gain(), 1.0, 1e-9), "ducking level 1: never dips");

    CHECK(Near(SlideshowSecondsForMusic(10.0, 3, 1.0), 4.0, 1e-9), "3 photos, 1 s overlaps, 10 s song: 4 s each");
    CHECK(Near(SlideshowSecondsForMusic(10.0, 1, 0.0), 10.0, 1e-9), "one photo lasts the song");
    CHECK(Near(SlideshowSecondsForMusic(5.0, 100, 1.0), 2.0, 1e-9), "too many photos: at least twice the transition");
}

// ============================================================================
// PART 2 - ENGINE
// ============================================================================

#ifdef VIDEOFX_HAS_FFMPEG

namespace fs = std::filesystem;

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
static void TestStillImages(const VideoFXExportSettings& base);
static void TestMusic(const VideoFXExportSettings& base);

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
    TestStillImages(base);
    TestMusic(base);

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

// A JPEG carrying an EXIF "rotate 90 clockwise" orientation (value 6)
static bool WriteRotatedJpeg(const VideoFXFrame& frame, const std::string& path) {
    if (VideoFX_SaveFrameImage(frame, path) != VideoFXResult::Ok) return false;
    std::FILE* in = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!in) return false;
    std::vector<uint8_t> jpeg;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) jpeg.insert(jpeg.end(), buf, buf + n);
    std::fclose(in);
    const uint8_t exif[] = {
        0xFF, 0xE1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0,             // APP1, length 34
        'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,                  // little-endian TIFF, IFD at 8
        0x01, 0x00,                                                    // one entry
        0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,   // Orientation = 6
        0x00, 0x00, 0x00, 0x00};                                       // no next IFD
    jpeg.insert(jpeg.begin() + 2, std::begin(exif), std::end(exif));
    std::FILE* out = UltraCanvas::OpenFileUtf8(path, "wb");
    if (!out) return false;
    const bool ok = std::fwrite(jpeg.data(), 1, jpeg.size(), out) == jpeg.size();
    return std::fclose(out) == 0 && ok;
}

static void TestStillImages(const VideoFXExportSettings& base) {
    std::printf("Engine: still images and slideshows\n");
    VideoFXExportSettings s = base;
    s.width = 160;
    s.height = 120;
    s.frameRate = 25.0;
    VideoFXFrame f;
    VideoFXMediaInfo info;
    double r, g, b;

    // ---- an image from memory, 2 s, still ----
    const std::string still = TempPath("still.mkv");
    CHECK_OK(VideoFX_Export({VideoFXSegment::FromImageFrame(SolidFrame(400, 300, 255, 0, 0), 2.0,
                                                            VideoFXImageMotion::Make(VideoFXMotionStyle::Still))},
                            still, s), "still image from memory");
    CHECK_OK(VideoFX_Probe(still, info), "probe still");
    CHECK(Near(info.duration, 2.0, 0.05), "shown for exactly its 2 s");
    VideoFX_ExtractFrame(still, 1.0, f);
    CentreColour(f, r, g, b);
    CHECK(r > 200 && g < 60, "it is the image");

    // ---- zoom in on a file: the picture changes over time ----
    VideoFXFrame checker = SolidFrame(320, 240, 0, 0, 0);
    for (int yy = 0; yy < 240; ++yy)
        for (int xx = 0; xx < 320; ++xx)
            if (((xx / 20) + (yy / 20)) % 2) {
                uint8_t* p = &checker.pixels[(static_cast<size_t>(yy) * 320 + xx) * 4];
                p[0] = p[1] = p[2] = 255;
            }
    const std::string png = TempPath("checker.png");
    CHECK_OK(VideoFX_SaveFrameImage(checker, png), "write a checkerboard PNG");
    const std::string zoom = TempPath("zoom.mkv");
    CHECK_OK(VideoFX_Export({VideoFXSegment::FromImage(png, 2.0,
                                 VideoFXImageMotion::Custom(1.0, 0.5, 0.5, 3.0, 0.5, 0.5))}, zoom, s),
             "zoom into a PNG");
    VideoFXFrame early, late;
    VideoFX_ExtractFrame(zoom, 0.05, early);
    VideoFX_ExtractFrame(zoom, 1.95, late);
    auto edges = [](const VideoFXFrame& fr) {           // black/white changes along the middle row
        int n = 0;
        const size_t row = static_cast<size_t>(fr.height / 2) * fr.width;
        for (int xx = 1; xx < fr.width; ++xx)
            if ((fr.pixels[(row + xx) * 4] > 128) != (fr.pixels[(row + xx - 1) * 4] > 128)) ++n;
        return n;
    };
    CHECK(edges(early) > edges(late) * 2, "zoomed in 3x: far fewer squares across");

    // ---- EXIF orientation ----
    const std::string rotated = TempPath("rotated.jpg");
    CHECK(WriteRotatedJpeg(SolidFrame(64, 32, 0, 0, 255), rotated), "write a JPEG with EXIF orientation 6");
    CHECK_OK(VideoFX_ExtractFrame(rotated, 0.0, f), "decode it");
    CHECK(f.width == 32 && f.height == 64, "shown upright: 64x32 stored, 32x64 displayed");
    std::vector<VideoFXFrame> strip;
    CHECK_OK(VideoFX_ExtractThumbnails(rotated, 3, strip, 32, 32), "several thumbnails of one still");
    CHECK(strip.size() == 3 && strip[2].IsValid(), "a still can be read more than once");

    // ---- the output takes an image's shape when no size is given ----
    VideoFXExportSettings sized = base;
    sized.frameRate = 25.0;
    const std::string shaped = TempPath("shaped.mkv");
    CHECK_OK(VideoFX_Export({VideoFXSegment::FromImage(rotated, 0.5)}, shaped, sized), "export without a size");
    CHECK_OK(VideoFX_Probe(shaped, info), "probe it");
    CHECK(info.width == 32 && info.height == 64, "size from the upright photo");

    // ---- slideshow in one call ----
    VideoFXSlideshowOptions opt;
    opt.secondsPerImage = 2.0;
    opt.transition = VideoFXTransition::Crossfade(0.5);
    opt.captions = {"one", "", "three"};
    const std::string show = TempPath("show.mkv");
    VideoFXExportSettings small = base;
    small.width = 160;
    small.height = 90;
    CHECK_OK(VideoFX_CreateSlideshow({png, rotated, png}, show, opt, small), "slideshow of three images");
    CHECK_OK(VideoFX_Probe(show, info), "probe slideshow");
    CHECK(Near(info.duration, 5.0, 0.06), "3 x 2 s - 2 x 0.5 s overlap = 5 s");
    CHECK(info.width == 160 && info.height == 90 && Near(info.frameRate, 30.0, 0.01), "given size, 30 fps default");
    VideoFX_ExtractFrame(show, 0.0, f);
    CentreColour(f, r, g, b);
    CHECK(r < 40 && g < 40 && b < 40, "fades in from black");

    // ---- a portrait photo in a landscape frame: whole, on a blurred copy ----
    const std::string portrait = TempPath("portrait.mkv");
    VideoFXSegment tall = VideoFXSegment::FromImageFrame(SolidFrame(100, 200, 255, 0, 0), 1.0,
                                                         VideoFXImageMotion::Make(VideoFXMotionStyle::Still));
    CHECK_OK(VideoFX_Export({tall}, portrait, s), "portrait photo, Auto fit");
    VideoFX_ExtractFrame(portrait, 0.5, f);
    int pr, pg, pb;
    PixelAt(f, f.width / 2, 3, pr, pg, pb);
    CHECK(pr > 200, "the whole height is shown (top edge is the photo)");
    PixelAt(f, 4, f.height / 2, pr, pg, pb);
    CHECK(pr > 90 && pr < 200 && pg < 40, "the side is its blurred, darkened copy - not black, not cropped");
    tall.imageFit = VideoFXImageFit::Contain;
    CHECK_OK(VideoFX_Export({tall}, portrait, s), "portrait photo, Contain");
    VideoFX_ExtractFrame(portrait, 0.5, f);
    PixelAt(f, 4, f.height / 2, pr, pg, pb);
    CHECK(pr < 30, "Contain: black bars");
    tall.imageFit = VideoFXImageFit::Cover;
    CHECK_OK(VideoFX_Export({tall}, portrait, s), "portrait photo, Cover");
    VideoFX_ExtractFrame(portrait, 0.5, f);
    PixelAt(f, 4, f.height / 2, pr, pg, pb);
    CHECK(pr > 200, "Cover: fills the frame");

    // ---- errors ----
    CHECK(VideoFX_CreateSlideshow({}, TempPath("x.mkv")) == VideoFXResult::InvalidArgument, "no images");
    CHECK(VideoFX_CreateSlideshow({TempPath("none.jpg")}, TempPath("x.mkv")) == VideoFXResult::FileNotFound,
          "missing image");
    opt.transition = VideoFXTransition::Crossfade(1.5);
    CHECK(VideoFX_CreateSlideshow({png, png}, TempPath("x.mkv"), opt) == VideoFXResult::InvalidArgument,
          "transition longer than half an image");
    VideoFXSegment bad = VideoFXSegment::FromImage(png, 2.0, VideoFXImageMotion::Custom(9.0, 0.5, 0.5, 1.0, 0.5, 0.5));
    CHECK(VideoFX_Export({bad}, TempPath("x.mkv"), s) == VideoFXResult::InvalidArgument, "bad zoom refused");
    CHECK(!Exists(TempPath("x.mkv")), "nothing written by a refused export");
}

// 16-bit PCM WAV, mixed down: RMS (0..1) of [from, to) seconds
struct Wav {
    int rate = 0, channels = 0;
    std::vector<int16_t> samples;           // interleaved
    double Rms(double from, double to) const {
        if (rate <= 0 || channels <= 0) return -1.0;
        const size_t a = static_cast<size_t>(std::max(0.0, from) * rate) * channels;
        const size_t b = std::min(samples.size(), static_cast<size_t>(to * rate) * channels);
        if (b <= a) return -1.0;
        double sum = 0.0;
        for (size_t i = a; i < b; ++i) sum += (samples[i] / 32768.0) * (samples[i] / 32768.0);
        return std::sqrt(sum / static_cast<double>(b - a));
    }
    double Seconds() const { return rate > 0 && channels > 0 ? double(samples.size()) / channels / rate : 0.0; }
    // RMS of (this - other) over [from, to): what one export added to the other,
    // independent of how the two sounds' phases happen to line up
    double RmsOfDifference(const Wav& other, double from, double to) const {
        if (rate <= 0 || channels <= 0 || other.channels != channels) return -1.0;
        const size_t a = static_cast<size_t>(std::max(0.0, from) * rate) * channels;
        const size_t b = std::min({samples.size(), other.samples.size(), static_cast<size_t>(to * rate) * channels});
        if (b <= a) return -1.0;
        double sum = 0.0;
        for (size_t i = a; i < b; ++i) {
            const double d = (samples[i] - other.samples[i]) / 32768.0;
            sum += d * d;
        }
        return std::sqrt(sum / static_cast<double>(b - a));
    }
};

static Wav ReadWav(const std::string& path) {
    Wav w;
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!f) return w;
    std::vector<uint8_t> bytes;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    auto u16 = [&](size_t o) { return uint32_t(bytes[o]) | (uint32_t(bytes[o + 1]) << 8); };
    auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
    for (size_t o = 12; o + 8 <= bytes.size();) {                  // RIFF chunks after "WAVE"
        const std::string id(reinterpret_cast<const char*>(&bytes[o]), 4);
        const size_t size = u32(o + 4);
        if (id == "fmt " && o + 24 <= bytes.size()) {
            w.channels = static_cast<int>(u16(o + 10));
            w.rate = static_cast<int>(u32(o + 12));
        } else if (id == "data") {
            const size_t end = std::min(bytes.size(), o + 8 + size);
            for (size_t i = o + 8; i + 1 < end; i += 2) w.samples.push_back(static_cast<int16_t>(u16(i)));
            break;
        }
        o += 8 + size + (size & 1);
    }
    return w;
}

static void TestMusic(const VideoFXExportSettings& base) {
    std::printf("Engine: background music\n");
    (void)base;
    VideoFXMediaInfo info;

    // A 1 s and a 6 s "song" (the test pattern's tone) to put under things
    const std::string shortSong = TempPath("song1.mkv");
    const std::string longSong = TempPath("song6.mkv");
    CHECK_OK(VideoFX_GenerateTestClip(shortSong, 1.0, 64, 48, 25.0, true), "make a 1 s song");
    CHECK_OK(VideoFX_GenerateTestClip(longSong, 6.0, 64, 48, 25.0, true), "make a 6 s song");

    // ---- music under silent pictures, written as WAV to measure ----
    VideoFXExportSettings wav = VideoFXExportSettings::AudioOnlyWAV();
    wav.music = VideoFXMusic::FromFile(longSong, 1.0);
    wav.music.fadeIn = 0.5;
    wav.music.fadeOut = 0.0;
    const std::vector<VideoFXSegment> cards = {VideoFXSegment::SolidColor(0x000000, 2.0),
                                               VideoFXSegment::SolidColor(0xFFFFFF, 2.0)};
    const std::string bed = TempPath("bed.wav");
    CHECK_OK(VideoFX_Export(cards, bed, wav), "silent cards + music, sound only");
    Wav w = ReadWav(bed);
    CHECK(Near(w.Seconds(), 4.0, 0.05), "as long as the pictures, not the song");
    CHECK(w.Rms(1.0, 3.5) > 0.05, "the music is there");
    CHECK(w.Rms(0.0, 0.05) < w.Rms(1.0, 3.5) / 4, "and fades in");

    // ---- shorter than the export: loops, or stops ----
    wav.music = VideoFXMusic::FromFile(shortSong, 1.0);
    wav.music.fadeIn = wav.music.fadeOut = 0.0;
    const std::string looped = TempPath("looped.wav");
    CHECK_OK(VideoFX_Export(cards, looped, wav), "1 s song under 4 s");
    w = ReadWav(looped);
    CHECK(w.Rms(2.2, 2.8) > 0.05, "loops: still playing after its end");
    wav.music.loop = false;
    const std::string once = TempPath("once.wav");
    CHECK_OK(VideoFX_Export(cards, once, wav), "the same, no loop");
    w = ReadWav(once);
    CHECK(w.Rms(0.2, 0.8) > 0.05 && w.Rms(2.2, 2.8) < 0.001, "no loop: plays once, then silence");

    // ---- ducking: under the segments' own sound the music goes down ----
    const std::vector<VideoFXSegment> talk = {VideoFXSegment::TestPattern(2.0), VideoFXSegment::SolidColor(0, 2.0)};
    VideoFXExportSettings duck = VideoFXExportSettings::AudioOnlyWAV();
    const std::string dry = TempPath("dry.wav");
    CHECK_OK(VideoFX_Export(talk, dry, duck), "the segments' sound alone");
    duck.music = VideoFXMusic::FromFile(longSong, 1.0);
    duck.music.fadeIn = duck.music.fadeOut = 0.0;
    duck.music.duckingLevel = 1.0;
    const std::string full = TempPath("full.wav");
    CHECK_OK(VideoFX_Export(talk, full, duck), "music, never ducked");
    duck.music.duckingLevel = 0.0;
    const std::string ducked = TempPath("ducked.wav");
    CHECK_OK(VideoFX_Export(talk, ducked, duck), "music, ducked to nothing");
    const Wav wd = ReadWav(dry), wf = ReadWav(full), wk = ReadWav(ducked);
    // (Compared sample by sample: the music and the clip are the same tone,
    // so their loudness alone says nothing - their phases may cancel.)
    const double music = wf.RmsOfDifference(wd, 3.2, 3.9);
    CHECK(music > 0.05, "the music alone, after the sound");
    CHECK(Near(wf.RmsOfDifference(wd, 0.8, 1.8), music, music * 0.1), "not ducked: full music under the sound");
    CHECK(wk.RmsOfDifference(wd, 0.8, 1.8) < music * 0.05, "ducked to 0: under the sound, no music");
    CHECK(wk.Rms(3.2, 3.9) > 0.05, "and the music returns once the sound stops");

    // ---- a slideshow that ends with its song ----
    VideoFXFrame px;
    px.width = 64; px.height = 48; px.pixels.assign(64 * 48 * 4, 200);
    const std::string photo = TempPath("photo.png");
    CHECK_OK(VideoFX_SaveFrameImage(px, photo), "a photo");
    VideoFXSlideshowOptions opt;
    opt.transition = VideoFXTransition::Crossfade(0.5);
    opt.music = VideoFXMusic::FromFile(longSong);
    opt.matchMusicLength = true;
    VideoFXExportSettings small = base;
    small.width = 64;
    small.height = 36;
    const std::string show = TempPath("music-show.mkv");
    CHECK_OK(VideoFX_CreateSlideshow({photo, photo, photo}, show, opt, small), "slideshow fitted to the music");
    CHECK_OK(VideoFX_Probe(show, info), "probe it");
    CHECK(Near(info.duration, 6.0, 0.1) && info.HasAudio(), "6 s song, 6 s slideshow, with sound");

    // ---- errors ----
    VideoFXExportSettings bad = VideoFXExportSettings::AudioOnlyWAV();
    bad.music = VideoFXMusic::FromFile(TempPath("none.mp3"));
    CHECK(VideoFX_Export(cards, TempPath("x.wav"), bad) == VideoFXResult::FileNotFound, "missing music file");
    bad.music = VideoFXMusic::FromFile(photo);
    CHECK(VideoFX_Export(cards, TempPath("x.wav"), bad) == VideoFXResult::NoMediaStreams, "music without sound");
    bad.music = VideoFXMusic::FromFile(longSong, 9.0);
    CHECK(VideoFX_Export(cards, TempPath("x.wav"), bad) == VideoFXResult::InvalidArgument, "volume 9");
    VideoFXSlideshowOptions noMusic;
    noMusic.matchMusicLength = true;
    CHECK(VideoFX_CreateSlideshow({photo}, TempPath("x.mkv"), noMusic) == VideoFXResult::InvalidArgument,
          "fit to music without music");
    CHECK(!Exists(TempPath("x.wav")) && !Exists(TempPath("x.mkv")), "nothing written by refused exports");
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
    TestKenBurnsMath();
    TestMusicMath();
    TestEngine();
    if (failures) {
        std::printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nAll VideoFX checks passed\n");
    return 0;
}
