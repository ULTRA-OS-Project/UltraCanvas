// UltraAI/adapters/_shared/include/UltraAIStreamHandleBase.h
// Worker-thread-safe IStreamHandle base for streaming adapters, per the
// cancellation pattern in Docs/UltraNetIntegration.md §5. Transport-neutral:
// the cancel side effect (e.g. UltraNet_CancelRequest) is injected as a
// hook, so the same base serves UltraNet, local-inference, and test
// adapters.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <atomic>
#include <functional>
#include <mutex>

namespace UltraAI {

class StreamHandleBase : public IStreamHandle {
public:
    // Cancel is idempotent and thread-safe: the flag flips exactly once and
    // the hook runs at most once, even under concurrent calls. Safe to call
    // before a hook is installed — the hook then runs on installation.
    void Cancel() override {
        std::function<void()> hook;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cancelled_.exchange(true)) return;
            hook = std::move(cancelHook_);
            cancelHook_ = nullptr;
        }
        if (hook) hook();
    }

    bool IsDone() const override { return done_.load(); }
    bool IsCancelled() const { return cancelled_.load(); }

    // Called by the adapter when the terminal Done / Error event has been
    // delivered.
    void MarkDone() { done_.store(true); }

    // Install the transport-specific cancel side effect. If Cancel() already
    // ran, the hook fires immediately (covers the race where the user
    // cancels between issuing the request and learning its handle).
    void SetCancelHook(std::function<void()> hook) {
        bool runNow = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cancelled_.load()) {
                runNow = true;
            } else {
                cancelHook_ = std::move(hook);
            }
        }
        if (runNow && hook) hook();
    }

private:
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> done_{false};
    std::mutex mutex_;
    std::function<void()> cancelHook_;
};

} // namespace UltraAI
