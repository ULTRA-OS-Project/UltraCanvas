// Tests/VideoCodecPluginTest.cpp
// Headless test for the video codec plugin path: that a container registered by
// an application is classified as video, appears in the format inventory, and —
// the part that makes the registration more than a declaration — is actually
// decoded by UltraCanvasVideoPlayer and by CaptureVideoThumbnail, including the
// generic thumbnail fallback for a plugin that supplied only a decoder.
// Uses a synthetic in-process codec, so it needs no media assets and no
// platform video backend. Exits non-zero on any failure.
// Version: 1.0.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework

#include "../UltraCanvas/libspecific/Video/VideoCodecPlugin.h"
#include "UltraCanvasMediaCodecRegistry.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasVideoPlayer.h"
#include "UltraCanvasVideoThumbnail.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool cond, const std::string& what) {
    if (cond) {
        std::printf("  ok   %s\n", what.c_str());
    } else {
        std::printf("  FAIL %s\n", what.c_str());
        ++failures;
    }
}

constexpr int kWidth = 32;
constexpr int kHeight = 16;
constexpr uint8_t kMarkerBlue = 0xC0;

std::atomic<int> sessionsOpened{0};
std::atomic<int> thumbnailGrabs{0};

UCVideoFramePtr MakeFrame(double pts) {
    auto frame = std::make_shared<UCVideoFrame>();
    VideoFrameInfo info;
    info.width = kWidth;
    info.height = kHeight;
    info.pixelFormat = VideoPixelFormat::RGBA32;
    info.stride = kWidth * 4;
    info.pts = pts;
    auto& data = frame->MutableData();
    data.assign(static_cast<size_t>(info.stride) * kHeight, 0);
    // A recognisable marker so a test can tell this codec's frame from anything
    // the platform backend might have produced.
    for (size_t i = 0; i < data.size(); i += 4) {
        data[i] = kMarkerBlue;
        data[i + 3] = 0xFF;
    }
    frame->SetInfo(info);
    return frame;
}

// A decode session that produces one frame as soon as it is played. Enough to
// drive both the player's frame delivery and the generic thumbnail grab, which
// waits on exactly this signal.
class FakeSession : public IVideoDecodeSession {
public:
    FakeSession() {
        info.width = kWidth;
        info.height = kHeight;
        info.frameRate = 25.0;
        info.duration = 4.0;
        info.hasAudio = false;
    }
    ~FakeSession() override { JoinWorker(); }

    bool Play() override {
        JoinWorker();
        worker = std::thread([this] {
            if (onLoaded) onLoaded();
            if (onFrame) onFrame(MakeFrame(position));
        });
        return true;
    }
    bool Pause() override { return true; }
    bool Stop() override { JoinWorker(); return true; }
    bool Seek(double seconds) override { position = seconds; return true; }

    double GetPosition() const override { return position; }
    double GetDuration() const override { return info.duration; }
    const VideoStreamInfo& GetStreamInfo() const override { return info; }

    void SetVolume(float) override {}
    void SetMute(bool) override {}
    void SetLoop(bool) override {}
    void SetPlaybackRate(float) override {}

private:
    void JoinWorker() { if (worker.joinable()) worker.join(); }

    VideoStreamInfo info;
    double position = 0.0;
    std::thread worker;
};

std::unique_ptr<IVideoDecodeSession> OpenFakeSession(const std::string& source,
                                                     const VideoDecodeOptions&) {
    // A real plugin checks the file; this one only refuses a source it was
    // never meant to claim, which is the behaviour the caller relies on.
    if (source.find(".ucvid") == std::string::npos) return nullptr;
    ++sessionsOpened;
    return std::make_unique<FakeSession>();
}

MediaCodecRegistration FakeCodec() {
    MediaCodecRegistration codec;
    codec.extension = "ucvid";
    codec.aliases = { "ucvid2" };
    codec.description = "UltraCanvas test video";
    codec.kind = MediaCodecKind::Video;
    codec.provider = "VideoCodecPluginTest";
    return codec;
}

