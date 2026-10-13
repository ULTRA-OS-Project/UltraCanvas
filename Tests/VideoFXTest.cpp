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
// Version: 0.14.0 - VideoFX_GetFileFormats: every type it writes written and read back
// Version: 0.13.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "VideoFX/VideoFX.h"
#include "VideoFXBeats.h"
#include "VideoFXFaces.h"
#include "VideoFXFilterBuilder.h"
#include "VideoFXKenBurns.h"
#include "VideoFXMusic.h"
#include "VideoFXProject.h"
#include "VideoFXProxy.h"
#include "VideoFXPlatform.h"

#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <locale>
#include <random>
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
    CHECK(!ValidateOverlay(VideoFXOverlay::Video(""), 5.0, error), "video without a file refused");
    const VideoFXExportSettings draft = VideoFXExportSettings::Draft();
    CHECK(draft.container == VideoFXContainer::MP4 && draft.videoCodec == VideoFXVideoCodec::H264 && draft.height == 360 &&
          draft.encoderPreset == "ultrafast" && draft.quality == 30 && draft.audioBitRate == 96000, "the draft preset: a quick small MP4");
    VideoFXOverlay pip = VideoFXOverlay::Video("cam.mp4", VideoFXAnchor::BottomRight, 0.25, 1.5);
    CHECK(ValidateOverlay(pip, 5.0, error) && pip.kind == VideoFXOverlayKind::Video && Near(pip.videoStart, 1.5, 1e-12),
          "a video overlay with its start");
    pip.start = 1.0;
    pip.end = 3.0;
    pip.fadeIn = 0.5;
    pip.opacity = 0.8;
    BuildVideoOverlayFilters(pip, 640, 360, 90, 5.0, chain, ov);
    CHECK(chain == "format=rgba,setsar=1,colorchannelmixer=aa=0.8,fade=t=in:st=1:d=0.5:alpha=1",
          "video chain: no scale at the fed height, opacity, a fade on the stream's own clock");
    CHECK(ov.find("eof_action=repeat") != std::string::npos && ov.find("enable=between(t\\,1\\,3)") != std::string::npos,
          "the last frame holds; shown in its window");
    BuildVideoOverlayFilters(pip, 640, 360, 200, 5.0, chain, ov);
    CHECK(chain.find("scale=-1:90") != std::string::npos, "scaled when fed at another height");
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

    // ---- keeping faces in shot ----
    CHECK(ValidateKeepInView({VideoFXRect::Make(0.4, 0.4, 0.2, 0.2)}, error), "a face region is valid");
    CHECK(!ValidateKeepInView({VideoFXRect::Make(0.9, 0.4, 0.2, 0.2)}, error), "a region past the edge refused");
    CHECK(!ValidateKeepInView({VideoFXRect::Make(0.4, 0.4, 0.0, 0.2)}, error), "an empty region refused");
    VideoFXRect box = FocusBounds({VideoFXRect::Make(0.4, 0.4, 0.2, 0.2)});
    CHECK(Near(box.x, 0.37, 1e-9) && Near(box.w, 0.26, 1e-9), "headroom: 15 % of the face each side");
    box = FocusBounds({VideoFXRect::Make(0.0, 0.1, 0.1, 0.1), VideoFXRect::Make(0.5, 0.6, 0.1, 0.1)});
    CHECK(Near(box.x, 0.0, 1e-9) && Near(box.y, 0.085, 1e-9) && Near(box.x + box.w, 0.615, 1e-9) &&
          Near(box.y + box.h, 0.715, 1e-9), "two faces: one box round both, clipped at the edge");
    CHECK(FocusBounds({}).IsEmpty(), "no regions: no box");

    // Does the view at `fraction` - ends fitted, every frame slid - hold `focus`?
    auto holds = [](const VideoFXImageMotion& m, const VideoFXRect& focus, int iw, int ih, int ow, int oh,
                    VideoFXImageFit fit, bool slide) {
        for (int k = 0; k <= 40; ++k) {
            double x, y, w, h;
            const KenBurnsView v = ViewAt(m, k / 40.0);
            if (fit == VideoFXImageFit::Cover) ViewRect(v, iw, ih, ow, oh, x, y, w, h);
            else ContainViewRect(v, iw, ih, ow, oh, x, y, w, h);
            if (slide) KeepFocusInView(focus, iw, ih, x, y, w, h);
            const double e = 1e-6;
            if (x > focus.x * iw + e || y > focus.y * ih + e || x + w < (focus.x + focus.w) * iw - e ||
                y + h < (focus.y + focus.h) * ih - e)
                return false;
        }
        return true;
    };
    const VideoFXRect face = FocusBounds({VideoFXRect::Make(0.8, 0.3, 0.1, 0.13)});
    const VideoFXImageMotion zoomIn = ResolveMotion(VideoFXImageMotion::Make(VideoFXMotionStyle::ZoomIn),
                                                    4000, 3000, 1920, 1080, 0);
    CHECK(!holds(zoomIn, face, 4000, 3000, 1920, 1080, VideoFXImageFit::Cover, false),
          "a face near the edge: a plain zoom-in loses it");
    VideoFXImageMotion fitted = FitMotionToFocus(zoomIn, face, 4000, 3000, 1920, 1080, VideoFXImageFit::Cover);
    CHECK(Near(fitted.endZoom, zoomIn.endZoom, 1e-9) && fitted.endX > 0.5, "fitted: same zoom, moved towards the face");
    CHECK(holds(fitted, face, 4000, 3000, 1920, 1080, VideoFXImageFit::Cover, true), "and it holds the face throughout");
    const VideoFXRect group = FocusBounds({VideoFXRect::Make(0.1, 0.2, 0.8, 0.5)});
    fitted = FitMotionToFocus(zoomIn, group, 4000, 3000, 1920, 1080, VideoFXImageFit::Cover);
    CHECK(Near(fitted.endZoom, 1.0, 1e-9), "a wide group: no closer than holds them all");
    const VideoFXImageMotion panRight = ResolveMotion(VideoFXImageMotion::Make(VideoFXMotionStyle::PanRight),
                                                      4000, 3000, 1920, 1080, 0);
    fitted = FitMotionToFocus(panRight, face, 4000, 3000, 1920, 1080, VideoFXImageFit::Cover);
    CHECK(fitted.startX > 0.0 && fitted.endX > fitted.startX, "a pan: narrowed round the face, still a pan");
    CHECK(holds(fitted, face, 4000, 3000, 1920, 1080, VideoFXImageFit::Cover, true), "holding it all the way");
    const VideoFXRect top = FocusBounds({VideoFXRect::Make(0.4, 0.05, 0.2, 0.07)});
    fitted = FitMotionToFocus(VideoFXImageMotion::Make(VideoFXMotionStyle::Still), top, 1200, 4800, 1600, 1200,
                              VideoFXImageFit::Cover);
    CHECK(Near(fitted.startZoom, 1.0, 1e-9) && Near(fitted.endZoom, 1.0, 1e-9) && fitted.startY < 0.2 &&
          Near(fitted.startY, fitted.endY, 1e-9), "Still on a tall photo: still, but looking at the face");
    // Any motion, any face that fits: held in every frame
    bool always = true;
    for (int i = 0; i < 200 && always; ++i) {
        const double fx = (i * 37 % 90) / 100.0, fy = (i * 53 % 90) / 100.0;
        const VideoFXRect f = FocusBounds({VideoFXRect::Make(fx, fy, 0.08, 0.08)});
        const VideoFXImageMotion m = VideoFXImageMotion::Custom(1.0 + (i % 7) * 0.4, (i % 5) / 4.0, (i % 3) / 2.0,
                                                                1.0 + (i % 4) * 0.5, (i % 2), ((i + 1) % 5) / 4.0);
        const VideoFXImageFit fit = i % 2 ? VideoFXImageFit::Cover : VideoFXImageFit::BlurredBackground;
        always = holds(FitMotionToFocus(m, f, 3000, 2000, 1280, 720, fit), f, 3000, 2000, 1280, 720, fit, true);
    }
    CHECK(always, "200 motions and faces: the face in every frame");
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

    VideoFXMusic speech = VideoFXMusic::FromFile("song.mp3");
    speech.duckingLevel = 0.25;
    MusicDucker d(speech);
    for (int i = 0; i < 50; ++i) d.Update(0.2, 0.02);            // 1 s of speech
    CHECK(Near(d.Gain(), 0.25, 0.01), "under speech the music sits at the ducking level");
    d.Update(0.0, 0.3);
    CHECK(Near(d.Gain(), 0.25, 0.01), "a short pause between words: still down");
    for (int i = 0; i < 200; ++i) d.Update(0.0, 0.02);           // 4 s of quiet
    CHECK(d.Gain() > 0.95, "after a real pause it comes back up");
    VideoFXMusic never = speech;
    never.duckingLevel = 1.0;
    MusicDucker off(never);
    off.Update(0.5, 1.0);
    CHECK(Near(off.Gain(), 1.0, 1e-9), "ducking level 1: never dips");

    // A loud clip: a steady -20 dBFS background (0.1 RMS) holds the music down
    // with the speech defaults; with the threshold raised above it, it does not,
    // while a louder moment (0.5 RMS, about -6 dBFS) still ducks.
    MusicDucker speechTuned(speech);
    for (int i = 0; i < 100; ++i) speechTuned.Update(0.1, 0.02);  // 2 s of loud background
    CHECK(Near(speechTuned.Gain(), 0.25, 0.01), "speech defaults: a loud background keeps the music down");
    VideoFXMusic loud = speech;
    loud.duckingThresholdDb = -12.0;
    MusicDucker loudTuned(loud);
    for (int i = 0; i < 100; ++i) loudTuned.Update(0.1, 0.02);
    CHECK(Near(loudTuned.Gain(), 1.0, 1e-9), "threshold -12 dBFS: the background alone does not duck");
    for (int i = 0; i < 50; ++i) loudTuned.Update(0.5, 0.02);
    CHECK(Near(loudTuned.Gain(), 0.25, 0.01), "threshold -12 dBFS: a louder moment still ducks");

    // The times are the settings': a short hold lets it come back after a pause
    // the speech hold (0.6 s) would wait out.
    VideoFXMusic quick = speech;
    quick.duckingHold = 0.1;
    quick.duckingRelease = 0.05;
    MusicDucker q(quick), slow(speech);
    for (int i = 0; i < 50; ++i) { q.Update(0.2, 0.02); slow.Update(0.2, 0.02); }
    for (int i = 0; i < 20; ++i) { q.Update(0.0, 0.02); slow.Update(0.0, 0.02); }   // 0.4 s pause
    CHECK(q.Gain() > 0.9, "hold 0.1 s, release 0.05 s: back up after a 0.4 s pause");
    CHECK(Near(slow.Gain(), 0.25, 0.01), "speech hold 0.6 s: still down after a 0.4 s pause");

    // Presets fill in the threshold and times, never the depth
    VideoFXMusic preset;
    preset.duckingLevel = 0.2;
    preset.SetDuckingPreset(VideoFXDuckingPreset::LoudEvent);
    CHECK(Near(preset.duckingThresholdDb, -15.0, 1e-9) && Near(preset.duckingLevel, 0.2, 1e-9),
          "LoudEvent preset: -15 dBFS, the ducking depth left alone");
    CHECK(ValidateMusic(preset, error), "LoudEvent preset is valid");
    MusicDucker loudPreset(preset);
    for (int i = 0; i < 100; ++i) loudPreset.Update(0.1, 0.02);  // the -20 dBFS background again
    CHECK(Near(loudPreset.Gain(), 1.0, 1e-9), "LoudEvent preset: a loud background alone does not duck");
    preset.SetDuckingPreset(VideoFXDuckingPreset::Outdoor);
    CHECK(Near(preset.duckingThresholdDb, -28.0, 1e-9) && ValidateMusic(preset, error), "Outdoor preset: -28 dBFS");
    preset.SetDuckingPreset(VideoFXDuckingPreset::Speech);
    const VideoFXMusic defaults;
    CHECK(preset.duckingThresholdDb == defaults.duckingThresholdDb && preset.duckingAttack == defaults.duckingAttack &&
          preset.duckingHold == defaults.duckingHold && preset.duckingRelease == defaults.duckingRelease,
          "Speech preset equals the defaults");

    m = VideoFXMusic::FromFile("song.mp3");
    m.duckingThresholdDb = 3.0;
    CHECK(!ValidateMusic(m, error), "threshold above 0 dBFS refused");
    m.duckingThresholdDb = -20.0;
    m.duckingAttack = 0.0;
    CHECK(!ValidateMusic(m, error), "attack 0 refused");
    m.duckingAttack = 0.05;
    m.duckingHold = -1.0;
    CHECK(!ValidateMusic(m, error), "negative hold refused");
    m.duckingHold = 0.0;
    m.duckingRelease = std::nan("");
    CHECK(!ValidateMusic(m, error), "release NaN refused");
    m.duckingRelease = 2.0;
    CHECK(ValidateMusic(m, error), "loud-clip settings are valid");

    CHECK(Near(SlideshowSecondsForMusic(10.0, 3, 1.0), 4.0, 1e-9), "3 photos, 1 s overlaps, 10 s song: 4 s each");
    CHECK(Near(SlideshowSecondsForMusic(10.0, 1, 0.0), 10.0, 1e-9), "one photo lasts the song");
    CHECK(Near(SlideshowSecondsForMusic(5.0, 100, 1.0), 2.0, 1e-9), "too many photos: at least twice the transition");

    // ---- song lists ----
    VideoFXMusic list = VideoFXMusic::FromFiles({"a.mp3", "b.mp3"});
    CHECK(list.IsSet() && list.path.empty(), "a playlist alone is music");
    list.path = "intro.mp3";
    CHECK(list.Songs() == std::vector<std::string>({"intro.mp3", "a.mp3", "b.mp3"}), "path plays first, then the list");
    CHECK(ValidateMusic(list, error), "a playlist is valid");
    list.crossfade = 31.0;
    CHECK(!ValidateMusic(list, error), "crossfade 31 s refused");
    list.crossfade = 3.0;
    list.playlist.push_back("");
    CHECK(!ValidateMusic(list, error), "a playlist entry without a path refused");

    CHECK(Near(CrossfadeSeconds(3.0, 180.0, 200.0), 3.0, 1e-9), "long songs: the crossfade asked for");
    CHECK(Near(CrossfadeSeconds(3.0, 4.0, 200.0), 2.0, 1e-9), "a 4 s song: at most half of it");
    CHECK(Near(CrossfadeSeconds(3.0, 0.0, 0.0), 3.0, 1e-9), "unknown lengths do not limit it");
    CHECK(Near(PlaylistSeconds({60.0, 60.0, 60.0}, 3.0, 0.0), 174.0, 1e-9), "three songs, two 3 s overlaps");
    CHECK(Near(PlaylistSeconds({60.0, 60.0}, 3.0, 10.0), 107.0, 1e-9), "the start offset cuts the first song");
    CHECK(Near(PlaylistSeconds({60.0, 0.0}, 3.0, 0.0), 0.0, 1e-9), "a length unknown: unknown");
    double gOut = 0.0, gIn = 0.0;
    CrossfadeGains(0.0, gOut, gIn);
    CHECK(Near(gOut, 1.0, 1e-9) && Near(gIn, 0.0, 1e-9), "crossfade start: only the outgoing song");
    CrossfadeGains(0.5, gOut, gIn);
    CHECK(Near(gOut * gOut + gIn * gIn, 1.0, 1e-9) && Near(gOut, gIn, 1e-9), "half-way: equal power, both alike");
    CrossfadeGains(1.0, gOut, gIn);
    CHECK(Near(gOut, 0.0, 1e-9) && Near(gIn, 1.0, 1e-9), "crossfade end: only the incoming song");
}

