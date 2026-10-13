// Apps/UltraVideo/UltraVideoProxies.cpp
// Proxy media for the editor: see UltraVideoProxies.h.
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraVideoProxies.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace UltraVideo {

using namespace VideoFX;

ProxyMaker::ProxyMaker(VideoFXProxySettings s) : settings(std::move(s)) {
    thread = std::thread([this]() { Loop(); });
}

ProxyMaker::~ProxyMaker() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        quit = true;
        queue.clear();
    }
    stopCurrent = true;
    wake.notify_all();
    if (thread.joinable()) thread.join();
}

void ProxyMaker::Want(const std::string& clip) {
    if (clip.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = entries.find(clip);
        if (it != entries.end() && it->second.state != State::Unknown) return;
    }
    // Made in an earlier session: nothing to wait for (a stat, not a probe)
    const bool made = VideoFX_HasProxy(clip, settings);
    std::lock_guard<std::mutex> lock(mutex);
    Entry& e = entries[clip];
    if (e.state != State::Unknown) return;          // another call got here first
    if (made) {
        e.state = State::Ready;
        e.progress = 1.0;
        finished.emplace_back(clip, State::Ready);
        return;
    }
    e.state = State::Queued;
    queue.push_back(clip);
    wake.notify_one();
}

void ProxyMaker::Keep(const std::vector<std::string>& clips) {
    const std::set<std::string> keep(clips.begin(), clips.end());
    std::lock_guard<std::mutex> lock(mutex);
    queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const std::string& c) { return !keep.count(c); }),
                queue.end());
    if (!current.empty() && !keep.count(current)) stopCurrent = true;
    for (auto it = entries.begin(); it != entries.end();) {
        // a clip that comes back is looked at afresh; the one being made is
        // left for the worker to settle
        if (!keep.count(it->first) && it->first != current) it = entries.erase(it);
        else ++it;
    }
}

ProxyMaker::State ProxyMaker::StateOf(const std::string& clip) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(clip);
    return it == entries.end() ? State::Unknown : it->second.state;
}

double ProxyMaker::Progress(const std::string& clip) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(clip);
    return it == entries.end() ? 0.0 : it->second.progress;
}

std::string ProxyMaker::Error(const std::string& clip) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(clip);
    return it == entries.end() ? std::string() : it->second.error;
}

int ProxyMaker::Pending() const {
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int>(queue.size()) + (current.empty() ? 0 : 1);
}

std::string ProxyMaker::Badge(const std::string& clip) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(clip);
    if (it == entries.end()) return "";
    switch (it->second.state) {
        case State::Queued: return "proxy waiting";
        case State::Making: return "proxy " + std::to_string(static_cast<int>(std::lround(it->second.progress * 100))) + " %";
        case State::Ready: return "proxy";
        default: return "";
    }
}

std::vector<VideoFXSegment> ProxyMaker::Use(const std::vector<VideoFXSegment>& segments) const {
    return VideoFX_UseProxies(segments, settings);
}

void ProxyMaker::Pump() {
    std::vector<std::pair<std::string, State>> done;
    bool moved = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        done.swap(finished);
        moved = progressed;
        progressed = false;
    }
    if (moved && onProgress) onProgress();
    for (const auto& [clip, state] : done)
        if (onDone) onDone(clip, state);
}

void ProxyMaker::Loop() {
    while (true) {
        std::string clip;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this]() { return quit || !queue.empty(); });
            if (quit) return;
            clip = queue.front();
            queue.pop_front();
            current = clip;
            stopCurrent = false;
            entries[clip].state = State::Making;
            entries[clip].progress = 0.0;
            progressed = true;
        }
        std::string editPath, error;
        double reported = 0.0;
        const VideoFXResult r = VideoFX_CreateProxy(clip, settings, [&](double f) {
            if (stopCurrent) return false;
            if (f - reported >= 0.1) {              // the bin redraws every tenth, not every frame
                reported = f;
                std::lock_guard<std::mutex> lock(mutex);
                auto it = entries.find(clip);
                if (it != entries.end()) it->second.progress = f;
                progressed = true;
            }
            return true;
        }, &editPath);
        if (r != VideoFXResult::Ok) error = VideoFX_GetLastError();

        std::lock_guard<std::mutex> lock(mutex);
        current.clear();
        auto it = entries.find(clip);
        if (it == entries.end()) continue;              // forgotten meanwhile (Keep)
        if (r == VideoFXResult::Cancelled) {
            entries.erase(it);                          // stopped: asked for again, it is made again
            continue;
        }
        State state = State::Failed;
        if (r == VideoFXResult::Ok) state = editPath == clip ? State::NotNeeded : State::Ready;
        it->second.state = state;
        it->second.progress = state == State::Failed ? 0.0 : 1.0;
        it->second.error = error;
        finished.emplace_back(clip, state);
    }
}

} // namespace UltraVideo