std::string WriteSourceFile(const std::string& name) {
    const std::string path = "/tmp/" + name;
    std::ofstream f(path, std::ios::binary);
    f << "not really a video, the plugin does not care";
    return path;
}

bool FrameLooksLikeOurs(const UCVideoFramePtr& frame) {
    return frame && frame->IsValid() && frame->GetData() &&
           frame->GetData()[0] == kMarkerBlue;
}

// ===== CASES =====

void TestUnregisteredExtensionIsInert() {
    std::printf("Before registration\n");
    const std::string path = WriteSourceFile("uc-videoplugin-a.ucvid");
    Check(!IsMediaFileOfKind(MediaCodecKind::Video, path),
          "the extension is not video yet");
    Check(!FindVideoDecoderFor(path), "no decoder is registered for it");
    std::remove(path.c_str());
}

void TestRegistrationReachesClassificationAndInventory() {
    std::printf("Registration\n");
    RegisterVideoCodecPlugin(FakeCodec(), OpenFakeSession);

    const std::string path = WriteSourceFile("uc-videoplugin-b.ucvid");
    Check(IsMediaFileOfKind(MediaCodecKind::Video, path), "the file is now video");
    Check(CanDecodeMediaFile(MediaCodecKind::Video, path),
          "supplying a decoder implies canDecode");
    Check(static_cast<bool>(FindVideoDecoderFor(path)), "the decoder is found by path");

    const std::string aliasPath = WriteSourceFile("uc-videoplugin-b.UCVID2");
    Check(static_cast<bool>(FindVideoDecoderFor(aliasPath)),
          "an alias resolves to the same plugin, case-insensitively");

    bool listed = false;
    for (const auto& f : UltraCanvasSupportedFormats::GetByCategory(MediaFormatCategory::Video)) {
        if (f.MatchesExtension("ucvid")) {
            listed = true;
            Check(f.canLoad, "listed as loadable");
            Check(!f.canSave, "not listed as saveable");
            Check(f.provider == "VideoCodecPluginTest", "the provider is the plugin's");
        }
    }
    Check(listed, "the format inventory lists it, so file dialogs offer it");

    std::remove(path.c_str());
    std::remove(aliasPath.c_str());
}