// ============================================================================
// PART 2 - ENGINE
// ============================================================================

// A click track: a short low thump every beat from `offset`, over optional noise
static std::vector<float> ClickTrack(int rate, double bpm, double offset, double seconds, float noise = 0.0f) {
    std::vector<float> s(static_cast<size_t>(seconds * rate), 0.0f);
    std::mt19937 gen(7);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    if (noise > 0.0f) for (float& v : s) v = noise * dist(gen);
    for (double t = offset; t < seconds; t += 60.0 / bpm) {
        const size_t at = static_cast<size_t>(t * rate);
        const int length = rate / 30;
        for (int i = 0; i < length && at + i < s.size(); ++i)
            s[at + i] += static_cast<float>(0.8 * std::exp(-i * 280.0 / rate) * std::sin(2.0 * 3.14159265 * 150.0 * i / rate));
    }
    return s;
}

// Free audio clips without the engine: values, gain, length
static void TestAudioClipMath() {
    std::printf("Free audio clips: values, envelope, length\n");
    std::string error;
    VideoFXAudioClip c = VideoFXAudioClip::Sound("boom.wav", 2.0);
    CHECK(ValidateAudioClip(c, error) && !c.ducksMusic, "a sound effect: valid, does not duck the music");
    CHECK(VideoFXAudioClip::VoiceOver("voice.wav", 1.0).ducksMusic, "a voice-over ducks it");
    VideoFXAudioClip bad = c;
    bad.path.clear();
    CHECK(!ValidateAudioClip(bad, error), "no file refused");
    bad = c; bad.at = -1.0;
    CHECK(!ValidateAudioClip(bad, error), "a time before the start refused");
    bad = c; bad.start = 3.0; bad.end = 2.0;
    CHECK(!ValidateAudioClip(bad, error), "an end before the start refused");
    bad = c; bad.volume = 5.0;
    CHECK(!ValidateAudioClip(bad, error), "volume over 4 refused");
    bad = c; bad.fadeIn = -0.5;
    CHECK(!ValidateAudioClip(bad, error) && !error.empty(), "a negative fade refused, with a reason");

    c.volume = 0.5;
    c.fadeIn = 1.0;
    c.fadeOut = 2.0;
    CHECK(Near(AudioClipGain(c, 0.0, 10.0), 0.0, 1e-12) && Near(AudioClipGain(c, 0.5, 10.0), 0.25, 1e-12) &&
          Near(AudioClipGain(c, 5.0, 10.0), 0.5, 1e-12) && Near(AudioClipGain(c, 9.0, 10.0), 0.25, 1e-12),
          "volume times the fades in and out");
    CHECK(AudioClipGain(c, -0.1, 10.0) == 0.0 && AudioClipGain(c, 10.1, 10.0) == 0.0, "silent outside the clip");
    CHECK(Near(AudioClipGain(c, 5.0, 0.0), 0.5, 1e-12), "an unknown length: no fade-out");
    // fades longer than the clip shrink to half each, meeting in the middle
    CHECK(Near(AudioClipGain(c, 1.0, 2.0), 0.5, 1e-12) && Near(AudioClipGain(c, 0.5, 2.0), 0.25, 1e-12),
          "fades that would overlap share the clip");

    VideoFXAudioClip r = VideoFXAudioClip::Sound("x.wav", 0.0);
    CHECK(Near(AudioClipSeconds(r, 8.0), 8.0, 1e-12) && AudioClipSeconds(r, 0.0) == 0.0, "the whole file, or unknown");
    r.start = 1.5;
    CHECK(Near(AudioClipSeconds(r, 8.0), 6.5, 1e-12), "what is left after the start");
    r.end = 4.0;
    CHECK(Near(AudioClipSeconds(r, 8.0), 2.5, 1e-12) && Near(AudioClipSeconds(r, 0.0), 2.5, 1e-12), "start to end");
    r.end = 20.0;
    CHECK(Near(AudioClipSeconds(r, 8.0), 6.5, 1e-12), "an end past the file stops with the file");
}

static void TestBeatMath() {
    std::printf("Beat detection: tempo, beats, changes on the beat\n");
    const int r = kBeatSampleRate;
    BeatAnalysis a = AnalyseBeats(ClickTrack(r, 120.0, 0.25, 12.0), r);
    CHECK(Near(a.bpm, 120.0, 1.0) && a.confidence > 0.5, "120 BPM clicks: 120 BPM");
    CHECK(!a.beats.empty() && Near(a.beats.front(), 0.25, 0.02), "the first beat on the first click");
    bool onClicks = a.beats.size() >= 20;
    for (double b : a.beats) {
        const double phase = std::fmod(b - 0.25 + 100.0, 0.5);
        onClicks = onClicks && (phase < 0.02 || phase > 0.48);
    }
    CHECK(onClicks, "every beat within 20 ms of a click");
    a = AnalyseBeats(ClickTrack(r, 150.0, 0.05, 12.0, 0.02f), r);
    CHECK(Near(a.bpm, 150.0, 2.0), "150 BPM over noise: 150, not half of it");
    a = AnalyseBeats(ClickTrack(r, 90.0, 0.4, 12.0), r);
    CHECK(Near(a.bpm, 90.0, 1.5), "90 BPM");
    a = AnalyseBeats(std::vector<float>(static_cast<size_t>(4 * r), 0.0f), r);
    CHECK(a.bpm == 0.0 && a.beats.empty(), "silence: no beat");
    std::vector<float> noise(static_cast<size_t>(12 * r));
    std::mt19937 gen(3);
    std::normal_distribution<float> dist(0.0f, 0.2f);
    for (float& v : noise) v = dist(gen);
    a = AnalyseBeats(noise, r);
    CHECK(a.bpm == 0.0 && a.beats.empty(), "white noise: no beat");

    std::vector<double> beats;
    for (int k = 0; k < 24; ++k) beats.push_back(0.25 + 0.5 * k);
    auto same = [](const std::vector<double>& got, const std::vector<double>& want) {
        if (got.size() != want.size()) return false;
        for (size_t i = 0; i < got.size(); ++i) if (!Near(got[i], want[i], 1e-9)) return false;
        return true;
    };
    CHECK(same(BeatAlignedChanges(beats, 4, 1.7, 1.0, 0), {1.75, 3.25, 4.75}),
          "1.7 s per image: each change on the nearest beat");
    CHECK(same(BeatAlignedChanges(beats, 4, 1.7, 1.0, 4), {2.25, 4.25, 6.25}), "4 beats per image: one bar each");
    CHECK(same(BeatAlignedChanges(beats, 4, 1.7, 1.0, 1), {1.25, 2.25, 3.25}),
          "1 beat per image is under the 1 s minimum: 2 beats");
    CHECK(same(BeatAlignedChanges({0.25, 0.75, 1.25, 1.75}, 4, 1.0, 1.0, 0), {1.25, 2.25, 3.25}),
          "past the last beat: on the beat grid");
    CHECK(same(BeatAlignedChanges({}, 4, 1.7, 1.0, 0), {1.7, 3.4, 5.1}), "no beats: every 1.7 s");
    std::vector<double> wobbly = beats;
    wobbly[8] = 4.247;                      // 3 ms early, as detected beats are
    CHECK(same(BeatAlignedChanges(wobbly, 3, 2.0, 2.0, 0), {2.25, 4.247}),
          "a beat 3 ms short of the minimum gap still takes the change");
    CHECK(BeatAlignedChanges(beats, 1, 1.7, 1.0, 0).empty(), "one image: no change");
}

// ---- the built-in face detector ----

// The test photo (a real face, public domain - Tests/data/videofx/README.md)
// as RGBA, read from its binary PPM without any decoder
static bool ReadPpm(const std::string& path, VideoFXFrame& frame) {
    std::ifstream f(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    std::string magic;
    int w = 0, h = 0, maxValue = 0;
    if (!(f >> magic >> w >> h >> maxValue) || magic != "P6" || maxValue != 255 || w <= 0 || h <= 0) return false;
    f.get();
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    if (!f.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()))) return false;
    frame.width = w;
    frame.height = h;
    frame.pixels.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        frame.pixels[i * 4] = rgb[i * 3];
        frame.pixels[i * 4 + 1] = rgb[i * 3 + 1];
        frame.pixels[i * 4 + 2] = rgb[i * 3 + 2];
        frame.pixels[i * 4 + 3] = 255;
    }
    return true;
}

// BT.601 grey, as OpenCV's RGB2GRAY
static std::vector<uint8_t> Grey(const VideoFXFrame& f) {
    std::vector<uint8_t> g(static_cast<size_t>(f.width) * f.height);
    for (size_t i = 0; i < g.size(); ++i)
        g[i] = static_cast<uint8_t>((f.pixels[i * 4] * 4899 + f.pixels[i * 4 + 1] * 9617 + f.pixels[i * 4 + 2] * 1868 +
                                     8192) >> 14);
    return g;
}

static const std::string kAstronaut = std::string(VIDEOFX_TEST_DATA_DIR) + "/astronaut-200.ppm";

static void TestFaceDetector() {
    std::printf("Face detector: a real face, mirrored, several, none\n");
    VideoFXFrame photo;
    CHECK(ReadPpm(kAstronaut, photo) && photo.width == 200 && photo.height == 200, "read the test photo");
    if (!photo.IsValid()) return;
    const int w = photo.width, h = photo.height;
    const std::vector<uint8_t> px = Grey(photo);
    // OpenCV's CascadeClassifier, same model and settings, finds (66, 24) 43 x 43
    std::vector<FaceBox> boxes = DetectFacesGray(px.data(), w, h, w);
    CHECK(boxes.size() == 1, "one face, as OpenCV finds");
    if (boxes.size() == 1)
        CHECK(std::abs(boxes[0].x - 66) <= 3 && std::abs(boxes[0].y - 24) <= 3 && std::abs(boxes[0].w - 43) <= 3,
              "where OpenCV finds it, within 3 pixels");
    std::vector<uint8_t> mirrored(px.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) mirrored[static_cast<size_t>(y) * w + x] = px[static_cast<size_t>(y) * w + (w - 1 - x)];
    boxes = DetectFacesGray(mirrored.data(), w, h, w);
    CHECK(boxes.size() == 1 && std::abs(boxes[0].x - 91) <= 3, "mirrored: found on the other side (OpenCV: x 91)");

    std::vector<VideoFXRect> faces;
    CHECK(VideoFX_DetectFaces(photo, faces) == VideoFXResult::Ok && faces.size() == 1, "VideoFX_DetectFaces: one face");
    if (faces.size() == 1)
        CHECK(Near(faces[0].x, 0.335, 0.02) && Near(faces[0].y, 0.12, 0.02) && Near(faces[0].w, 0.21, 0.02),
              "as fractions of the photo");
    CHECK(VideoFX_DetectFaces(VideoFXFrame{}, faces) == VideoFXResult::InvalidArgument, "no image: refused");
    // In colour a face needs skin: the same face tinted blue is no face
    VideoFXFrame blue = photo;
    for (int y = 20; y < 70; ++y)
        for (int x = 60; x < 115; ++x) {
            uint8_t* p = &blue.pixels[(static_cast<size_t>(y) * w + x) * 4];
            const int v = (p[0] + p[1] + p[2]) / 3;
            p[0] = static_cast<uint8_t>(v / 2); p[1] = static_cast<uint8_t>(v * 3 / 4); p[2] = static_cast<uint8_t>(std::min(255, v + 60));
        }
    CHECK(VideoFX_DetectFaces(blue, faces) == VideoFXResult::Ok && faces.empty(), "colour photo, blue face: no skin, no face");
    VideoFXFrame mono = photo;
    for (size_t i = 0; i < px.size(); ++i) mono.pixels[i * 4] = mono.pixels[i * 4 + 1] = mono.pixels[i * 4 + 2] = px[i];
    CHECK(VideoFX_DetectFaces(mono, faces) == VideoFXResult::Ok && faces.size() == 1,
          "black and white photo: found without the skin test");

    // Four people: the photo at four sizes on one canvas
    VideoFXFrame group;
    group.width = 640;
    group.height = 400;
    group.pixels.assign(static_cast<size_t>(640) * 400 * 4, 128);
    const int places[4][3] = {{10, 10, 200}, {250, 20, 170}, {440, 180, 190}, {120, 250, 140}};
    for (const auto& p : places)                // all four found by OpenCV too
        for (int y = 0; y < p[2]; ++y)
            for (int x = 0; x < p[2]; ++x) {
                const uint8_t* s0 = &photo.pixels[(static_cast<size_t>(y * h / p[2]) * w + x * w / p[2]) * 4];
                uint8_t* d = &group.pixels[(static_cast<size_t>(p[1] + y) * 640 + p[0] + x) * 4];
                d[0] = s0[0]; d[1] = s0[1]; d[2] = s0[2];
            }
    CHECK(VideoFX_DetectFaces(group, faces) == VideoFXResult::Ok && faces.size() == 4, "four faces on one picture");
    CHECK(faces.size() == 4 && faces[0].w >= faces[3].w, "largest first");

    VideoFXFrame flat;
    flat.width = 320; flat.height = 240;
    flat.pixels.assign(static_cast<size_t>(320) * 240 * 4, 180);
    CHECK(VideoFX_DetectFaces(flat, faces) == VideoFXResult::Ok && faces.empty(), "a flat picture: none");
    std::mt19937 gen(5);
    for (size_t i = 0; i < flat.pixels.size(); ++i) flat.pixels[i] = (i % 4 == 3) ? 255 : static_cast<uint8_t>(gen());
    CHECK(VideoFX_DetectFaces(flat, faces) == VideoFXResult::Ok && faces.empty(), "noise: none");
}

// ---- project files ----

