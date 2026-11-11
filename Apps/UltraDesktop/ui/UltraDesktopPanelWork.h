// Apps/UltraDesktop/ui/UltraDesktopPanelWork.h
// Slow calls for a panel, off the UI thread: joining a network or pairing a
// device takes seconds, and the desktop must not freeze meanwhile. `Run`
// starts the call on a thread of its own and hands its answer to `done` on
// the UI thread - unless the panel has gone by then, which a token the
// threads share tells them. `work` must not touch the panel: it captures
// what it needs by value.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasApplication.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

namespace UltraDesktop {

class UltraDesktopPanelWork {
public:
    UltraDesktopPanelWork() : alive_(std::make_shared<std::atomic<bool>>(true)) {}
    ~UltraDesktopPanelWork() { alive_->store(false); }
    UltraDesktopPanelWork(const UltraDesktopPanelWork&) = delete;
    UltraDesktopPanelWork& operator=(const UltraDesktopPanelWork&) = delete;

    template <typename Result>
    void Run(std::function<Result()> work, std::function<void(Result)> done) {
        std::shared_ptr<std::atomic<bool>> alive = alive_;
        std::thread([alive, work = std::move(work), done = std::move(done)]() mutable {
            Result result = work();
            if (!alive->load()) return;
            auto* app = UltraCanvas::UltraCanvasApplicationBase::GetCurrent();
            if (!app) return;
            app->PostToUIThread([alive, done = std::move(done), result = std::move(result)]() mutable {
                if (alive->load() && done) done(std::move(result));
            });
        }).detach();
    }

private:
    std::shared_ptr<std::atomic<bool>> alive_;
};

} // namespace UltraDesktop