void TestPlayerDecodesThroughThePlugin() {
    std::printf("UltraCanvasVideoPlayer\n");
    const std::string path = WriteSourceFile("uc-videoplugin-c.ucvid");
    sessionsOpened = 0;

    UltraCanvasVideoPlayer player;
    std::atomic<bool> gotFrame{false};
    player.onFrameReady = [&](UCVideoFramePtr f) { if (FrameLooksLikeOurs(f)) gotFrame = true; };

    Check(player.LoadFromFile(path), "LoadFromFile reaches the plugin's factory");
    Check(sessionsOpened == 1, "exactly one session was opened");
    Check(player.IsLoaded(), "the player reports a loaded source");
    Check(player.GetStreamInfo().width == kWidth &&
          player.GetStreamInfo().height == kHeight,
          "stream info comes from the plugin's session");
    Check(player.GetLastError().empty(), "no error was recorded");

    Check(player.Play(), "Play starts the plugin's session");
    for (int i = 0; i < 200 && !gotFrame; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Check(gotFrame, "a decoded frame arrives through onFrameReady");
    Check(FrameLooksLikeOurs(player.GetCurrentFrame()),
          "GetCurrentFrame returns the plugin's frame");

    player.Stop();
    player.Unload();
    std::remove(path.c_str());
}

void TestUnclaimedSourceBypassesThePlugin() {
    std::printf("A source no plugin claims\n");
    // Precedence must not mean interception: an extension no plugin registered
    // has to go straight to the platform backend. (Whether that backend then
    // succeeds is its business — it builds a pipeline and reports an
    // undecodable source later on its bus, which is why the plugin runs first
    // for what it *did* claim.)
    const std::string path = WriteSourceFile("uc-videoplugin-d.ucnothing");
    sessionsOpened = 0;
    thumbnailGrabs = 0;

    Check(!FindVideoDecoderFor(path), "no plugin decoder matches it");
    Check(!FindVideoThumbnailGrabberFor(path), "no plugin grabber matches it");

    UltraCanvasVideoPlayer player;
    player.LoadFromFile(path);
    Check(sessionsOpened == 0, "the plugin's factory was never called");
    player.Unload();

    CaptureVideoThumbnail(path, VideoThumbnailRequest{});
    Check(sessionsOpened == 0 && thumbnailGrabs == 0,
          "and neither was its thumbnail path");
    std::remove(path.c_str());
}

void TestThumbnailFallsBackToTheDecoder() {
    std::printf("CaptureVideoThumbnail, decoder only\n");
    const std::string path = WriteSourceFile("uc-videoplugin-e.ucvid");
    sessionsOpened = 0;
    thumbnailGrabs = 0;

    UCVideoFramePtr frame = CaptureVideoThumbnail(path, VideoThumbnailRequest{});
    Check(FrameLooksLikeOurs(frame),
          "a plugin with no dedicated grabber still produces a thumbnail");
    Check(thumbnailGrabs == 0, "no dedicated grabber was called - there is none");
    Check(sessionsOpened >= 1, "the generic path drove the plugin's factory");
    std::remove(path.c_str());
}

void TestDedicatedThumbnailGrabberWins() {
    std::printf("CaptureVideoThumbnail, dedicated grabber\n");
    // Re-registering adds the grabber; per the upgrade rule the decoder stays.
    RegisterVideoCodecPlugin(FakeCodec(), {},
                             [](const std::string&, const VideoThumbnailRequest&) {
                                 ++thumbnailGrabs;
                                 return MakeFrame(0.0);
                             });

    const std::string path = WriteSourceFile("uc-videoplugin-f.ucvid");
    sessionsOpened = 0;
    thumbnailGrabs = 0;

    UCVideoFramePtr frame = CaptureVideoThumbnail(path, VideoThumbnailRequest{});
    Check(FrameLooksLikeOurs(frame), "the grabber's frame comes back");
    Check(thumbnailGrabs == 1, "the dedicated grabber was used");
    Check(sessionsOpened == 0, "no decode session was opened for it");
    Check(static_cast<bool>(FindVideoDecoderFor(path)),
          "re-registering kept the decoder it already had");

    // And the request's size bound is still applied to a plugin's frame.
    VideoThumbnailRequest small;
    small.maxWidth = 8;
    UCVideoFramePtr scaled = CaptureVideoThumbnail(path, small);
    Check(scaled && scaled->IsValid() && scaled->GetWidth() <= 8,
          "maxWidth is honoured for a plugin frame too");

    std::remove(path.c_str());
}

void TestUnregisterDropsDecodingButNotRecognition() {
    std::printf("Unregistering\n");
    const std::string path = WriteSourceFile("uc-videoplugin-g.ucvid");

    UnregisterVideoCodecPlugin("ucvid");
    Check(!FindVideoDecoderFor(path), "the decoder is gone");
    Check(!FindVideoThumbnailGrabberFor(path), "the grabber is gone");
    Check(IsMediaFileOfKind(MediaCodecKind::Video, path),
          "the format stays recognised - the registry entry is separate");

    UnregisterMediaCodec(MediaCodecKind::Video, "ucvid");
    Check(!IsMediaFileOfKind(MediaCodecKind::Video, path),
          "unregistering the codec too removes it entirely");
    std::remove(path.c_str());
}

} // namespace

int main() {
    std::printf("=== video codec plugin test ===\n");
    TestUnregisteredExtensionIsInert();
    TestRegistrationReachesClassificationAndInventory();
    TestPlayerDecodesThroughThePlugin();
    TestUnclaimedSourceBypassesThePlugin();
    TestThumbnailFallsBackToTheDecoder();
    TestDedicatedThumbnailGrabberWins();
    TestUnregisterDropsDecodingButNotRecognition();

    if (failures == 0) {
        std::printf("=== all checks passed ===\n");
        return 0;
    }
    std::printf("=== %d check(s) failed ===\n", failures);
    return 1;
}
