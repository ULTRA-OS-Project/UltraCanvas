// core/UltraCanvasVideoPlayer.cpp
// Non-visual video playback engine; wraps a backend decode session
// Version: 0.1.3
// Last Modified: 2026-09-07
// V0.1.3: Open() consults the codecs registered through
//   RegisterVideoCodecPlugin, so an application can add a container without
//   patching the platform backend. A registered codec takes precedence for the
//   sources it claims — the backend accepts any source optimistically and only
//   fails later on its bus, so a fallback ordering could never reach one.
// V0.1.2: Open() no longer pushes the default 1.0 playback rate into a freshly
//   opened session. Backends apply a rate via a flushing seek, which — issued
//   while the initial preroll is still settling — flushed away the prerolled
//   first frame, so a session left paused (still-image video preview) showed
//   "Buffering..." instead of the first frame.
// V0.1.1: Play() after the clip ran to end-of-stream rewinds to 0 first — at EOS
//   the pipeline produces no data, so a bare Play() silently did nothing.
// Author: UltraCanvas Framework

#include "UltraCanvasVideoPlayer.h"
#include "../libspecific/Video/IVideoBackend.h"
#include "../libspecific/Video/VideoCodecPlugin.h"
#include <algorithm>
#include <mutex>

namespace UltraCanvas {

struct UltraCanvasVideoPlayer::Impl {
    VideoPlaybackConfig config;
    std::unique_ptr<IVideoDecodeSession> session;
    VideoPlaybackState state = VideoPlaybackState::Idle;
    bool endedAtEos = false;   // playback ran to end-of-stream; Play() must rewind
    std::string lastError;
    VideoStreamInfo emptyInfo;

    mutable std::mutex frameMutex;
    UCVideoFramePtr latestFrame;

    UltraCanvasVideoPlayer* owner = nullptr;

    void SetState(VideoPlaybackState s) {
        if (state == s) return;
        state = s;
        if (owner->onPlaybackStateChanged) owner->onPlaybackStateChanged(s);
    }

    bool Open(const std::string& source) {
        Reset();
        SetState(VideoPlaybackState::Loading);
        VideoDecodeOptions opts;
        opts.disableAudio = config.disableAudio;

        // A codec an application registered goes first — unlike the audio side,
        // where a plugin runs last. The platform video backend does not decline
        // a source it cannot decode: it builds a pipeline and reports the
        // failure asynchronously on its bus, so "backend first, plugin
        // fallback" would never reach a plugin at all. Precedence is safe here
        // because the lookup only matches an extension a plugin explicitly
        // claimed; everything else still goes straight to the backend. A
        // plugin's factory must therefore never call back into the player.
        if (auto openDecoder = FindVideoDecoderFor(source)) {
            session = openDecoder(source, opts);
        }
        if (!session) {
            if (auto* backend = GetVideoBackend()) {
                session = backend->OpenDecoder(source, opts);
            }
        }
        if (!session) {
            Fail(GetVideoBackend() ? "Failed to open video source: " + source
                                   : "No video backend or registered codec can open: " + source);
            return false;
        }

        HookSession();

        // Apply configured properties. The playback rate is only forwarded when
        // it differs from the backend's initial 1.0: backends realise a rate
        // change with a flushing seek, and issuing one while the just-started
        // preroll is still settling discards the prerolled first frame that a
        // session left paused (e.g. a still-image video preview) must show.
        session->SetVolume(config.volume);
        session->SetMute(config.mute);
        session->SetLoop(config.loop);
        if (config.playbackRate != 1.0f)
            session->SetPlaybackRate(config.playbackRate);

        SetState(VideoPlaybackState::Stopped);
        if (config.autoPlay) { session->Play(); SetState(VideoPlaybackState::Playing); }
        return true;
    }

    void HookSession() {
        IVideoDecodeSession* s = session.get();
        s->onLoaded = [this]() {
            if (owner->onLoaded) owner->onLoaded();
        };
        s->onFrame = [this](UCVideoFramePtr f) {
            {
                std::lock_guard<std::mutex> lk(frameMutex);
                latestFrame = f;
            }
            if (owner->onFrameReady) owner->onFrameReady(f);
            if (owner->onPositionChanged) owner->onPositionChanged(f ? f->GetPTS() : 0.0);
        };
        s->onEnded = [this]() {
            endedAtEos = true;
            SetState(VideoPlaybackState::Stopped);
            if (owner->onEnded) owner->onEnded();
        };
        s->onError = [this](const std::string& msg) { Fail(msg); };
    }

