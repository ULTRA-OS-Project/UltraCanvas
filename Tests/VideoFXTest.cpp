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

static double StreamDuration(const VideoFXMediaInfo& info, VideoFXStreamKind kind) {
    for (const auto& s : info.streams) if (s.kind == kind) return s.duration;
    return -1.0;
}

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

    std::error_code ec;
    fs::remove_all(UltraCanvas::PathFromUtf8(TempPath("")), ec);
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
    TestEngine();
    if (failures) {
        std::printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nAll VideoFX checks passed\n");
    return 0;
}
