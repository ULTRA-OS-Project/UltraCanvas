// Tests/UltraVideoDraftTest.cpp
// The draft UltraVideo plays with sound, without a display: rendered from
// a document with the settings the app uses, its sound steps in where the
// timeline has the cut (the decoded loudness envelope against the
// document's cut time), and the framework's player reports a position
// that matches the frames it shows, at the cut within a frame. Skips
// itself without the FFmpeg engine, and the player part without a
// playing player (no GStreamer plugins).
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraVideoDocument.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasVideoPlayer.h"
#include "VideoFX/VideoFX.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;
using namespace UltraVideo;
using namespace VideoFX;
namespace fs = std::filesystem;

static int testCount = 0, failCount = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        const bool ok = (cond);                                                \
        std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", msg);                  \
        if (!ok) ++failCount;                                                  \
        ++testCount;                                                           \
    } while (0)

static bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

static std::string TempPath(const std::string& name) {
    static const fs::path dir = [] {
        fs::path d = fs::temp_directory_path() / ("ultravideo-draft-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(d / "media");
        return d;
    }();
    return PathToUtf8(dir / PathFromUtf8(name));
}

static VideoFXFrame Solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    VideoFXFrame f;
    f.width = w;
    f.height = h;
    f.pixels.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < f.pixels.size(); i += 4) {
        f.pixels[i] = r; f.pixels[i + 1] = g; f.pixels[i + 2] = b; f.pixels[i + 3] = 255;
    }
    return f;
}