// A project with every field away from its default, so a field the reader
// forgets shows as a difference when the loaded project is written again
static VideoFXProject RichTimeline(const std::string& dir) {
    VideoFXSegment clip = VideoFXSegment::FromFile(dir + "/clips/beach.mp4", 2.5, 9.75);
    clip.speed = 1.5;
    clip.mute = true;
    clip.effects = {VideoFXEffect::Brightness(0.25), VideoFXEffect::Crop(10, 20, 640, 360),
                    VideoFXEffect::LUT(dir + "/looks/warm.cube", 0.7), VideoFXEffect::FadeOut(1.25)};
    VideoFXOverlay title = VideoFXOverlay::Text("Sommer \xC3\xBC 2026\nZweite Zeile", VideoFXAnchor::Custom, 0.08);
    title.fontPath = "/fonts/Brand.ttf";             // outside the project: stays absolute
    title.textColor = 0x12AB9F;
    title.shadow = false;
    title.box = true;
    title.boxColor = 0x203040;
    title.boxOpacity = 0.4;
    title.x = 0.1; title.y = 0.2; title.opacity = 0.9;
    title.start = 0.5; title.end = 4.0; title.fadeIn = 0.3; title.fadeOut = 0.6;
    VideoFXOverlay logo = VideoFXOverlay::Image(dir + "/logo.png", VideoFXAnchor::TopRight, 0.15);
    logo.margin = 0.03;
    VideoFXOverlay pip = VideoFXOverlay::Video(dir + "/clips/cam.mp4", VideoFXAnchor::BottomLeft, 0.3, 1.5);
    pip.start = 2.0; pip.end = 6.0; pip.fadeOut = 0.4;
    clip.overlays = {title, logo, pip};
    VideoFXSegment photo = VideoFXSegment::FromImage(dir + "/photos/a.jpg", 4.5,
                                                     VideoFXImageMotion::Custom(1.1, 0.2, 0.3, 1.6, 0.7, 0.8));
    photo.motion.easeInOut = false;
    photo.imageFit = VideoFXImageFit::BlurredBackground;
    photo.keepInView = {VideoFXRect::Make(0.1, 0.2, 0.15, 0.2), VideoFXRect::Make(0.6, 0.25, 0.1, 0.12)};
    photo.keepFacesInView = true;
    photo.transitionIn = VideoFXTransition::Make(VideoFXTransitionType::DiagonalBottomRight, 0.75);
    VideoFXSegment card = VideoFXSegment::SolidColor(0xFF8800, 2.0);
    card.transitionIn = VideoFXTransition::Make(VideoFXTransitionType::FadeThroughWhite, 0.5);
    VideoFXSegment pattern = VideoFXSegment::TestPattern(1.5);

    VideoFXExportSettings settings = VideoFXExportSettings::WebM(720);
    settings.width = 1280;
    settings.frameRate = 29.97;
    settings.fitMode = VideoFXFitMode::Fill;
    settings.quality = 77;
    settings.videoBitRate = 4000000;
    settings.encoderPreset = "slow";
    settings.keyframeInterval = 1.5;
    settings.acceleration = VideoFXAcceleration::QuickSync;
    settings.sampleRate = 44100;
    settings.channels = 1;
    settings.audioBitRate = 96000;
    settings.threads = 3;
    settings.music = VideoFXMusic::FromFile(dir + "/music/one.mp3", 0.6);
    settings.music.playlist = {dir + "/music/two.flac", "/library/three.ogg"};
    settings.music.crossfade = 4.5;
    settings.music.start = 12.0;
    settings.music.loop = false;
    settings.music.fadeIn = 2.0;
    settings.music.fadeOut = 3.0;
    settings.music.duckingLevel = 0.2;
    settings.music.SetDuckingPreset(VideoFXDuckingPreset::LoudEvent);
    VideoFXAudioClip voice = VideoFXAudioClip::VoiceOver(dir + "/sound/intro.wav", 1.25, 0.8);
    voice.start = 0.5; voice.end = 7.5; voice.fadeIn = 0.2; voice.fadeOut = 0.3;
    settings.audioClips = {voice, VideoFXAudioClip::Sound("/library/whoosh.wav", 9.0, 1.5)};

    VideoFXProject p = VideoFXProject::FromTimeline({clip, photo, card, pattern}, settings);
    p.title = "Ferien \xE2\x80\x93 Sommer";
    p.outputPath = dir + "/out/holiday.webm";
    return p;
}

static VideoFXProject RichSlideshow(const std::string& dir) {
    VideoFXSlideshowOptions o;
    o.secondsPerImage = 3.25;
    o.transition = VideoFXTransition::Make(VideoFXTransitionType::SlideLeft, 0.6);
    o.motion = VideoFXImageMotion::Make(VideoFXMotionStyle::ZoomOut);
    o.imageFit = VideoFXImageFit::Contain;
    o.captions = {"Arrival", "", "Old town"};
    o.fadeInOut = false;
    o.music = VideoFXMusic::FromFiles({dir + "/a.mp3", dir + "/b.mp3"}, 0.9);
    o.matchMusicLength = true;
    o.beatSync = true;
    o.beatsPerImage = 8;
    o.keepInView = {{VideoFXRect::Make(0.4, 0.1, 0.2, 0.25)}, {}, {VideoFXRect::Make(0.1, 0.1, 0.1, 0.1)}};
    o.keepFacesInView = false;
    VideoFXExportSettings settings;
    settings.container = VideoFXContainer::MKV;
    settings.videoCodec = VideoFXVideoCodec::FFV1;
    settings.audioCodec = VideoFXAudioCodec::FLAC;
    VideoFXProject p = VideoFXProject::FromSlideshow({dir + "/p/1.jpg", dir + "/p/2.jpg", "/elsewhere/3.png"}, o,
                                                     settings);
    p.title = "Trip";
    return p;
}

static void TestProjectFiles() {
    std::printf("Project files: round trip, paths, versions, errors\n");
    namespace stdfs = std::filesystem;
    const std::string dir = "/work/holiday";        // nothing is read: the paths are only text here
    std::string json, again, error;

    // ---- every field survives a save and a load ----
    const VideoFXProject rich = RichTimeline(dir);
    CHECK(VideoFX_ProjectToJson(rich, json, dir) == VideoFXResult::Ok, "a rich timeline written");
    VideoFXProject loaded;
    CHECK(VideoFX_ProjectFromJson(json, loaded, dir) == VideoFXResult::Ok, "and read back");
    CHECK(VideoFX_ProjectToJson(loaded, again, dir) == VideoFXResult::Ok && again == json,
          "written again it is the same text: every field read");
    CHECK(loaded.segments.size() == 4 && loaded.title == rich.title, "four segments, the UTF-8 title");
    if (loaded.segments.size() == 4) {
        const VideoFXSegment& c = loaded.segments[0];
        CHECK(c.path == dir + "/clips/beach.mp4" && Near(c.start, 2.5, 1e-12) && Near(c.end, 9.75, 1e-12) &&
              Near(c.speed, 1.5, 1e-12) && c.mute, "the clip: path back absolute, range, speed, mute");
        CHECK(c.effects.size() == 4 && c.effects[1].type == VideoFXEffectType::Crop && c.effects[1].width == 640 &&
              c.effects[2].path == dir + "/looks/warm.cube", "effects: a crop's rectangle, a LUT's file");
        CHECK(c.overlays.size() == 3 && c.overlays[0].text == rich.segments[0].overlays[0].text &&
              c.overlays[0].textColor == 0x12AB9F && c.overlays[0].fontPath == "/fonts/Brand.ttf" &&
              c.overlays[1].imagePath == dir + "/logo.png", "overlays: text, colour, font, image");
        CHECK(c.overlays.size() == 3 && c.overlays[2].kind == VideoFXOverlayKind::Video &&
              c.overlays[2].videoPath == dir + "/clips/cam.mp4" && Near(c.overlays[2].videoStart, 1.5, 1e-12) &&
              Near(c.overlays[2].imageHeight, 0.3, 1e-12) && Near(c.overlays[2].end, 6.0, 1e-12),
              "a video overlay: its file (relative in the text), start, size, window");
        CHECK(json.find("\"video\": \"clips/cam.mp4\"") != std::string::npos, "the overlay's file stored relative to the project");
        const VideoFXSegment& ph = loaded.segments[1];
        CHECK(ph.kind == VideoFXSourceKind::Image && ph.motion.style == VideoFXMotionStyle::Custom &&
              Near(ph.motion.endZoom, 1.6, 1e-12) && !ph.motion.easeInOut &&
              ph.imageFit == VideoFXImageFit::BlurredBackground && ph.keepInView.size() == 2 &&
              ph.keepFacesInView && ph.transitionIn.type == VideoFXTransitionType::DiagonalBottomRight,
              "the photo: motion, framing, regions, faces, transition");
        CHECK(loaded.segments[2].color == 0xFF8800 && loaded.segments[3].kind == VideoFXSourceKind::TestPattern,
              "colour card and test pattern");
    }
    const VideoFXExportSettings& st = loaded.settings;
    CHECK(st.container == VideoFXContainer::WebM && st.videoCodec == VideoFXVideoCodec::VP9 && st.width == 1280 &&
          Near(st.frameRate, 29.97, 1e-12) && st.fitMode == VideoFXFitMode::Fill && st.quality == 77 &&
          st.videoBitRate == 4000000 && st.encoderPreset == "slow" && st.channels == 1 && st.threads == 3 &&
          Near(st.keyframeInterval, 1.5, 1e-12) && st.acceleration == VideoFXAcceleration::QuickSync &&
          st.audioClips.size() == 2 && st.audioClips[0].path == dir + "/sound/intro.wav" && st.audioClips[0].ducksMusic &&
          Near(st.audioClips[0].at, 1.25, 1e-12) && Near(st.audioClips[0].end, 7.5, 1e-12) &&
          Near(st.audioClips[0].fadeOut, 0.3, 1e-12) && !st.audioClips[1].ducksMusic &&
          st.audioClips[1].path == "/library/whoosh.wav" && Near(st.audioClips[1].volume, 1.5, 1e-12),
          "export settings");
    CHECK(st.music.Songs().size() == 3 && Near(st.music.crossfade, 4.5, 1e-12) && !st.music.loop &&
          Near(st.music.duckingThresholdDb, -15.0, 1e-12) && Near(st.music.duckingLevel, 0.2, 1e-12),
          "music: the song list, crossfade, loop, ducking");
    CHECK(loaded.outputPath == dir + "/out/holiday.webm", "the output file");

    const VideoFXProject show = RichSlideshow(dir);
    CHECK(VideoFX_ProjectToJson(show, json, dir) == VideoFXResult::Ok &&
          VideoFX_ProjectFromJson(json, loaded, dir) == VideoFXResult::Ok &&
          VideoFX_ProjectToJson(loaded, again, dir) == VideoFXResult::Ok && again == json,
          "a rich slideshow: the same text after a round trip");
    CHECK(loaded.kind == VideoFXProjectKind::Slideshow && loaded.images.size() == 3 &&
          loaded.slideshow.captions.size() == 3 && loaded.slideshow.keepInView.size() == 3 &&
          loaded.slideshow.beatsPerImage == 8 && !loaded.slideshow.keepFacesInView &&
          loaded.slideshow.music.playlist.size() == 2 && loaded.settings.videoCodec == VideoFXVideoCodec::FFV1,
          "images, captions, regions, beats, faces off, songs, codecs");

    // ---- paths: inside the project relative, outside absolute ----
    CHECK(json.find("\"p/1.jpg\"") != std::string::npos && json.find("\"/elsewhere/3.png\"") != std::string::npos,
          "media in the project's folder relative, others absolute");
    CHECK(VideoFX_ProjectFromJson(json, loaded, "/moved/there") == VideoFXResult::Ok &&
          loaded.images[0] == "/moved/there/p/1.jpg" && loaded.images[2] == "/elsewhere/3.png",
          "a moved project finds its own media where it now is");
    CHECK(VideoFX_ProjectToJson(show, json) == VideoFXResult::Ok && json.find("\"" + dir + "/p/1.jpg\"") != std::string::npos,
          "no base directory: paths kept as given");

    // ---- tolerant of what it does not know, strict about what it cannot read ----
    const std::string minimal = R"({"format": "VideoFX project", "version": 1, "kind": "timeline",
        "futureKey": {"anything": [1, 2]},
        "segments": [{"kind": "color", "duration": 2.0, "color": "#00FF00", "newField": true}]})";
    CHECK(VideoFX_ProjectFromJson(minimal, loaded) == VideoFXResult::Ok && loaded.segments.size() == 1 &&
          loaded.segments[0].color == 0x00FF00 && Near(loaded.segments[0].speed, 1.0, 1e-12) &&
          loaded.settings.quality == -1, "unknown keys ignored, missing ones at their defaults");
    CHECK(VideoFX_ProjectFromJson(R"({"format": "VideoFX project", "version": 99})", loaded) ==
              VideoFXResult::InvalidArgument && VideoFX_GetLastError().find("newer VideoFX") != std::string::npos,
          "a newer format refused, and says so");
    CHECK(VideoFX_ProjectFromJson(R"({"format": "Something else", "version": 1})", loaded) ==
              VideoFXResult::InvalidArgument, "another program's JSON refused");
    CHECK(VideoFX_ProjectFromJson("{\"format\": \"VideoFX project\", ", loaded) == VideoFXResult::InvalidArgument,
          "broken JSON refused");
    CHECK(VideoFX_ProjectFromJson(R"({"format": "VideoFX project", "version": 1,
              "segments": [{"kind": "color", "transitionIn": {"type": "spin"}}]})", loaded) ==
              VideoFXResult::InvalidArgument &&
              VideoFX_GetLastError().find("segments[0].transitionIn.type") != std::string::npos,
          "an unknown transition refused, naming where");
    CHECK(VideoFX_ProjectFromJson(R"({"format": "VideoFX project", "version": 1,
              "segments": [{"kind": "color", "duration": "long"}]})", loaded) == VideoFXResult::InvalidArgument,
          "text where a number belongs refused");
    VideoFXProject memory = VideoFXProject::FromTimeline({VideoFXSegment::FromImageFrame(SolidFrame(8, 8, 1, 2, 3), 1.0)});
    CHECK(VideoFX_ProjectToJson(memory, json) == VideoFXResult::InvalidArgument, "a photo held only in memory refused");

    // ---- files: written whole, read with missing media reported ----
    const stdfs::path tmp = stdfs::temp_directory_path() /
                            ("videofx-project-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    stdfs::create_directories(tmp / "photos");
    const std::string base = UltraCanvas::PathToUtf8(tmp);
    { std::ofstream(tmp / "photos" / "1.jpg") << "x"; }
    VideoFXProject onDisk = VideoFXProject::FromSlideshow({base + "/photos/1.jpg", base + "/photos/2.jpg"});
    const std::string file = base + "/Ferien \xC3\x9C.vfxproj";
    CHECK(VideoFX_SaveProject(onDisk, file) == VideoFXResult::Ok, "saved to a UTF-8 file name");
    CHECK(!stdfs::exists(UltraCanvas::PathFromUtf8(file + ".saving")), "no half-written file left beside it");
    std::vector<std::string> missing;
    CHECK(VideoFX_LoadProject(file, loaded, &missing) == VideoFXResult::Ok && loaded.images.size() == 2,
          "loaded back");
    CHECK(missing.size() == 1 && missing[0] == base + "/photos/2.jpg", "the one missing photo reported");
    CHECK(VideoFX_LoadProject(base + "/none.vfxproj", loaded) == VideoFXResult::FileNotFound, "no such project");
    CHECK(VideoFX_SaveProject(onDisk, base + "/no/such/dir/x.vfxproj") == VideoFXResult::WriteError,
          "an unwritable place refused");
    std::error_code ec;
    stdfs::remove_all(tmp, ec);
}