    void Fail(const std::string& msg) {
        lastError = msg;
        SetState(VideoPlaybackState::Error);
        if (owner->onError) owner->onError(msg);
    }

    void Reset() {
        session.reset();
        {
            std::lock_guard<std::mutex> lk(frameMutex);
            latestFrame.reset();
        }
        state = VideoPlaybackState::Idle;
        endedAtEos = false;
        lastError.clear();
    }
};

UltraCanvasVideoPlayer::UltraCanvasVideoPlayer() : impl(std::make_unique<Impl>()) {
    impl->owner = this;
}

UltraCanvasVideoPlayer::UltraCanvasVideoPlayer(const VideoPlaybackConfig& cfg)
    : impl(std::make_unique<Impl>()) {
    impl->owner = this;
    impl->config = cfg;
}

UltraCanvasVideoPlayer::~UltraCanvasVideoPlayer() = default;

bool UltraCanvasVideoPlayer::LoadFromFile(const std::string& filePath) {
    return impl->Open(filePath);
}
bool UltraCanvasVideoPlayer::LoadFromUrl(const std::string& url) {
    return impl->Open(url);
}
void UltraCanvasVideoPlayer::Unload() { impl->Reset(); }
bool UltraCanvasVideoPlayer::IsLoaded() const { return impl->session != nullptr; }

bool UltraCanvasVideoPlayer::Play() {
    if (!impl->session) return false;
    // After end-of-stream the pipeline is parked at EOS; setting it back to
    // playing produces no data (GStreamer keeps reporting EOS). Rewind first so
    // Play() after the clip finished restarts it from the top.
    if (impl->endedAtEos) {
        impl->endedAtEos = false;
        impl->session->Seek(0.0);
    }
    bool ok = impl->session->Play();
    if (ok) impl->SetState(VideoPlaybackState::Playing);
    return ok;
}
bool UltraCanvasVideoPlayer::Pause() {
    if (!impl->session) return false;
    bool ok = impl->session->Pause();
    if (ok) impl->SetState(VideoPlaybackState::Paused);
    return ok;
}
bool UltraCanvasVideoPlayer::Stop() {
    if (!impl->session) return false;
    bool ok = impl->session->Stop();       // pauses and rewinds to 0
    if (ok) {
        impl->endedAtEos = false;
        impl->SetState(VideoPlaybackState::Stopped);
    }
    return ok;
}
bool UltraCanvasVideoPlayer::Seek(double seconds) {
    if (!impl->session) return false;
    impl->endedAtEos = false;   // an explicit seek moves playback off the end
    return impl->session->Seek(std::max(0.0, seconds));
}

VideoPlaybackState UltraCanvasVideoPlayer::GetState() const { return impl->state; }
double UltraCanvasVideoPlayer::GetPosition() const {
    return impl->session ? impl->session->GetPosition() : 0.0;
}
double UltraCanvasVideoPlayer::GetDuration() const {
    return impl->session ? impl->session->GetDuration() : 0.0;
}
const VideoStreamInfo& UltraCanvasVideoPlayer::GetStreamInfo() const {
    return impl->session ? impl->session->GetStreamInfo() : impl->emptyInfo;
}
const std::string& UltraCanvasVideoPlayer::GetLastError() const { return impl->lastError; }

UCVideoFramePtr UltraCanvasVideoPlayer::GetCurrentFrame() const {
    std::lock_guard<std::mutex> lk(impl->frameMutex);
    return impl->latestFrame;
}

void UltraCanvasVideoPlayer::SetVolume(float v) {
    impl->config.volume = std::clamp(v, 0.0f, 1.0f);
    if (impl->session) impl->session->SetVolume(impl->config.volume);
}
float UltraCanvasVideoPlayer::GetVolume() const { return impl->config.volume; }
void UltraCanvasVideoPlayer::SetMute(bool mute) {
    impl->config.mute = mute;
    if (impl->session) impl->session->SetMute(mute);
}
bool UltraCanvasVideoPlayer::IsMuted() const { return impl->config.mute; }
void UltraCanvasVideoPlayer::SetLoop(bool loop) {
    impl->config.loop = loop;
    if (impl->session) impl->session->SetLoop(loop);
}
bool UltraCanvasVideoPlayer::IsLoop() const { return impl->config.loop; }
void UltraCanvasVideoPlayer::SetPlaybackRate(float rate) {
    impl->config.playbackRate = std::clamp(rate, 0.25f, 4.0f);
    if (impl->session) impl->session->SetPlaybackRate(impl->config.playbackRate);
}
float UltraCanvasVideoPlayer::GetPlaybackRate() const { return impl->config.playbackRate; }

} // namespace UltraCanvas
