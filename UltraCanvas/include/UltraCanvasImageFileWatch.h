// include/UltraCanvasImageFileWatch.h
// Keeps the pictures a view keeps drawing in step with their files, without
// touching the disk on the paint path.
//
// A view that paints pictures asks the shared image cache for them with
// UCImage::Get(), which is keyed by the path and never looks at the disk -
// that is what makes it cheap enough to call for every tile of every frame.
// The price is that a picture saved over keeps being drawn as it was. Checking
// the file from the paint path is no answer: a stat per tile per frame is
// real cost on a local disk and can stall the UI for as long as a network
// share takes to answer.
//
// So the view hands this watch what it drew, at the end of each paint
// (SetDrawnImages: no I/O, one comparison and a swap under a mutex): each
// picture's path and the version of the file it was read from
// (UCImage::GetSourceStamp() - a file's size and modification time,
// UltraCanvasFileStamp.h, recorded when the image was read). A worker thread
// looks at those files every interval, and when a file is no longer the
// version the view drew, its cached copy is dropped
// (UCImage::RemoveFromCacheIfChanged) and the watch calls onChanged on the UI
// thread, where the view repaints (and relayouts, if picture shapes drive its
// layout); the next Get() reads the file as it is now, and the next paint
// hands over the new version. Comparing against what THIS view drew, rather
// than against the cache, is what lets two views showing one picture both
// repaint: whichever watch drops the cached copy first cannot take the change
// away from the other.
//
// Only what was drawn last is watched, so the cost follows what is on screen,
// not how many pictures the view holds, and it keeps being watched while the
// view sits idle - a picture saved over in an editor while the album just
// shows it is noticed without anything repainting first. The worker is
// started by the first non-empty list, so a view that never draws a picture
// never owns a thread.
//
// Used by UltraCanvasAlbum and UltraCanvasSlideshow. The Filer has its own
// folder watch, and its thumbnail workers read with UCImage::GetFresh().
// Version: 1.0.0
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasFileStamp.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasImageFileWatch {
    public:
        // How often the drawn files are looked at, unless the constructor is
        // told otherwise.
        static constexpr int kDefaultIntervalMs = 1500;

        // `onChanged` runs on the UI thread, after one or more of the watched
        // files changed and their cached copies were dropped. It may capture
        // the view that owns this watch raw: it never runs once the watch is
        // destroyed, and the view's destructor destroys the watch.
        explicit UltraCanvasImageFileWatch(std::function<void()> onChanged,
                                           int intervalMs = kDefaultIntervalMs);
        // Stops and joins the worker. A change already posted to the UI
        // thread is dropped, not delivered.
        ~UltraCanvasImageFileWatch();

        UltraCanvasImageFileWatch(const UltraCanvasImageFileWatch&) = delete;
        UltraCanvasImageFileWatch& operator=(const UltraCanvasImageFileWatch&) = delete;

        // One picture a paint drew: its file, and the version of the file it
        // shows - UCImage::GetSourceStamp() of the image drawn, or an invalid
        // stamp when nothing could be drawn for the path (it is then watched
        // for appearing or being fixed).
        struct DrawnImage {
            std::string path;
            FileStamp   version;
        };

        // What the view drew this frame - call it at the end of each paint.
        // Replaces the previous list; an empty one watches nothing (the
        // worker keeps sleeping). Empty paths are ignored; a path drawn twice
        // is watched once, against the version drawn last.
        void SetDrawnImages(std::vector<DrawnImage> drawn);

        // The pictures being watched now, sorted by path, for tests.
        std::vector<DrawnImage> GetDrawnImages() const;

    private:
        void WorkerMain();
        void PostChanged();

        std::function<void()> onChanged;
        int intervalMs;

        mutable std::mutex mutex;
        std::condition_variable cond;
        std::vector<DrawnImage> images;   // guarded by mutex, sorted by path
        bool shutdown = false;            // guarded by mutex
        std::thread worker;               // started by the first non-empty list

        // Flipped by the destructor so a change already queued on the UI
        // thread does not call into a view that is gone.
        std::shared_ptr<std::atomic<bool>> alive =
                std::make_shared<std::atomic<bool>>(true);
        // One queued UI task covers however many changes land before it runs.
        std::shared_ptr<std::atomic<bool>> posted =
                std::make_shared<std::atomic<bool>>(false);
    };

} // namespace UltraCanvas