// Proxies without the engine: names, what may be cleared, which clips, the
// crop, and switching a timeline to proxies that exist
static void TestProxyFiles() {
    std::printf("Proxies: names, cache folder, switching, clearing\n");
    namespace stdfs = std::filesystem;
    VideoFXProxySettings ps;

    // ---- the name: readable stem, a hash of everything that matters ----
    const std::string a = ProxyFileName("/v/Trip 2026 (final).MOV", 1000, 42, ps);
    CHECK(a.rfind("Trip_2026_final-", 0) == 0 && a.size() == std::string("Trip_2026_final-").size() + 16 + 4 &&
          a.compare(a.size() - 4, 4, ".mp4") == 0, "\"Trip_2026_final-<16 hex>.mp4\"");
    CHECK(ProxyFileName("/v/Trip 2026 (final).MOV", 1000, 42, ps) == a, "the same clip, the same name");
    VideoFXProxySettings taller = ps;
    taller.height = 720;
    VideoFXProxySettings sharper = ps;
    sharper.quality = 80;
    VideoFXProxySettings sparser = ps;
    sparser.keyframeInterval = 2.0;
    VideoFXProxySettings elsewhere = ps;
    elsewhere.cacheDirectory = "/tmp/somewhere";
    CHECK(ProxyFileName("/v/Trip 2026 (final).MOV", 1001, 42, ps) != a &&
          ProxyFileName("/v/Trip 2026 (final).MOV", 1000, 43, ps) != a &&
          ProxyFileName("/w/Trip 2026 (final).MOV", 1000, 42, ps) != a,
          "a new name when the clip's size, time or folder differs");
    CHECK(ProxyFileName("/v/Trip 2026 (final).MOV", 1000, 42, taller) != a &&
          ProxyFileName("/v/Trip 2026 (final).MOV", 1000, 42, sharper) != a &&
          ProxyFileName("/v/Trip 2026 (final).MOV", 1000, 42, sparser) != a,
          "and when the height, quality or keyframe interval does");
    CHECK(ProxyFileName("/v/Trip 2026 (final).MOV", 1000, 42, elsewhere) == a, "the folder is not part of the name");
    CHECK(ProxyFileName("/v/\xC3\x9C" "ber-See.mp4", 1, 1, ps).rfind("ber-See-", 0) == 0, "UTF-8 letters become '_', trimmed");
    CHECK(ProxyFileName("/v/\xE6\x97\xA5\xE6\x9C\xAC.mp4", 1, 1, ps).rfind("clip-", 0) == 0, "nothing ASCII left: \"clip\"");
    CHECK(ProxyFileName("C:\\Videos\\Day One.mov", 1, 1, ps).rfind("Day_One-", 0) == 0, "Windows separators");
    CHECK(ProxyFileName("/v/" + std::string(90, 'x') + ".mp4", 1, 1, ps).size() == 40 + 1 + 16 + 4, "a long stem is cut to 40");

    // ---- what Clear may delete ----
    CHECK(IsProxyFileName(a), "its own names are proxies");
    const std::string part = a.substr(0, a.size() - 4) + ".0badc0de.part.mp4";
    CHECK(IsProxyFileName(part), "and its half-made ones");
    CHECK(!IsProxyFileName("holiday.mp4") && !IsProxyFileName("x-0123456789ABCDEF.mp4") &&
          !IsProxyFileName("x-0123456789abcdef.mov") && !IsProxyFileName(a.substr(0, a.size() - 4) + ".xyz.part.mp4") &&
          !IsProxyFileName("notes.txt"), "nothing else");

    // ---- settings ----
    std::string error;
    VideoFXProxySettings bad = ps;
    bad.height = 8;
    CHECK(!ValidateProxySettings(bad, error) && !error.empty(), "a height under 16 refused");
    bad = ps;
    bad.quality = 101;
    CHECK(!ValidateProxySettings(bad, error), "quality over 100 refused");
    bad = ps;
    bad.keyframeInterval = -1.0;
    CHECK(!ValidateProxySettings(bad, error), "a negative keyframe interval refused");
    CHECK(ValidateProxySettings(ps, error), "the defaults are valid");

    // ---- which clips a timeline plays ----
    VideoFXSegment one = VideoFXSegment::FromFile("/v/a.mp4");
    VideoFXSegment again = VideoFXSegment::FromFile("/v/a.mp4", 2.0, 3.0);
    again.overlays = {VideoFXOverlay::Video("/v/pip.mp4", VideoFXAnchor::TopRight, 0.3),
                      VideoFXOverlay::Video("/v/own-size.mp4", VideoFXAnchor::TopRight, 0.0)};
    const std::vector<std::string> clips =
        ProxyableClips({one, VideoFXSegment::FromImage("/v/photo.jpg"), again, VideoFXSegment::SolidColor(0, 1.0)});
    CHECK(clips.size() == 2 && clips[0] == "/v/a.mp4" && clips[1] == "/v/pip.mp4",
          "each clip once; no photos, colours or own-size picture-in-picture");

    // ---- the crop, in the proxy's pixels ----
    const VideoFXEffect half = ScaleCrop(VideoFXEffect::Crop(480, 270, 960, 540), 0.5, 0.5, 960, 540);
    CHECK(half.x == 240 && half.y == 135 && half.width == 480 && half.height == 270, "a crop of a 1080p clip on its 540p proxy");
    const VideoFXEffect edge = ScaleCrop(VideoFXEffect::Crop(1900, 1070, 20, 10), 0.25, 0.25, 480, 270);
    CHECK(edge.x + edge.width <= 480 && edge.y + edge.height <= 270 && edge.width >= 1 && edge.height >= 1,
          "a crop at the edge stays inside, never empty");

    // ---- on disk: paths, switching, clearing ----
    const stdfs::path dir = stdfs::temp_directory_path() /
        ("videofx-proxy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    stdfs::create_directories(dir / "cache");
    ps.cacheDirectory = UltraCanvas::PathToUtf8(dir / "cache");
    const std::string clip = UltraCanvas::PathToUtf8(dir / UltraCanvas::PathFromUtf8("Reise \xC3\xBC.mp4"));
    const std::string pipClip = UltraCanvas::PathToUtf8(dir / "pip.mp4");
    for (const std::string& f : {clip, pipClip}) {
        std::ofstream(UltraCanvas::PathFromUtf8(f), std::ios::binary) << "not really a video";
    }
    CHECK(ProxyPathFor(UltraCanvas::PathToUtf8(dir / "gone.mp4"), ps).empty(), "no proxy path for a missing clip");
    const std::string proxy = VideoFX_ProxyPath(clip, ps);
    CHECK(!proxy.empty() && UltraCanvas::PathFromUtf8(proxy).parent_path() == dir / "cache", "the proxy goes in the cache folder");
    CHECK(!VideoFX_HasProxy(clip, ps), "not made yet");
    VideoFXSegment withCrop = VideoFXSegment::FromFile(clip);
    withCrop.effects = {VideoFXEffect::Crop(0, 0, 4, 4)};
    VideoFXSegment withPip = VideoFXSegment::FromFile(clip, 1.0, 2.0);
    withPip.overlays = {VideoFXOverlay::Video(pipClip, VideoFXAnchor::TopRight, 0.3),
                        VideoFXOverlay::Video(pipClip, VideoFXAnchor::TopLeft, 0.0)};
    const std::vector<VideoFXSegment> timeline = {VideoFXSegment::FromFile(clip), withPip, withCrop};
    int replaced = -1;
    std::vector<VideoFXSegment> used = VideoFX_UseProxies(timeline, ps, &replaced);
    CHECK(replaced == 0 && used.size() == 3 && used[0].path == clip, "without proxies the timeline is unchanged");

    // a stand-in for a made proxy: Use switches by the file being there
    std::ofstream(UltraCanvas::PathFromUtf8(proxy), std::ios::binary) << "proxy";
    std::ofstream(UltraCanvas::PathFromUtf8(VideoFX_ProxyPath(pipClip, ps)), std::ios::binary) << "proxy";
    CHECK(VideoFX_HasProxy(clip, ps), "made: HasProxy");
    used = VideoFX_UseProxies(timeline, ps, &replaced);
    CHECK(used.size() == 3 && used[0].path == proxy && used[1].path == proxy, "file segments switched to the proxy");
    CHECK(used.size() == 3 && used[1].start == 1.0 && used[1].end == 2.0, "their trim kept: proxies keep the clip's timing");
    CHECK(used.size() == 3 && used[1].overlays.size() == 2 && used[1].overlays[0].videoPath == VideoFX_ProxyPath(pipClip, ps) &&
          used[1].overlays[1].videoPath == pipClip, "a sized picture-in-picture switched, an own-size one kept");
    CHECK(used.size() == 3 && used[2].path == clip, "a cropped clip whose sizes cannot be read stays the original");
    CHECK(replaced == 3, "three switched");

    // changing the clip moves its proxy name: the old proxy is no longer used
    std::ofstream(UltraCanvas::PathFromUtf8(clip), std::ios::binary | std::ios::app) << " - edited";
    CHECK(VideoFX_ProxyPath(clip, ps) != proxy && !VideoFX_HasProxy(clip, ps), "an edited clip needs a new proxy");

    // clearing: proxies and half-made ones go, nothing else
    std::ofstream(dir / "cache" / UltraCanvas::PathFromUtf8(part), std::ios::binary) << "half";
    std::ofstream(dir / "cache" / "notes.txt") << "keep me";
    std::ofstream(dir / "cache" / "holiday.mp4", std::ios::binary) << "keep me too";
    int files = 0;
    uint64_t bytes = 0;
    CHECK_OK(VideoFX_ClearProxies(ps, &files, &bytes), "clear the cache");
    CHECK(files == 3 && bytes == 5 + 5 + 4, "two proxies and a half-made one, and their bytes");
    CHECK(stdfs::exists(dir / "cache" / "notes.txt") && stdfs::exists(dir / "cache" / "holiday.mp4") &&
          !stdfs::exists(UltraCanvas::PathFromUtf8(proxy)), "other files untouched");
    VideoFXProxySettings none = ps;
    none.cacheDirectory = UltraCanvas::PathToUtf8(dir / "never-made");
    CHECK_OK(VideoFX_ClearProxies(none, &files), "clearing a folder that does not exist");
    CHECK(files == 0, "removes nothing");
#ifndef _WIN32
    // the default folder follows XDG_CACHE_HOME
    const std::string xdg = UltraCanvas::PathToUtf8(dir / "xdg");
    ::setenv("XDG_CACHE_HOME", xdg.c_str(), 1);
#ifdef __APPLE__
    CHECK(VideoFX_GetDefaultProxyDirectory().find("/Library/Caches/VideoFX/Proxies") != std::string::npos,
          "the default: ~/Library/Caches/VideoFX/Proxies");
#else
    CHECK(VideoFX_GetDefaultProxyDirectory() == xdg + "/VideoFX/Proxies", "the default: $XDG_CACHE_HOME/VideoFX/Proxies");
#endif
    ::unsetenv("XDG_CACHE_HOME");
#endif
#ifndef VIDEOFX_HAS_FFMPEG
    std::string editPath;
    CHECK(VideoFX_CreateProxy(clip, ps, {}, &editPath) == VideoFXResult::NotAvailable,
          "without the engine a proxy cannot be made");
#endif
    std::error_code ec;
    stdfs::remove_all(dir, ec);
}

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
static void TestPreview(const VideoFXExportSettings& base);
static void TestProxies(const VideoFXExportSettings& base);
static void TestEncoders(const VideoFXExportSettings& base);
static void TestAudioClips(const VideoFXExportSettings& base);
static void TestFileFormats();

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
    TestPreview(base);
    TestProxies(base);
    TestEncoders(base);
    TestAudioClips(base);
    TestFileFormats();

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

    // ---- picture-in-picture: a video overlay on a card, a photo and a file ----
    {
        const std::string greenFile = TempPath("pip-green.mkv"), redFile = TempPath("pip-red.mkv");
        CHECK_OK(VideoFX_Export({VideoFXSegment::SolidColor(0x00FF00, 3.0)}, greenFile, s), "a green clip to lay over");
        CHECK_OK(VideoFX_Export({VideoFXSegment::SolidColor(0xFF0000, 1.0)}, redFile, s), "a red clip, 1 s");
        VideoFXOverlay pip = VideoFXOverlay::Video(greenFile, VideoFXAnchor::TopLeft, 0.25);   // 40 x 30 px
        pip.margin = 0.0;
        pip.start = 0.5;
        VideoFXSegment blueCard = VideoFXSegment::SolidColor(0x0000FF, 2.0);
        blueCard.overlays = {pip};
        const std::string onCard = TempPath("pip-card.mkv");
        CHECK_OK(VideoFX_Export({blueCard}, onCard, s), "video overlay on a card");
        CHECK_OK(VideoFX_Probe(onCard, info), "probe it");
        CHECK(Near(info.duration, 2.0, 0.1), "the card's own length, not the overlay file's");
        VideoFX_ExtractFrame(onCard, 0.2, f);
        PixelAt(f, 10, 10, pr, pg, pb);
        CHECK(pb > 200 && pg < 60, "before its start: the card");
        VideoFX_ExtractFrame(onCard, 1.5, f);
        PixelAt(f, 10, 10, pr, pg, pb);
        CHECK(pg > 200 && pb < 60, "then the overlay's picture, top left, a quarter high");
        PixelAt(f, 45, 10, pr, pg, pb);
        CHECK(pb > 200 && pg < 60, "and nothing beyond its 40 px");
        PixelAt(f, 100, 100, pr, pg, pb);
        CHECK(pb > 200 && pg < 60, "the rest of the frame untouched");

        VideoFXSegment photo = VideoFXSegment::FromImageFrame(SolidFrame(160, 120, 0, 0, 255), 2.0);
        photo.motion = VideoFXImageMotion::Make(VideoFXMotionStyle::Still);
        photo.overlays = {pip};
        const std::string onPhoto = TempPath("pip-photo.mkv");
        CHECK_OK(VideoFX_Export({photo}, onPhoto, s), "video overlay on a photo");
        VideoFX_ExtractFrame(onPhoto, 1.5, f);
        PixelAt(f, 10, 10, pr, pg, pb);
        CHECK(pg > 200 && pb < 60, "over the photo too");

        // on a file, from 0.5 s into a 1 s clip: it runs out after 0.5 s and its last frame holds
        VideoFXOverlay late = VideoFXOverlay::Video(redFile, VideoFXAnchor::BottomRight, 0.5, 0.5);
        late.margin = 0.0;
        VideoFXSegment onFile = VideoFXSegment::FromFile(greenFile, 0.0, 2.0);
        onFile.overlays = {late};
        const std::string fileOut = TempPath("pip-file.mkv");
        CHECK_OK(VideoFX_Export({onFile}, fileOut, s), "video overlay on a file");
        VideoFX_ExtractFrame(fileOut, 0.2, f);
        PixelAt(f, f.width - 5, f.height - 5, pr, pg, pb);
        CHECK(pr > 200 && pg < 60, "the red clip from its half-second, bottom right");
        PixelAt(f, 5, 5, pr, pg, pb);
        CHECK(pg > 200 && pr < 60, "the green file behind it");
        VideoFX_ExtractFrame(fileOut, 1.8, f);
        PixelAt(f, f.width - 5, f.height - 5, pr, pg, pb);
        CHECK(pr > 200 && pg < 60, "long after the clip ran out: its last frame holds");

        VideoFXOverlay faded = pip;
        faded.start = 0.0;
        faded.end = 1.0;
        faded.fadeIn = 1.0;
        blueCard.overlays = {faded};
        const std::string fadeOut = TempPath("pip-fade.mkv");
        CHECK_OK(VideoFX_Export({blueCard}, fadeOut, s), "video overlay fading in");
        VideoFX_ExtractFrame(fadeOut, 0.12, f);
        PixelAt(f, 10, 10, pr, pg, pb);
        CHECK(pg < 100 && pb > 150, "barely there at the start of the fade");
        VideoFX_ExtractFrame(fadeOut, 1.5, f);
        PixelAt(f, 10, 10, pr, pg, pb);
        CHECK(pb > 200 && pg < 60, "gone after its end");

        VideoFXOverlay missing = VideoFXOverlay::Video(TempPath("nowhere.mkv"));
        blueCard.overlays = {missing};
        CHECK(VideoFX_Export({blueCard}, TempPath("pip-missing.mkv"), s) != VideoFXResult::Ok &&
              VideoFX_GetLastError().find("nowhere.mkv") != std::string::npos, "a missing overlay file fails before anything is written");
    }

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

    // ---- faces kept in shot: a tall photo with a red "face" near its top ----
    VideoFXFrame person = SolidFrame(120, 480, 90, 90, 90);
    for (int yy = 24; yy < 58; ++yy)
        for (int xx = 48; xx < 72; ++xx) {
            uint8_t* p = &person.pixels[(static_cast<size_t>(yy) * 120 + xx) * 4];
            p[0] = 230; p[1] = 20; p[2] = 20;
        }
    const VideoFXRect faceBox = VideoFXRect::FromPixels(48, 24, 24, 34, 120, 480);
    auto redPixels = [](const std::string& file, double t) {
        VideoFXFrame fr;
        if (VideoFX_ExtractFrame(file, t, fr) != VideoFXResult::Ok) return -1;
        int n = 0;
        for (size_t i = 0; i + 3 < fr.pixels.size(); i += 4)
            if (fr.pixels[i] > 170 && fr.pixels[i + 1] < 90 && fr.pixels[i + 2] < 90) ++n;
        return n;
    };
    VideoFXSegment pan = VideoFXSegment::FromImageFrame(person, 2.0,
                                                        VideoFXImageMotion::Custom(1.2, 0.5, 0.0, 1.2, 0.5, 1.0));
    pan.imageFit = VideoFXImageFit::Cover;
    const std::string lost = TempPath("face-lost.mkv");
    CHECK_OK(VideoFX_Export({pan}, lost, s), "a pan down a tall photo");
    CHECK(redPixels(lost, 0.05) > 20 && redPixels(lost, 1.9) == 0, "without: the face scrolls out of shot");
    pan.keepInView = {faceBox};
    const std::string kept = TempPath("face-kept.mkv");
    CHECK_OK(VideoFX_Export({pan}, kept, s), "the same, keeping the face in view");
    CHECK(redPixels(kept, 0.05) > 20 && redPixels(kept, 1.0) > 20 && redPixels(kept, 1.9) > 20,
          "with: the face in shot at the start, middle and end");
    VideoFXSegment closer = VideoFXSegment::FromImageFrame(person, 2.0,
                                                           VideoFXImageMotion::Custom(1.0, 0.5, 0.5, 3.0, 0.5, 0.9));
    closer.imageFit = VideoFXImageFit::Cover;
    closer.keepInView = {faceBox};
    const std::string zoomed = TempPath("face-zoom.mkv");
    CHECK_OK(VideoFX_Export({closer}, zoomed, s), "a zoom aimed away from the face");
    CHECK(redPixels(zoomed, 1.9) > 20, "ends on the face instead");

    const std::string personPng = TempPath("person.png");
    CHECK_OK(VideoFX_SaveFrameImage(person, personPng), "save the tall photo");
    VideoFXSlideshowOptions faces;
    faces.transition = VideoFXTransition::Make(VideoFXTransitionType::Cut);
    faces.secondsPerImage = 1.5;
    faces.imageFit = VideoFXImageFit::Cover;
    faces.motion = VideoFXImageMotion::Make(VideoFXMotionStyle::PanDown);
    faces.fadeInOut = false;
    int asked = 0;
    faces.findKeepInView = [&](const VideoFXFrame& shown, size_t) {
        ++asked;
        return std::vector<VideoFXRect>{VideoFXRect::FromPixels(48, 24, 24, 34, shown.width, shown.height)};
    };
    const std::string faceShow = TempPath("face-show.mkv");
    CHECK_OK(VideoFX_CreateSlideshow({personPng, personPng}, faceShow, faces, s), "slideshow with a face finder");
    CHECK(asked == 2, "the finder asked once per image");
    CHECK(redPixels(faceShow, 1.4) > 20 && redPixels(faceShow, 2.9) > 20, "each image ends with its face in shot");
    VideoFXSegment outside = VideoFXSegment::FromImageFrame(person, 1.0);
    outside.keepInView = {VideoFXRect::Make(0.9, 0.1, 0.2, 0.1)};
    CHECK(VideoFX_Export({outside}, TempPath("x.mkv"), s) == VideoFXResult::InvalidArgument,
          "a region past the image edge refused");

    // ---- the built-in detector frames a real face ----
    {
        VideoFXFrame astronaut;
        if (ReadPpm(kAstronaut, astronaut)) {
            VideoFXExportSettings big = s;
            big.width = 320;
            big.height = 240;
            auto facesAt = [](const std::string& file, double t) {
                VideoFXFrame fr;
                std::vector<VideoFXRect> found;
                if (VideoFX_ExtractFrame(file, t, fr) == VideoFXResult::Ok) VideoFX_DetectFaces(fr, found);
                return found.size();
            };
            // Zooming into the suit, bottom right: the face leaves the frame
            VideoFXSegment away = VideoFXSegment::FromImageFrame(
                astronaut, 2.0, VideoFXImageMotion::Custom(1.0, 0.5, 0.5, 2.0, 0.8, 0.85));
            const std::string suit = TempPath("astro-suit.mkv");
            CHECK_OK(VideoFX_Export({away}, suit, big), "a zoom into the suit");
            CHECK(facesAt(suit, 0.05) == 1 && facesAt(suit, 1.95) == 0, "without: no face in the last frame");
            away.keepFacesInView = true;
            const std::string framed = TempPath("astro-framed.mkv");
            CHECK_OK(VideoFX_Export({away}, framed, big), "the same, the built-in detector framing the face");
            CHECK(facesAt(framed, 1.0) == 1 && facesAt(framed, 1.95) == 1, "with: the face in the middle and last frames");

            const std::string astroPng = TempPath("astronaut.png");
            CHECK_OK(VideoFX_SaveFrameImage(astronaut, astroPng), "save the photo");
            VideoFXSlideshowOptions show;
            show.transition = VideoFXTransition::Make(VideoFXTransitionType::Cut);
            show.secondsPerImage = 2.0;
            show.fadeInOut = false;
            show.motion = VideoFXImageMotion::Custom(1.0, 0.5, 0.5, 2.0, 0.8, 0.85);
            const std::string withFaces = TempPath("astro-show.mkv");
            CHECK_OK(VideoFX_CreateSlideshow({astroPng}, withFaces, show, big), "a slideshow, no face code at all");
            CHECK(facesAt(withFaces, 1.95) == 1, "keeps the face in shot by default");
            show.keepFacesInView = false;
            const std::string plain = TempPath("astro-plain.mkv");
            CHECK_OK(VideoFX_CreateSlideshow({astroPng}, plain, show, big), "the same with keepFacesInView off");
            CHECK(facesAt(plain, 1.95) == 0, "plain motion: into the suit");
        }
    }

    // ---- a project renders as its timeline does ----
    {
        VideoFXProject project = VideoFXProject::FromTimeline(
            {VideoFXSegment::TestPattern(1.0), VideoFXSegment::SolidColor(0x0000FF, 1.0)}, s);
        project.segments[1].transitionIn = VideoFXTransition::Crossfade(0.5);
        CHECK(VideoFX_RenderProject(project) == VideoFXResult::InvalidArgument, "no output named: refused");
        const std::string projectFile = TempPath("timeline.vfxproj");
        project.outputPath = TempPath("from-project.mkv");
        CHECK_OK(VideoFX_SaveProject(project, projectFile), "save a timeline project");
        VideoFXProject back;
        CHECK_OK(VideoFX_LoadProject(projectFile, back), "load it");
        CHECK_OK(VideoFX_RenderProject(back), "render it to its own output");
        CHECK_OK(VideoFX_Probe(project.outputPath, info), "probe it");
        CHECK(Near(info.duration, 1.5, 0.05), "1 s + 1 s with a 0.5 s crossfade: 1.5 s");
    }

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

// 16-bit stereo WAV of a mono signal
static bool WriteWav(const std::string& path, const std::vector<float>& mono, int rate) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc(static_cast<int>((v >> (8 * i)) & 0xFF), f); };
    auto u16 = [&](uint16_t v) { std::fputc(v & 0xFF, f); std::fputc(v >> 8, f); };
    const uint32_t bytes = static_cast<uint32_t>(mono.size() * 4);
    std::fputs("RIFF", f); u32(36 + bytes); std::fputs("WAVEfmt ", f);
    u32(16); u16(1); u16(2); u32(static_cast<uint32_t>(rate)); u32(static_cast<uint32_t>(rate * 4)); u16(4); u16(16);
    std::fputs("data", f); u32(bytes);
    for (float v : mono) {
        const int16_t s16 = static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        u16(static_cast<uint16_t>(s16)); u16(static_cast<uint16_t>(s16));
    }
    return std::fclose(f) == 0;
}

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
    duck.music.crossfade = 0.0;             // steady music: no loop blend inside the 4 s
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

    // ---- song lists: a loud song A and a quiet song B, 2 s each ----
    const std::string songA = TempPath("songA.wav"), songB = TempPath("songB.wav");
    VideoFXSegment toneA = VideoFXSegment::TestPattern(2.0), toneB = VideoFXSegment::TestPattern(2.0);
    toneB.effects = {VideoFXEffect::Volume(0.25)};
    CHECK_OK(VideoFX_Export({toneA}, songA, VideoFXExportSettings::AudioOnlyWAV()), "make song A");
    CHECK_OK(VideoFX_Export({toneB}, songB, VideoFXExportSettings::AudioOnlyWAV()), "make song B, a quarter as loud");
    const std::vector<VideoFXSegment> six = {VideoFXSegment::SolidColor(0, 6.0)};
    VideoFXExportSettings songs = VideoFXExportSettings::AudioOnlyWAV();
    songs.music = VideoFXMusic::FromFiles({songA, songB}, 1.0);
    songs.music.fadeIn = songs.music.fadeOut = 0.0;
    songs.music.loop = false;
    songs.music.crossfade = 0.0;
    const std::string backToBack = TempPath("list-cut.wav");
    CHECK_OK(VideoFX_Export(six, backToBack, songs), "two songs back to back");
    w = ReadWav(backToBack);
    const double loudA = w.Rms(0.3, 1.7), quietB = w.Rms(2.3, 3.7);
    CHECK(loudA > 0.05 && Near(quietB, loudA * 0.25, loudA * 0.05), "A for 2 s, then B");
    CHECK(w.Rms(4.1, 5.9) < 0.001, "then silence: the list does not loop");

    songs.music.crossfade = 1.0;
    const std::string blended = TempPath("list-xfade.wav");
    CHECK_OK(VideoFX_Export(six, blended, songs), "the same with a 1 s crossfade");
    w = ReadWav(blended);
    CHECK(Near(w.Rms(0.2, 0.8), loudA, loudA * 0.05), "A alone before the crossfade");
    CHECK(Near(w.Rms(2.2, 2.8), quietB, quietB * 0.1), "B alone after it - 1 s earlier than back to back");
    CHECK(w.Rms(1.4, 1.6) < loudA * 0.95 && w.Rms(1.4, 1.6) > quietB, "in the crossfade: between the two");
    CHECK(w.Rms(3.1, 5.9) < 0.001 && w.Rms(2.7, 2.9) > 0.01, "3 s in all: 2 + 2 - 1");

    songs.music.crossfade = 0.0;
    songs.music.loop = true;
    const std::string around = TempPath("list-loop.wav");
    CHECK_OK(VideoFX_Export(six, around, songs), "the list, looping");
    w = ReadWav(around);
    CHECK(Near(w.Rms(4.3, 5.7), loudA, loudA * 0.05), "after B comes A again");

    VideoFXSlideshowOptions listShow;
    listShow.transition = VideoFXTransition::Crossfade(0.5);
    listShow.music = VideoFXMusic::FromFiles({songA, songB});
    listShow.music.crossfade = 1.0;
    listShow.matchMusicLength = true;
    const std::string listShowPath = TempPath("list-show.mkv");
    CHECK_OK(VideoFX_CreateSlideshow({photo, photo, photo}, listShowPath, listShow, small), "slideshow fitted to a list");
    CHECK_OK(VideoFX_Probe(listShowPath, info), "probe it");
    CHECK(Near(info.duration, 3.0, 0.1), "2 s + 2 s songs, 1 s crossfade: a 3 s slideshow");

    // ---- beats: detected in a file, and a slideshow changing on them ----
    const std::string click = TempPath("click120.wav");
    CHECK(WriteWav(click, ClickTrack(44100, 120.0, 0.25, 12.0), 44100), "write a 120 BPM click track");
    VideoFXBeatInfo beatInfo;
    CHECK_OK(VideoFX_DetectBeats(click, beatInfo), "detect its beats");
    CHECK(beatInfo.HasBeat() && Near(beatInfo.bpm, 120.0, 1.0), "120 BPM");
    CHECK(!beatInfo.beats.empty() && Near(beatInfo.beats.front(), 0.25, 0.02), "first beat at 0.25 s");
    CHECK_OK(VideoFX_DetectBeats(longSong, beatInfo), "detect the beats of a steady tone");
    CHECK(!beatInfo.HasBeat(), "a steady tone has no beat");
    CHECK(VideoFX_DetectBeats(TempPath("none.wav"), beatInfo) == VideoFXResult::FileNotFound, "missing file");

    // ---- waveform: the loudness envelope a timeline draws ----
    VideoFXWaveform wave;
    CHECK_OK(VideoFX_ExtractWaveform(click, wave, 10.0), "waveform of the click track, 10 blocks a second");
    CHECK(wave.IsValid() && wave.sampleRate == 44100 && wave.samplesPerBlock == 4410, "44.1 kHz, 4410 samples a block");
    CHECK(wave.totalBlocks == 120 && Near(wave.duration, 12.0, 0.001) && wave.start == 0.0, "12 s: 120 blocks");
    CHECK(wave.totalBlocks == 120 && wave.maxValues[0] == 0.0f && wave.minValues[0] == 0.0f && wave.rmsValues[0] == 0.0f,
          "the first tenth of a second is silent");
    CHECK(wave.totalBlocks == 120 && wave.maxValues[2] > 0.3f && wave.minValues[2] < -0.1f && wave.rmsValues[2] > 0.05f,
          "the click at 0.25 s is in block 2");
    CHECK(wave.totalBlocks == 120 && wave.maxValues[7] > 0.3f && wave.maxValues[8] == 0.0f,
          "the next click at 0.75 s is in block 7, and block 8 is silent again");
    CHECK(wave.Peak() > 0.3f && wave.Peak() <= 1.0f, "peak from the clicks, within 0..1");
    CHECK(wave.BlockAt(0.25) == 2 && wave.BlockAt(11.99) == 119 && wave.BlockAt(12.0) == -1, "BlockAt");
    CHECK_OK(VideoFX_ExtractWaveform(click, wave, 10.0, 1.0, 2.0), "seconds 1 to 2 only");
    CHECK(wave.totalBlocks == 10 && Near(wave.start, 1.0, 0.001) && Near(wave.duration, 1.0, 0.001), "10 blocks from 1 s");
    CHECK(wave.totalBlocks == 10 && wave.maxValues[2] > 0.3f && wave.maxValues[0] == 0.0f, "the click at 1.25 s is in its block 2");
    CHECK(wave.BlockAt(0.5) == -1 && wave.BlockAt(1.25) == 2, "BlockAt counts from the range's start");
    CHECK_OK(VideoFX_ExtractWaveform(click, wave, 10.0, 11.95), "the last 50 ms: one partial block");
    CHECK(wave.totalBlocks == 1 && Near(wave.duration, 0.05, 0.001), "one block, 0.05 s");
    CHECK_OK(VideoFX_ExtractWaveform(longSong, wave, 50.0), "waveform of the encoded 6 s tone");
    CHECK(wave.IsValid() && Near(wave.duration, 6.0, 0.3) && Near(wave.totalBlocks, 300, 15), "about 300 blocks");
    CHECK(Near(wave.Peak(), 0.125, 0.04), "the test tone peaks at 1/8");
    CHECK(wave.IsValid() && Near(wave.rmsValues[wave.totalBlocks / 2], 0.125 / std::sqrt(2.0), 0.03),
          "and its RMS is a sine's");
    const std::string silentClip = TempPath("silent.mkv");
    CHECK_OK(VideoFX_GenerateTestClip(silentClip, 1.0, 64, 48, 25.0, false), "make a clip without sound");
    CHECK(VideoFX_ExtractWaveform(silentClip, wave) == VideoFXResult::NoMediaStreams, "no sound: NoMediaStreams");
    CHECK(VideoFX_ExtractWaveform(TempPath("none.wav"), wave) == VideoFXResult::FileNotFound, "missing file");
    CHECK(VideoFX_ExtractWaveform(click, wave, 0.5) == VideoFXResult::InvalidArgument, "blocksPerSecond below 1");
    CHECK(VideoFX_ExtractWaveform(click, wave, 10.0, 2.0, 1.0) == VideoFXResult::InvalidArgument, "end before start");
    CHECK(VideoFX_ExtractWaveform(click, wave, 10.0, 20.0) == VideoFXResult::InvalidArgument, "start past the end");
    CHECK(!wave.IsValid(), "a failed call leaves an empty waveform");

    const uint32_t colours[4] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
    std::vector<std::string> cardsOnBeat;
    for (int i = 0; i < 4; ++i) {
        VideoFXFrame card;
        card.width = 64; card.height = 36;
        card.pixels.resize(64 * 36 * 4);
        for (size_t p = 0; p < card.pixels.size(); p += 4) {
            card.pixels[p] = (colours[i] >> 16) & 0xFF;
            card.pixels[p + 1] = (colours[i] >> 8) & 0xFF;
            card.pixels[p + 2] = colours[i] & 0xFF;
            card.pixels[p + 3] = 255;
        }
        cardsOnBeat.push_back(TempPath("beat-card" + std::to_string(i) + ".png"));
        CHECK_OK(VideoFX_SaveFrameImage(card, cardsOnBeat.back()), "a coloured card");
    }
    auto colourAt = [&](const std::string& file, double t) {
        VideoFXFrame f;
        if (VideoFX_ExtractFrame(file, t, f) != VideoFXResult::Ok) return -1;
        double cr, cg, cb;
        CentreColour(f, cr, cg, cb);
        for (int i = 0; i < 4; ++i) {
            const double er = (colours[i] >> 16) & 0xFF, eg = (colours[i] >> 8) & 0xFF, eb = colours[i] & 0xFF;
            if (std::fabs(cr - er) < 60 && std::fabs(cg - eg) < 60 && std::fabs(cb - eb) < 60) return i;
        }
        return -1;
    };
    VideoFXSlideshowOptions onBeat;
    onBeat.transition = VideoFXTransition::Make(VideoFXTransitionType::Cut);
    onBeat.secondsPerImage = 1.7;
    onBeat.motion = VideoFXImageMotion::Make(VideoFXMotionStyle::Still);
    onBeat.fadeInOut = false;
    onBeat.music = VideoFXMusic::FromFile(click);
    onBeat.beatSync = true;
    const std::string beatShow = TempPath("beat-show.mkv");
    CHECK_OK(VideoFX_CreateSlideshow(cardsOnBeat, beatShow, onBeat, small), "slideshow on the beat");
    CHECK(colourAt(beatShow, 1.67) == 0 && colourAt(beatShow, 1.83) == 1, "first change on the beat at 1.75 s");
    CHECK(colourAt(beatShow, 3.17) == 1 && colourAt(beatShow, 3.33) == 2, "second at 3.25 s");
    CHECK(colourAt(beatShow, 4.67) == 2 && colourAt(beatShow, 4.83) == 3, "third at 4.75 s");
    onBeat.beatsPerImage = 4;
    onBeat.beatSync = false;                // implied
    const std::string barShow = TempPath("bar-show.mkv");
    CHECK_OK(VideoFX_CreateSlideshow(cardsOnBeat, barShow, onBeat, small), "slideshow, one bar per image");
    CHECK(colourAt(barShow, 2.17) == 0 && colourAt(barShow, 2.33) == 1, "4 beats per image: change at 2.25 s");
    CHECK(colourAt(barShow, 6.17) == 2 && colourAt(barShow, 6.33) == 3, "and at 6.25 s");
    VideoFXSlideshowOptions noBeatMusic;
    noBeatMusic.beatSync = true;
    CHECK(VideoFX_CreateSlideshow(cardsOnBeat, TempPath("x.mkv"), noBeatMusic) == VideoFXResult::InvalidArgument,
          "beat sync without music refused");
    onBeat.beatsPerImage = 65;
    CHECK(VideoFX_CreateSlideshow(cardsOnBeat, TempPath("x.mkv"), onBeat, small) == VideoFXResult::InvalidArgument,
          "65 beats per image refused");

    // ---- errors ----
    VideoFXExportSettings bad = VideoFXExportSettings::AudioOnlyWAV();
    bad.music = VideoFXMusic::FromFile(TempPath("none.mp3"));
    CHECK(VideoFX_Export(cards, TempPath("x.wav"), bad) == VideoFXResult::FileNotFound, "missing music file");
    bad.music = VideoFXMusic::FromFile(photo);
    CHECK(VideoFX_Export(cards, TempPath("x.wav"), bad) == VideoFXResult::NoMediaStreams, "music without sound");
    bad.music = VideoFXMusic::FromFiles({longSong, TempPath("none.mp3")});
    CHECK(VideoFX_Export(cards, TempPath("x.wav"), bad) == VideoFXResult::FileNotFound, "a missing song in the list");
    bad.music = VideoFXMusic::FromFile(longSong, 9.0);
    CHECK(VideoFX_Export(cards, TempPath("x.wav"), bad) == VideoFXResult::InvalidArgument, "volume 9");
    VideoFXSlideshowOptions noMusic;
    noMusic.matchMusicLength = true;
    CHECK(VideoFX_CreateSlideshow({photo}, TempPath("x.mkv"), noMusic) == VideoFXResult::InvalidArgument,
          "fit to music without music");
    CHECK(!Exists(TempPath("x.wav")) && !Exists(TempPath("x.mkv")), "nothing written by refused exports");
}