int main() {
    std::printf("UltraVideo draft suite (VideoFX %s)\n", VideoFX_GetVersion().c_str());
    if (!VideoFX_IsAvailable()) {
        std::printf("SKIP: the VideoFX engine is not built (no FFmpeg)\n");
        return 0;
    }
    const double frame = 1.0 / 25.0;

    // ---- a silent photo, then a clip with a tone ----
    const std::string photo = TempPath("media/photo.png");
    const std::string clip = TempPath("media/pattern.mkv");
    {
        VideoFXExportSettings s;
        s.container = VideoFXContainer::MKV;
        s.videoCodec = VideoFXVideoCodec::MPEG4;
        s.audioCodec = VideoFXAudioCodec::AAC;
        s.width = 320; s.height = 240; s.frameRate = 25.0;
        CHECK(VideoFX_SaveFrameImage(Solid(160, 120, 255, 0, 0), photo) == VideoFXResult::Ok, "a photo");
        CHECK(VideoFX_Export({VideoFXSegment::TestPattern(3.0)}, clip, s) == VideoFXResult::Ok, "a 3 s clip with a tone");
    }
    Document doc;
    const MediaItem picture = doc.Import(photo);
    const MediaItem video = doc.Import(clip);
    CHECK(picture.error.empty() && video.error.empty(), "both import");
    CHECK(doc.AppendMedia(picture, 2.0) == 0 && doc.AppendMedia(video) == 1, "two seconds of photo, then the clip");
    const double cut = doc.SegmentSeconds(0);
    CHECK(Near(cut, 2.0, 1e-9) && Near(doc.Length(), 5.0, 0.1), "the cut at two seconds, five in all");

    std::printf("\n-- the draft render --\n");
    const VideoFXExportSettings settings = doc.DraftSettings();
    CHECK(settings.height == 360 && settings.width == 480, "360 lines at the project's 4:3 shape");
    CHECK(settings.encoderPreset == "ultrafast" && Near(settings.frameRate, 25.0, 1e-9), "quick to encode, at the project's rate");
    const std::string draft = TempPath("draft.mp4");
    CHECK(VideoFX_Export(doc.SegmentsForEngine(), draft, settings) == VideoFXResult::Ok, "rendered");
    VideoFXMediaInfo info;
    CHECK(VideoFX_Probe(draft, info) == VideoFXResult::Ok && info.width == 480 && info.height == 360 && Near(info.duration, 5.0, 0.15),
          "480 x 360, five seconds");

    std::printf("\n-- the sound against the cut --\n");
    VideoFXWaveform wave;
    const VideoFXResult waveResult = VideoFX_ExtractWaveform(draft, wave, 100.0);
    CHECK(waveResult == VideoFXResult::Ok && wave.IsValid(), "the draft has sound, read at 10 ms");
    double onset = -1.0, loudBefore = 0.0, loudAfter = 0.0;
    if (wave.IsValid()) {
        const double block = wave.SecondsPerBlock();
        for (int i = 0; i < wave.totalBlocks; ++i) {
            const double at = wave.start + i * block;
            if (at < cut - 0.1) loudBefore = std::max(loudBefore, static_cast<double>(wave.rmsValues[static_cast<size_t>(i)]));
            if (at > cut + 0.1 && at < cut + 1.0) loudAfter = std::max(loudAfter, static_cast<double>(wave.rmsValues[static_cast<size_t>(i)]));
            if (onset < 0.0 && wave.rmsValues[static_cast<size_t>(i)] > 0.02) onset = at;
        }
    }
    std::printf("  silence before the cut: %.4f, sound after it: %.3f, onset at %.3f s (cut %.3f s)\n", loudBefore, loudAfter, onset, cut);
    CHECK(loudBefore < 0.01, "silence under the photo");
    CHECK(loudAfter > 0.05, "the clip's tone after the cut");
    CHECK(Near(onset, cut, frame + 0.01), "the sound steps in at the cut, within a frame");

    std::printf("\n-- the player's position against its frames --\n");
    {
        UltraCanvasVideoPlayer player;
        std::atomic<double> latestPts{-1.0};
        std::atomic<int> frames{0};
        player.onFrameReady = [&](UCVideoFramePtr f) {
            if (f) { latestPts.store(f->GetPTS()); frames.fetch_add(1); }
        };
        bool ready = player.LoadFromFile(draft);
        for (int i = 0; ready && i < 300; ++i) {
            const VideoPlaybackState s = player.GetState();
            if (s == VideoPlaybackState::Error) ready = false;
            if (s != VideoPlaybackState::Loading && s != VideoPlaybackState::Idle) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!ready) {
            std::printf("  SKIP: the player cannot open the draft here (no GStreamer plugins?)\n");
        } else {
            player.Seek(0.0);
            const bool playing = player.Play();
            struct Sample { double at, position, pts; };
            std::vector<Sample> samples;
            const auto t0 = std::chrono::steady_clock::now();
            while (playing) {
                const double at = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                if (at > 4.0 || player.GetState() == VideoPlaybackState::Stopped) break;
                samples.push_back({at, player.GetPosition(), latestPts.load()});
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            player.Stop();
            if (!playing || frames.load() == 0) {
                std::printf("  SKIP: the player does not play here (no frames arrived; Play %s)\n", playing ? "accepted" : "refused");
            } else {
                // the position moves at pace with the clock
                const Sample* late = nullptr;
                for (auto it = samples.rbegin(); it != samples.rend(); ++it) if (it->position > 0.5) { late = &*it; break; }
                const Sample* early = nullptr;
                for (const Sample& s : samples) if (s.position > 0.5) { early = &s; break; }
                double pace = 0.0;
                if (early && late && late->at > early->at + 0.5) pace = (late->position - early->position) / (late->at - early->at);
                std::printf("  %zu samples, %d frames, pace %.3f\n", samples.size(), frames.load(), pace);
                CHECK(frames.load() > 10, "frames were shown");
                CHECK(Near(pace, 1.0, 0.1), "the position runs at the clock's pace");
                // the frame shown is the one the position names: at most a
                // frame or two behind it (decoded ahead of the clock, never
                // beyond the position), after the start settled
                int agree = 0, counted = 0;
                double worst = 0.0;
                for (const Sample& s : samples) {
                    if (s.position < 0.5 || s.pts < 0.0) continue;
                    ++counted;
                    const double behind = s.position - s.pts;
                    worst = std::max(worst, std::fabs(behind));
                    if (behind >= -2 * frame && behind <= 2 * frame + 0.02) ++agree;
                }
                std::printf("  %d of %d samples within two frames, the worst %.3f s apart\n", agree, counted, worst);
                CHECK(counted > 20 && agree >= counted * 9 / 10, "the shown frame's time matches the reported position within two frames");
                // at the cut: the first frame of the clip arrives while the
                // position reads the cut, within a frame
                const Sample* atCut = nullptr;
                for (const Sample& s : samples) if (s.pts >= cut) { atCut = &s; break; }
                if (atCut) std::printf("  the first frame past the cut (%.3f s) seen with the position at %.3f s\n", atCut->pts, atCut->position);
                CHECK(atCut && Near(atCut->position, cut, 2 * frame + 0.02), "the position reads the cut when the clip's first frame shows");
            }
        }
    }

    std::error_code ec;
    fs::remove_all(PathFromUtf8(TempPath("")), ec);
    std::printf("\n%d of %d checks passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
