// Apps/UltraVideo/UltraVideoProxies.h
// Proxy media for the editor (VideoFX_CreateProxy): every video in the bin
// gets a small stand-in, made one at a time on a thread of its own - not the
// preview's worker, so scrubbing never waits behind a proxy - and the
// viewer and the playback draft play the timeline from them. The export
// reads the originals. A clip no taller than a proxy needs none; one already
// made (the cache outlives the session) is ready at once.
//
//   bin import ─ Want(clip) ─ queue ─ worker: VideoFX_CreateProxy ─ Pump() on
//   the UI timer: onDone(clip) ─ the window plans the preview again
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFX.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraVideo {

class ProxyMaker {
public:
    enum class State {
        Unknown,        // never asked for
        Queued,         // waiting its turn
        Making,         // being made: Progress() says how far
        Ready,          // made: the preview plays from it
        NotNeeded,      // no picture, or no taller than a proxy: plays as it is
        Failed          // could not be made (Error() says why): plays as it is
    };

    explicit ProxyMaker(VideoFX::VideoFXProxySettings settings = {});
    ~ProxyMaker();                                  // stops the proxy being made, joins
    ProxyMaker(const ProxyMaker&) = delete;
    ProxyMaker& operator=(const ProxyMaker&) = delete;

    // Queue a clip, unless it is known already; an existing proxy is Ready at once
    void Want(const std::string& clip);
    // Forget clips no longer wanted (out of the bin): queued ones are dropped,
    // one being made is stopped. Made proxies stay in the cache.
    void Keep(const std::vector<std::string>& clips);

    State StateOf(const std::string& clip) const;
    double Progress(const std::string& clip) const;    // 0..1 while Making
    std::string Error(const std::string& clip) const;
    int Pending() const;                                // queued + making
    // "proxy 40 %", "proxy", "" - for the bin tile
    std::string Badge(const std::string& clip) const;

    // The timeline with every clip that has a proxy switched to it
    std::vector<VideoFX::VideoFXSegment> Use(const std::vector<VideoFX::VideoFXSegment>& segments) const;

    // UI thread: report what finished since the last call
    void Pump();
    std::function<void(const std::string& clip, State state)> onDone;
    std::function<void()> onProgress;                   // a proxy started or moved on a tenth: the bin may redraw

    const VideoFX::VideoFXProxySettings& Settings() const { return settings; }

private:
    struct Entry {
        State state = State::Unknown;
        double progress = 0.0;
        std::string error;
    };
    void Loop();

    VideoFX::VideoFXProxySettings settings;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::map<std::string, Entry> entries;
    std::deque<std::string> queue;
    std::string current;                    // being made, "" when idle
    std::atomic<bool> stopCurrent{false};
    bool quit = false;
    std::vector<std::pair<std::string, State>> finished;    // waiting for Pump
    bool progressed = false;
    std::thread thread;
};

} // namespace UltraVideo