// ============================================================================
// PREVIEW - a timeline's frames without an export
// ============================================================================
static void TestPreview(const VideoFXExportSettings& base) {
    std::printf("\n-- preview --\n");
    const std::string clip = TempPath("pattern.mkv");      // 320x240, 25 fps, 3 s, from TestEngine

    // red card 1 s | pattern 0..2 s, cross-fade 0.5 in | blue card with text 1 s | green photo 1 s, fade in
    VideoFXSegment red = VideoFXSegment::SolidColor(0xFF0000, 1.0);
    VideoFXSegment file = VideoFXSegment::FromFile(clip, 0.0, 2.0);
    file.transitionIn = VideoFXTransition::Crossfade(0.5);
    VideoFXSegment blue = VideoFXSegment::SolidColor(0x0000FF, 1.0);
    const std::string redClip = TempPath("preview-red.mkv");
    {
        VideoFXExportSettings small = base;
        small.width = 160; small.height = 120; small.frameRate = 25.0;
        CHECK_OK(VideoFX_Export({VideoFXSegment::SolidColor(0xFF0000, 1.0)}, redClip, small), "a red clip for picture-in-picture");
    }
    VideoFXOverlay pip = VideoFXOverlay::Video(redClip, VideoFXAnchor::TopLeft, 0.5);   // 160 x 120 of the 320 x 240
    pip.margin = 0.0;
    pip.start = 0.25;
    blue.overlays = {VideoFXOverlay::Text("Preview", VideoFXAnchor::Bottom, 0.1), pip};
    VideoFXSegment photo = VideoFXSegment::FromImageFrame(SolidFrame(320, 240, 0, 255, 0), 1.0);
    photo.effects = {VideoFXEffect::FadeIn(0.5)};
    const std::vector<VideoFXSegment> timeline = {red, file, blue, photo};

    VideoFXExportSettings s = base;
    s.width = 320; s.height = 240; s.frameRate = 25.0;

    VideoFXPreview preview;
    CHECK(!preview.IsOpen(), "closed before Open");
    CHECK_OK(preview.Open(timeline, s), "open the timeline");
    CHECK(preview.IsOpen(), "open");
    CHECK(preview.GetWidth() == 320 && preview.GetHeight() == 240, "the settings' size");
    CHECK(Near(preview.GetFrameRate(), 25.0, 0.01), "the settings' rate");
    CHECK(Near(preview.GetDuration(), 4.5, 0.01), "1 + 2 + 1 + 1 less the 0.5 s overlap");

    double into = -1.0;
    CHECK(preview.SegmentAt(0.2, &into) == 0 && Near(into, 0.2, 1e-6), "the card at 0.2 s");
    CHECK(preview.SegmentAt(0.7, &into) == 1 && Near(into, 0.2, 1e-6), "inside the overlap: the clip being entered");
    CHECK(preview.SegmentAt(2.0, &into) == 1 && Near(into, 1.5, 1e-6), "the clip at 2.0 s");
    CHECK(preview.SegmentAt(2.6, &into) == 2 && Near(into, 0.1, 1e-6), "the blue card at 2.6 s");
    CHECK(preview.SegmentAt(3.6, &into) == 3 && Near(into, 0.1, 1e-6), "the photo at 3.6 s");
    CHECK(preview.SegmentAt(4.5) == -1 && preview.SegmentAt(-0.1) == -1, "nothing at the end or before the start");

    VideoFXFrame f;
    double r, g, b;
    CHECK_OK(preview.RenderFrame(0.2, f), "render the card");
    CHECK(f.IsValid() && f.width == 320 && f.height == 240, "a 320x240 RGBA frame");
    CHECK(Near(f.timestamp, 0.2, 1e-6), "stamped with the timeline time");
    CentreColour(f, r, g, b);
    CHECK(r > 200 && g < 40 && b < 40, "the card is red");

    CHECK_OK(preview.RenderFrame(0.75, f), "render inside the cross-fade");
    CentreColour(f, r, g, b);
    CHECK(r > 40 && r < 220, "mid-fade: red blended with the pattern");

    CHECK_OK(preview.RenderFrame(2.0, f), "render the file");
    VideoFXFrame direct;
    CHECK_OK(VideoFX_ExtractFrame(clip, 1.5, direct), "the same frame straight from the file");
    double dr, dg, db;
    CentreColour(f, r, g, b);
    CentreColour(direct, dr, dg, db);
    CHECK(std::fabs(r - dr) < 24 && std::fabs(g - dg) < 24 && std::fabs(b - db) < 24,
          "the file's frame at its own 1.5 s");
    bool onward = true;    // scrubbing forward decodes on without a seek: the frames must still be the right ones
    for (double step : {0.04, 0.2, 0.4}) {    // the file plays until 2.5 s
        CHECK_OK(preview.RenderFrame(2.0 + step, f), "render a little later");
        CHECK_OK(VideoFX_ExtractFrame(clip, 1.5 + step, direct), "the same straight from the file");
        onward = onward && f.width == direct.width && f.height == direct.height
                 && std::equal(f.pixels.begin(), f.pixels.end(), direct.pixels.begin(),
                               [](uint8_t p, uint8_t q) { return std::abs(int(p) - int(q)) <= 2; });
    }
    CHECK(onward, "scrubbing forward gives the frames a fresh seek would");
    CHECK_OK(preview.RenderFrame(2.0, f), "and back again");
    CHECK_OK(VideoFX_ExtractFrame(clip, 1.5, direct), "the file at 1.5 s again");
    CHECK(std::equal(f.pixels.begin(), f.pixels.end(), direct.pixels.begin(),
                     [](uint8_t p, uint8_t q) { return std::abs(int(p) - int(q)) <= 2; }),
          "a seek back gives the right frame too");

    CHECK_OK(preview.RenderFrame(2.6, f), "render the blue card with text");
    CentreColour(f, r, g, b);
    CHECK(b > 200 && r < 40, "blue in the middle");
    int whiteish = 0;
    for (int y = f.height * 3 / 4; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) {
            int pr, pg, pb;
            PixelAt(f, x, y, pr, pg, pb);
            if (pr > 180 && pg > 180) ++whiteish;
        }
    CHECK(whiteish > 50, "the text is drawn along the bottom");
    {
        int pr, pg, pb;
        PixelAt(f, 10, 10, pr, pg, pb);
        CHECK(pb > 200 && pr < 40, "at 0.1 s into the card the video overlay has not started");
        CHECK_OK(preview.RenderFrame(3.0, f), "render the card with the overlay showing");
        PixelAt(f, 10, 10, pr, pg, pb);
        CHECK(pr > 200 && pb < 40, "the red clip in the top left");
        PixelAt(f, 200, 200, pr, pg, pb);
        CHECK(pb > 200 && pr < 40, "the card around it");
        CHECK_OK(preview.RenderFrame(3.2, f), "render a little later");
        PixelAt(f, 10, 10, pr, pg, pb);
        CHECK(pr > 200 && pb < 40, "still there, decoded onward");
    }

    CHECK_OK(preview.RenderFrame(3.5, f), "render the photo at its first frame");
    CentreColour(f, r, g, b);
    CHECK(g < 60, "the fade-in starts black");
    CHECK_OK(preview.RenderFrame(4.4, f), "render the photo faded in");
    CentreColour(f, r, g, b);
    CHECK(g > 200 && r < 40, "green once the fade is over");

    CHECK_OK(preview.RenderFrame(2.0, f, 100, 100), "render scaled");
    CHECK(f.width == 100 && f.height == 75, "scaled keeping 4:3");
    CHECK(preview.RenderFrame(4.5, f) == VideoFXResult::InvalidArgument, "past the end is refused");
    CHECK(preview.RenderFrame(-1.0, f) == VideoFXResult::InvalidArgument, "so is before the start");

    preview.Close();
    CHECK(!preview.IsOpen() && preview.GetDuration() == 0.0, "closed again");
    CHECK(preview.RenderFrame(0.0, f) == VideoFXResult::InvalidArgument, "nothing to render when closed");

    // ---- the one-call form ----
    CHECK_OK(VideoFX_RenderFrame(timeline, 0.2, f, s), "one frame in one call");
    CentreColour(f, r, g, b);
    CHECK(r > 200 && g < 40, "and it is the red card");
    CHECK_OK(VideoFX_RenderFrame({red, blue}, 1.5, f), "default settings take the first segment's size");
    CHECK(f.width > 0 && f.height > 0, "a frame of some size");
    CHECK(VideoFX_RenderFrame({}, 0.0, f) == VideoFXResult::InvalidArgument, "no segments");
    VideoFXSegment missing = VideoFXSegment::FromFile(TempPath("nowhere.mkv"));
    CHECK(VideoFX_RenderFrame({missing}, 0.0, f) == VideoFXResult::FileNotFound, "a missing file");

    // ---- a slideshow's plan ----
    std::vector<std::string> photos;
    const uint8_t rgb[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    for (int i = 0; i < 3; ++i) {
        photos.push_back(TempPath("plan" + std::to_string(i) + ".png"));
        CHECK_OK(VideoFX_SaveFrameImage(SolidFrame(160, 120, rgb[i][0], rgb[i][1], rgb[i][2]), photos.back()),
                 "a slideshow photo");
    }
    VideoFXSlideshowOptions options;
    options.secondsPerImage = 2.0;
    options.transition = VideoFXTransition::Crossfade(0.5);
    options.captions = {"one", "", "three"};
    options.keepFacesInView = false;
    std::vector<VideoFXSegment> plan;
    VideoFXExportSettings planned;
    CHECK_OK(VideoFX_PlanSlideshow(photos, options, {}, plan, planned), "plan a slideshow");
    CHECK(plan.size() == 3, "one segment per photo");
    bool images = plan.size() == 3;
    for (const auto& seg : plan) images = images && seg.kind == VideoFXSourceKind::Image && Near(seg.duration, 2.0, 1e-6);
    CHECK(images, "image segments of two seconds");
    CHECK(plan.size() == 3 && plan[0].path == photos[0] && plan[2].path == photos[2], "in the order given");
    CHECK(plan.size() == 3 && plan[0].transitionIn.type == VideoFXTransitionType::Cut
          && plan[1].transitionIn.type == VideoFXTransitionType::Crossfade && Near(plan[1].transitionIn.duration, 0.5, 1e-6),
          "the transition on every cut but the first");
    CHECK(plan.size() == 3 && plan[0].overlays.size() == 1 && plan[0].overlays[0].text == "one"
          && plan[1].overlays.empty() && plan[2].overlays.size() == 1 && plan[2].overlays[0].text == "three",
          "captions where given");
    bool fades = plan.size() == 3 && !plan[0].effects.empty() && plan[0].effects.front().type == VideoFXEffectType::FadeIn
                 && !plan[2].effects.empty() && plan[2].effects.back().type == VideoFXEffectType::FadeOut;
    CHECK(fades, "fade in at the start, out at the end");
    CHECK(planned.width == 1920 && planned.height == 1080, "the slideshow's default size");
    CHECK_OK(VideoFX_RenderFrame(plan, 3.0, f, planned, 192, 108), "a frame of the plan");
    CHECK(f.width == 192 && f.height == 108, "scaled to the request");
    CentreColour(f, r, g, b);
    CHECK(g > 150 && r < 80, "the second photo at 3 s");
    CHECK(VideoFX_PlanSlideshow({}, options, {}, plan, planned) == VideoFXResult::InvalidArgument, "no photos");
}

// Mean absolute difference of two RGBA frames of one size, 0..255; -1 when they differ in size
static double MeanDifference(const VideoFXFrame& a, const VideoFXFrame& b) {
    if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size()) return -1.0;
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i < a.pixels.size(); ++i) {
        if (i % 4 == 3) continue;
        sum += std::abs(static_cast<int>(a.pixels[i]) - static_cast<int>(b.pixels[i]));
        ++n;
    }
    return n ? sum / n : 0.0;
}

