// core/UltraCanvasImageFileWatch.cpp
// Keeps the pictures a view keeps drawing in step with their files. See
// UltraCanvasImageFileWatch.h for why the check is not made on the paint path.
// Version: 1.0.0
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework

#include "UltraCanvasImageFileWatch.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasFileStamp.h"
#include "UltraCanvasImage.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace UltraCanvas {

    UltraCanvasImageFileWatch::UltraCanvasImageFileWatch(std::function<void()> changed,
                                                         int interval)
            : onChanged(std::move(changed)),
              intervalMs(std::max(100, interval)) {}

    UltraCanvasImageFileWatch::~UltraCanvasImageFileWatch() {
        alive->store(false);
        {
            std::lock_guard<std::mutex> lk(mutex);
            shutdown = true;
        }
        cond.notify_all();
        if (worker.joinable()) worker.join();
    }

    void UltraCanvasImageFileWatch::SetDrawnImages(std::vector<DrawnImage> drawn) {
        // Called from every paint: the common case is the same list as last
        // frame, which costs one sort, one comparison and no allocation
        // beyond the caller's own vector.
        drawn.erase(std::remove_if(drawn.begin(), drawn.end(),
                                   [](const DrawnImage& d) { return d.path.empty(); }),
                    drawn.end());
        // Stable, so of a path drawn twice the version drawn last is kept.
        std::stable_sort(drawn.begin(), drawn.end(),
                         [](const DrawnImage& a, const DrawnImage& b) { return a.path < b.path; });
        std::vector<DrawnImage> unique;
        unique.reserve(drawn.size());
        for (DrawnImage& d : drawn) {
            if (!unique.empty() && unique.back().path == d.path) unique.back() = std::move(d);
            else unique.push_back(std::move(d));
        }

        std::lock_guard<std::mutex> lk(mutex);
        const bool same = unique.size() == images.size() &&
                std::equal(unique.begin(), unique.end(), images.begin(),
                           [](const DrawnImage& a, const DrawnImage& b) {
                               return a.path == b.path && a.version == b.version;
                           });
        if (same) return;
        images = std::move(unique);
        if (!images.empty() && !worker.joinable() && !shutdown)
            worker = std::thread([this]() { WorkerMain(); });
    }

    std::vector<UltraCanvasImageFileWatch::DrawnImage>
    UltraCanvasImageFileWatch::GetDrawnImages() const {
        std::lock_guard<std::mutex> lk(mutex);
        return images;
    }

    void UltraCanvasImageFileWatch::WorkerMain() {
        // The file version each picture was last reported changed to: a view
        // told once repaints with the new version and hands it over through
        // SetDrawnImages; until it does (a hidden view does not repaint) the
        // same change is not reported again.
        std::unordered_map<std::string, FileStamp> reported;
        for (;;) {
            std::vector<DrawnImage> toCheck;
            {
                std::unique_lock<std::mutex> lk(mutex);
                cond.wait_for(lk, std::chrono::milliseconds(intervalMs),
                              [this]() { return shutdown; });
                if (shutdown) return;
                toCheck = images;
            }
            // Outside the lock: a stat or two per file, and SetDrawnImages
            // runs on the UI thread, which must never wait for a disk.
            bool changed = false;
            for (const DrawnImage& drawn : toCheck) {
                try {
                    const FileStamp now = StampFile(drawn.path);
                    if (now == drawn.version) {
                        reported.erase(drawn.path);
                        continue;
                    }
                    // The file is not the version the view drew. Drop the
                    // cached copy - another view's watch may have done that
                    // already, which is why the answer is not what decides.
                    UCImage::RemoveFromCacheIfChanged(drawn.path);
                    auto it = reported.find(drawn.path);
                    if (it != reported.end() && it->second == now) continue;
                    reported[drawn.path] = now;
                    changed = true;
                } catch (...) {
                    // A file the platform will not describe is not a change;
                    // the worker must outlive it.
                }
                std::lock_guard<std::mutex> lk(mutex);
                if (shutdown) return;
            }
            // Forget pictures no longer drawn, so the record follows the screen.
            if (!reported.empty()) {
                std::unordered_set<std::string> drawnPaths;
                for (const DrawnImage& d : toCheck) drawnPaths.insert(d.path);
                for (auto it = reported.begin(); it != reported.end();)
                    it = drawnPaths.count(it->first) ? std::next(it) : reported.erase(it);
            }
            if (changed) PostChanged();
        }
    }

    void UltraCanvasImageFileWatch::PostChanged() {
        if (posted->exchange(true)) return;
        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) {
            posted->store(false);
            return;
        }
        auto stillAlive = alive;
        auto stillPosted = posted;
        app->PostToUIThread([this, stillAlive, stillPosted]() {
            stillPosted->store(false);
            if (!stillAlive->load()) return;   // the watch went away meanwhile
            if (onChanged) onChanged();
        });
    }

} // namespace UltraCanvas