static void TestProxies(const VideoFXExportSettings& base) {
    std::printf("\n-- proxies --\n");
    VideoFXProxySettings ps;
    ps.height = 180;
    ps.cacheDirectory = TempPath("proxies");

    // a 640x360 clip with sound, under a UTF-8 name
    const std::string clip = TempPath("Reise \xC3\xBC.mkv");
    {
        VideoFXExportSettings s = base;
        s.width = 640; s.height = 360; s.frameRate = 25.0;
        CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(3.0)}, clip, s), "a 640x360 clip");
    }

    std::string editPath;
    double last = -1.0;
    bool rising = true;
    CHECK_OK(VideoFX_CreateProxy(clip, ps, [&](double f) { rising = rising && f >= last; last = f; return true; }, &editPath),
             "make its proxy");
    CHECK(editPath == VideoFX_ProxyPath(clip, ps) && Exists(editPath) && VideoFX_HasProxy(clip, ps), "the proxy is made where it belongs");
    CHECK(rising && last > 0.9, "progress rises to the end");
    CHECK(VideoFX_GetLastError().empty(), "no error left behind");
    VideoFXMediaInfo info, orig;
    CHECK_OK(VideoFX_Probe(editPath, info), "probe the proxy");
    CHECK_OK(VideoFX_Probe(clip, orig), "probe the clip");
    CHECK(info.width == 320 && info.height == 180, "320x180: the clip's shape at the proxy height");
    CHECK(Near(info.frameRate, 25.0, 0.01) && info.HasAudio(), "its frame rate, and its sound");
    CHECK(Near(info.duration, orig.duration, 0.1), "as long as the clip");
    {
        // the same picture at the same time: the clip scaled down vs the proxy
        VideoFXFrame a, b, c;
        CHECK_OK(VideoFX_ExtractFrame(clip, 1.2, a, 320, 180), "the clip's frame at 1.2 s");
        CHECK_OK(VideoFX_ExtractFrame(editPath, 1.2, b), "the proxy's frame at 1.2 s");
        CHECK_OK(VideoFX_ExtractFrame(editPath, 2.4, c), "and at 2.4 s");
        const double same = MeanDifference(a, b), other = MeanDifference(a, c);
        std::printf("        difference: same time %.1f, 1.2 s later %.1f\n", same, other);
        CHECK(same >= 0.0 && same < 12.0, "the proxy shows the clip's picture at that time");
        CHECK(other > same, "and not another moment's");
    }

    // made once
    const auto before = fs::last_write_time(UltraCanvas::PathFromUtf8(editPath));
    std::string again;
    CHECK_OK(VideoFX_CreateProxy(clip, ps, {}, &again), "asked again");
    CHECK(again == editPath && fs::last_write_time(UltraCanvas::PathFromUtf8(editPath)) == before, "the proxy is not made again");

    // nothing to gain: a small clip, a sound file
    const std::string small = TempPath("small.mkv");
    const std::string wav = TempPath("tone.wav");
    {
        VideoFXExportSettings s = base;
        s.width = 160; s.height = 120; s.frameRate = 25.0;
        CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(1.0)}, small, s), "a 160x120 clip");
        CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(1.0)}, wav, VideoFXExportSettings::AudioOnlyWAV()), "a WAV");
    }
    std::string edit;
    CHECK_OK(VideoFX_CreateProxy(small, ps, {}, &edit), "a clip no taller than a proxy");
    CHECK(edit == small && !VideoFX_HasProxy(small, ps), "is edited as it is: no proxy made");
    CHECK_OK(VideoFX_CreateProxy(wav, ps, {}, &edit), "a sound file");
    CHECK(edit == wav && !VideoFX_HasProxy(wav, ps), "is edited as it is too");

    // refused and cancelled
    CHECK(VideoFX_CreateProxy(TempPath("gone.mkv"), ps) == VideoFXResult::FileNotFound, "a missing clip: FileNotFound");
    VideoFXProxySettings bad = ps;
    bad.height = 0;
    CHECK(VideoFX_CreateProxy(clip, bad) == VideoFXResult::InvalidArgument, "a height of 0: InvalidArgument");
    VideoFXProxySettings other = ps;
    other.height = 120;
    CHECK(VideoFX_CreateProxy(clip, other, [](double) { return false; }) == VideoFXResult::Cancelled, "cancelled");
    bool leftovers = false;
    for (const auto& e : fs::directory_iterator(UltraCanvas::PathFromUtf8(ps.cacheDirectory)))
        leftovers = leftovers || UltraCanvas::PathToUtf8(e.path().filename()).find(".part") != std::string::npos;
    CHECK(!VideoFX_HasProxy(clip, other) && !leftovers, "a cancelled proxy leaves no file, whole or half");

    // a timeline: every clip once, then played from the proxies
    const std::string pipClip = TempPath("pip-source.mkv");
    {
        VideoFXExportSettings s = base;
        s.width = 480; s.height = 270; s.frameRate = 25.0;
        CHECK_OK(VideoFX_Export({VideoFXSegment::SolidColor(0xFF0000, 2.0)}, pipClip, s), "a 480x270 red clip");
    }
    VideoFXSegment first = VideoFXSegment::FromFile(clip, 0.0, 1.5);
    VideoFXSegment second = VideoFXSegment::FromFile(clip, 1.0, 2.5);
    second.effects = {VideoFXEffect::Crop(160, 90, 320, 180)};
    VideoFXOverlay pip = VideoFXOverlay::Video(pipClip, VideoFXAnchor::TopLeft, 0.25);
    pip.margin = 0.0;
    second.overlays = {pip};
    const std::vector<VideoFXSegment> timeline = {first, VideoFXSegment::FromFile(small), second};
    CHECK_OK(VideoFX_ClearProxies(ps), "start from an empty cache");
    CHECK(!VideoFX_HasProxy(clip, ps), "emptied");
    double seen = -1.0;
    bool monotone = true;
    CHECK_OK(VideoFX_CreateProxies(timeline, ps, [&](double f) { monotone = monotone && f >= seen - 1e-9; seen = f; return true; }),
             "proxies for a whole timeline");
    CHECK(monotone && Near(seen, 1.0, 1e-9), "one progress from 0 to 1 over all of them");
    CHECK(VideoFX_HasProxy(clip, ps) && VideoFX_HasProxy(pipClip, ps) && !VideoFX_HasProxy(small, ps),
          "the clip and the picture-in-picture source; not the small clip");
    int replaced = 0;
    const std::vector<VideoFXSegment> proxied = VideoFX_UseProxies(timeline, ps, &replaced);
    CHECK(replaced == 3, "two segments and the overlay switched");
    CHECK(proxied[1].path == small, "the small clip plays itself");
    CHECK(proxied[2].effects.size() == 1 && proxied[2].effects[0].x == 80 && proxied[2].effects[0].y == 45 &&
          proxied[2].effects[0].width == 160 && proxied[2].effects[0].height == 90, "the crop scaled to the proxy");

    // the preview over proxies matches the preview over the originals
    VideoFXExportSettings s = base;
    s.width = 320; s.height = 180; s.frameRate = 25.0;
    for (double t : {0.6, 3.0}) {         // the first segment; the cropped one with its overlay
        VideoFXFrame a, b;
        CHECK_OK(VideoFX_RenderFrame(timeline, t, a, s), "a frame of the originals");
        CHECK_OK(VideoFX_RenderFrame(proxied, t, b, s), "the same frame from the proxies");
        const double d = MeanDifference(a, b);
        std::printf("        at %.1f s: difference %.1f\n", t, d);
        CHECK(d >= 0.0 && d < 14.0, "the proxies show the same picture");
    }

    // the keyframe interval of an export
    VideoFXExportSettings k = base;
    k.keyframeInterval = -1.0;
    CHECK(VideoFX_Export({VideoFXSegment::TestPattern(0.5)}, TempPath("k.mkv"), k) == VideoFXResult::InvalidArgument,
          "a negative keyframe interval: InvalidArgument");

    int files = 0;
    CHECK_OK(VideoFX_ClearProxies(ps, &files), "clear the proxies");
    CHECK(files == 2 && !VideoFX_HasProxy(clip, ps), "both gone");
}

// Software, hardware, or one hardware family. The machine running the tests
// may have no graphics hardware at all, so what is checked is that every
// answer is consistent, a missing family fails cleanly, Hardware falls back,
// and - where some hardware encoder does work - that it really encodes.
static const VideoFXFileFormat* FindFormat(const std::vector<VideoFXFileFormat>& formats, const std::string& ext) {
    for (const VideoFXFileFormat& f : formats) {
        if (f.extension == ext) return &f;
        for (const std::string& a : f.aliases) if (a == ext) return &f;
    }
    return nullptr;
}

static void TestFileFormats() {
    std::printf("\n-- the files this build reads and writes --\n");
    const std::vector<VideoFXFileFormat> formats = VideoFX_GetFileFormats();
    std::string reads, writes;
    for (const VideoFXFileFormat& f : formats) {
        if (f.canRead) reads += " " + f.extension;
        if (f.canWrite) writes += " " + f.extension;
    }
    std::printf("        reads:%s\n        writes:%s\n", reads.c_str(), writes.c_str());

    const VideoFXFileFormat* mp4 = FindFormat(formats, "m4v");
    CHECK(mp4 && mp4->extension == "mp4" && mp4->kind == VideoFXFileKind::Video && mp4->description == "MPEG-4 container",
          "MP4 is a film, found by its alias too, under the registry's name");
    CHECK(mp4 && mp4->canRead && mp4->canWrite && mp4->container == VideoFXContainer::MP4,
          "... read, and written as the MP4 container (MPEG-4 and AAC are always here)");
    const VideoFXFileFormat* mpg = FindFormat(formats, "mpeg");
    CHECK(mpg && mpg->canRead && !mpg->canWrite && mpg->container == VideoFXContainer::Auto,
          "MPEG program streams are read, and no export writes them");
    const VideoFXFileFormat* wav = FindFormat(formats, "wav");
    CHECK(wav && wav->kind == VideoFXFileKind::Audio && wav->canRead && wav->canWrite,
          "WAV is sound, read and written");
    const VideoFXFileFormat* png = FindFormat(formats, "png");
    const VideoFXFileFormat* jpg = FindFormat(formats, "jpeg");
    CHECK(png && jpg && png->kind == VideoFXFileKind::Picture && png->canRead && jpg->canRead && !png->canWrite,
          "PNG and JPEG pictures are read; an export does not write a still");
    const VideoFXFileFormat* gif = FindFormat(formats, "gif");
    CHECK(gif && gif->canRead && gif->canWrite && gif->container == VideoFXContainer::GIF,
          "a GIF is read, and an export writes one: the animation");

    // Every type it says it writes, written and read back: no claim untested
    int written = 0;
    bool allOk = true;
    for (const VideoFXFileFormat& f : formats) {
        if (!f.canWrite) continue;
        VideoFXExportSettings s;
        s.container = f.container;
        s.width = 64; s.height = 48; s.frameRate = 10.0;
        const std::string out = TempPath("format." + f.extension);
        const bool ok = VideoFX_Export({VideoFXSegment::TestPattern(0.5)}, out, s) == VideoFXResult::Ok;
        VideoFXMediaInfo info;
        const bool back = ok && VideoFX_Probe(out, info) == VideoFXResult::Ok &&
                          (f.kind == VideoFXFileKind::Audio ? info.HasAudio() : info.HasVideo());
        if (!back) std::printf("        %s: %s\n", f.extension.c_str(), VideoFX_GetLastError().c_str());
        allOk = allOk && back;
        ++written;
    }
    CHECK(written >= 4 && allOk, "every type it writes is written, and read back");

    // Every picture it reads, read: one saved through FFmpeg's own writer
    VideoFXFrame frame;
    CHECK_OK(VideoFX_ExtractFrame(TempPath("format.mp4"), 0.1, frame), "a frame for the pictures");
    const std::string still = TempPath("format-still.png");
    CHECK_OK(VideoFX_SaveFrameImage(frame, still), "... saved as PNG");
    VideoFXMediaInfo info;
    CHECK(VideoFX_Probe(still, info) == VideoFXResult::Ok && info.HasVideo(), "... which it reads as a picture");
}

static void TestEncoders(const VideoFXExportSettings& base) {
    std::printf("\n-- encoders: software and hardware --\n");
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<VideoFXEncoderInfo> h264 = VideoFX_ListVideoEncoders(VideoFXVideoCodec::H264);
    const double firstMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    bool allH264 = true, kindsMatch = true;
    for (const VideoFXEncoderInfo& e : h264) {
        std::printf("        %-20s %-16s %s\n", e.name.c_str(), VideoFX_AccelerationName(e.acceleration),
                    e.works ? "works" : "not here");
        allH264 = allH264 && e.codec == VideoFXVideoCodec::H264;
        kindsMatch = kindsMatch && (e.acceleration == VideoFXAcceleration::Software) ==
                     (e.name.rfind("lib", 0) == 0);
    }
    CHECK(allH264, "only H.264 encoders listed for H.264");
    CHECK(kindsMatch, "software ones are the lib* encoders, the rest hardware");
    const auto t1 = std::chrono::steady_clock::now();
    const std::vector<VideoFXEncoderInfo> again = VideoFX_ListVideoEncoders(VideoFXVideoCodec::H264);
    const double secondMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    bool same = again.size() == h264.size();
    for (size_t i = 0; same && i < again.size(); ++i) same = again[i].name == h264[i].name && again[i].works == h264[i].works;
    std::printf("        first list %.0f ms, second %.1f ms\n", firstMs, secondMs);
    CHECK(same && secondMs < 50.0, "each encoder tried once: the second answer is the same, at once");

    const std::vector<VideoFXEncoderInfo> all = VideoFX_ListVideoEncoders();
    bool mpeg4 = false;
    for (const VideoFXEncoderInfo& e : all) mpeg4 = mpeg4 || (e.name == "mpeg4" && e.works);
    CHECK(all.size() >= h264.size() && mpeg4, "all codecs: FFmpeg's own MPEG-4 among them, working");

    // the choice follows the list
    const std::string sw = VideoFX_ChooseVideoEncoder(VideoFXVideoCodec::MPEG4, VideoFXAcceleration::Software);
    CHECK(sw == "mpeg4", "Software MPEG-4: FFmpeg's encoder");
    CHECK(VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::MPEG4, VideoFXAcceleration::Software), "available as software");
    CHECK(!VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::MPEG4, VideoFXAcceleration::NVENC) &&
          VideoFX_ChooseVideoEncoder(VideoFXVideoCodec::MPEG4, VideoFXAcceleration::NVENC).empty(),
          "no NVENC MPEG-4 exists: none chosen");
    CHECK(VideoFX_ChooseVideoEncoder(VideoFXVideoCodec::MPEG4, VideoFXAcceleration::Hardware) == "mpeg4",
          "Hardware with no hardware encoder falls back to software");
    std::string workingHw;
    VideoFXAcceleration workingKind = VideoFXAcceleration::Software;
    for (const VideoFXEncoderInfo& e : h264)
        if (e.works && e.acceleration != VideoFXAcceleration::Software && workingHw.empty()) {
            workingHw = e.name;
            workingKind = e.acceleration;
        }
    const std::string hw = VideoFX_ChooseVideoEncoder(VideoFXVideoCodec::H264, VideoFXAcceleration::Hardware);
    CHECK(workingHw.empty() ? (hw.empty() || hw.rfind("lib", 0) == 0) : hw == workingHw,
          "Hardware H.264: the first working hardware encoder, else software");
    CHECK(VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::H264, VideoFXAcceleration::Hardware) == !workingHw.empty(),
          "\"is hardware available\" agrees with the list");

    // exports
    const std::string out = TempPath("encoder.mkv");
    VideoFXExportSettings s = base;
    s.width = 320; s.height = 240; s.frameRate = 25.0;
    s.acceleration = VideoFXAcceleration::Software;
    CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(0.5)}, out, s), "export with Software");
    s.acceleration = VideoFXAcceleration::Hardware;
    CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(0.5)}, out, s), "export with Hardware (MPEG-4 has none: software)");
    s.acceleration = VideoFXAcceleration::NVENC;
    CHECK(VideoFX_Export({VideoFXSegment::TestPattern(0.5)}, out, s) == VideoFXResult::EncoderNotAvailable &&
          VideoFX_GetLastError().find("NVENC") != std::string::npos, "a family that cannot encode it: EncoderNotAvailable, named");

    // a hardware encoder that works here: a real file from it
    if (!workingHw.empty()) {
        VideoFXExportSettings h;
        h.container = VideoFXContainer::MP4;
        h.videoCodec = VideoFXVideoCodec::H264;
        h.audioCodec = VideoFXAudioCodec::AAC;
        h.width = 640; h.height = 360; h.frameRate = 25.0;
        h.acceleration = workingKind;
        const std::string hwOut = TempPath("hardware.mp4");
        CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(2.0)}, hwOut, h), "export through the hardware encoder");
        VideoFXMediaInfo info;
        CHECK_OK(VideoFX_Probe(hwOut, info), "probe it");
        CHECK(info.width == 640 && info.height == 360 && Near(info.duration, 2.0, 0.2), "640x360, two seconds");
        VideoFXFrame a, b;
        CHECK_OK(VideoFX_ExtractFrame(hwOut, 1.0, a), "a frame of it");
        CHECK_OK(VideoFX_RenderFrame({VideoFXSegment::TestPattern(2.0)}, 1.0, b, h), "the same frame rendered");
        const double d = MeanDifference(a, b);
        std::printf("        %s: difference %.1f\n", workingHw.c_str(), d);
        CHECK(d >= 0.0 && d < 20.0, "the hardware encoder's picture is the timeline's");
    } else {
        std::printf("        (no hardware encoder works on this machine: the hardware export is not tried)\n");
    }
}

// The RMS of a file's sound in the 0.1 s block that holds `seconds`
static double LevelAt(const VideoFXWaveform& w, double seconds) {
    const int i = static_cast<int>(std::floor((seconds - w.start) * 10.0));
    return i >= 0 && i < w.totalBlocks ? w.rmsValues[static_cast<size_t>(i)] : -1.0;
}

static void TestAudioClips(const VideoFXExportSettings& base) {
    std::printf("\n-- free audio clips --\n");
    // a 1 s tone, and a 6 s one for music
    const std::string tone = TempPath("clip-tone.wav");
    const std::string song = TempPath("clip-song.wav");
    CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(1.0)}, tone, VideoFXExportSettings::AudioOnlyWAV()), "a 1 s tone");
    CHECK_OK(VideoFX_Export({VideoFXSegment::TestPattern(6.0)}, song, VideoFXExportSettings::AudioOnlyWAV()), "a 6 s song");

    // three silent seconds of picture with a tone at 1 s
    VideoFXExportSettings s = base;
    s.width = 160; s.height = 120; s.frameRate = 25.0;
    const std::vector<VideoFXSegment> silent = {VideoFXSegment::SolidColor(0x000000, 3.0)};
    const std::string out = TempPath("clips.mkv");
    s.audioClips = {VideoFXAudioClip::Sound(tone, 1.0)};
    CHECK_OK(VideoFX_Export(silent, out, s), "silent picture with a sound at 1 s");
    VideoFXMediaInfo info;
    CHECK_OK(VideoFX_Probe(out, info), "probe it");
    CHECK(info.HasAudio() && Near(info.duration, 3.0, 0.15), "it has sound now, and is still three seconds");
    VideoFXWaveform w;
    CHECK_OK(VideoFX_ExtractWaveform(out, w, 10.0), "its waveform");
    std::printf("        levels at 0.5 / 1.5 / 2.5 s: %.3f %.3f %.3f\n", LevelAt(w, 0.5), LevelAt(w, 1.5), LevelAt(w, 2.5));
    CHECK(LevelAt(w, 0.5) < 0.01 && LevelAt(w, 1.5) > 0.05 && LevelAt(w, 2.5) < 0.01,
          "silent before, the tone from 1 to 2 s, silent after");

    // a part of the file, quieter, faded in
    VideoFXAudioClip part = VideoFXAudioClip::Sound(tone, 0.5, 0.5);
    part.start = 0.2;
    part.end = 0.9;                              // 0.7 s: plays 0.5 .. 1.2
    part.fadeIn = 0.4;
    s.audioClips = {part, VideoFXAudioClip::Sound(tone, 2.6)};   // and one running past the end
    CHECK_OK(VideoFX_Export(silent, out, s), "a trimmed, quieter, faded clip and one past the end");
    CHECK_OK(VideoFX_ExtractWaveform(out, w, 10.0), "its waveform");
    const double full = LevelAt(w, 2.8);
    std::printf("        fade 0.55 s %.3f, held 1.05 s %.3f, after 1.35 s %.3f, late clip 2.8 s %.3f\n",
                LevelAt(w, 0.55), LevelAt(w, 1.05), LevelAt(w, 1.35), full);
    CHECK(LevelAt(w, 0.55) < LevelAt(w, 1.05) && LevelAt(w, 1.05) > 0.02, "it fades in");
    CHECK(Near(LevelAt(w, 1.05), full * 0.5, full * 0.15), "at half the volume of the full one");
    CHECK(LevelAt(w, 1.35) < 0.01, "and stops where its end says");
    CHECK_OK(VideoFX_Probe(out, info), "probe it");
    CHECK(Near(info.duration, 3.0, 0.15) && full > 0.05, "a clip running past the end is cut there, not the export lengthened");

    // a voice-over ducks the music; a sound effect does not
    VideoFXExportSettings m = s;
    m.music = VideoFXMusic::FromFile(song, 0.5);
    m.music.duckingLevel = 0.2;
    m.music.fadeIn = 0.0;
    m.music.fadeOut = 0.0;
    const std::string voiced = TempPath("voiced.mkv"), effects = TempPath("effects.mkv");
    m.audioClips = {VideoFXAudioClip::VoiceOver(tone, 1.0, 0.5)};
    CHECK_OK(VideoFX_Export(silent, voiced, m), "music with a voice-over");
    m.audioClips = {VideoFXAudioClip::Sound(tone, 1.0, 0.5)};
    CHECK_OK(VideoFX_Export(silent, effects, m), "music with the same sound as an effect");
    VideoFXWaveform a, b;
    CHECK_OK(VideoFX_ExtractWaveform(voiced, a, 10.0), "waveform of the voiced one");
    CHECK_OK(VideoFX_ExtractWaveform(effects, b, 10.0), "and of the other");
    std::printf("        voice-over vs effect: before %.3f / %.3f, under it %.3f / %.3f\n",
                LevelAt(a, 0.5), LevelAt(b, 0.5), LevelAt(a, 1.7), LevelAt(b, 1.7));
    CHECK(Near(LevelAt(a, 0.5), LevelAt(b, 0.5), 0.01), "the same before the clip");
    CHECK(LevelAt(a, 1.7) < LevelAt(b, 1.7) - 0.03, "under the voice-over the music has ducked; under the effect it has not");

    // refused
    VideoFXExportSettings e = s;
    e.audioClips = {VideoFXAudioClip::Sound(TempPath("none.wav"), 1.0)};
    CHECK(VideoFX_Export(silent, out, e) == VideoFXResult::FileNotFound, "a missing file: FileNotFound");
    e.audioClips = {VideoFXAudioClip::Sound(tone, 1.0, 9.0)};
    CHECK(VideoFX_Export(silent, out, e) == VideoFXResult::InvalidArgument, "volume 9: InvalidArgument");
    VideoFXAudioClip late = VideoFXAudioClip::Sound(tone, 1.0);
    late.start = 5.0;
    e.audioClips = {late};
    CHECK(VideoFX_Export(silent, out, e) == VideoFXResult::InvalidArgument, "a start past the file's end: InvalidArgument");
    const std::string picture = TempPath("clip-picture.mkv");
    VideoFXExportSettings p = base;
    p.width = 160; p.height = 120; p.frameRate = 25.0;
    CHECK_OK(VideoFX_Export(silent, picture, p), "a clip without sound");
    e.audioClips = {VideoFXAudioClip::Sound(picture, 1.0)};
    CHECK(VideoFX_Export(silent, out, e) == VideoFXResult::NoMediaStreams, "a file without sound: NoMediaStreams");
}

#else

static void TestEngine() {
    std::printf("Engine: not built (no FFmpeg)\n");
    VideoFXMediaInfo info;
    CHECK(!VideoFX_IsAvailable(), "stub says unavailable");
    CHECK(VideoFX_Probe("x.mp4", info) == VideoFXResult::NotAvailable, "stub returns NotAvailable");
    CHECK(VideoFX_ListVideoEncoders().empty() &&
          !VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::H264, VideoFXAcceleration::Hardware) &&
          VideoFX_ChooseVideoEncoder(VideoFXVideoCodec::H264, VideoFXAcceleration::Software).empty(),
          "stub: no encoders of any kind");
    CHECK(VideoFX_GetFileFormats().empty(), "stub: no file it reads or writes");
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
    TestAudioClipMath();
    TestBeatMath();
    TestFaceDetector();
    TestProjectFiles();
    TestProxyFiles();
    TestEngine();
    if (failures) {
        std::printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nAll VideoFX checks passed\n");
    return 0;
}
