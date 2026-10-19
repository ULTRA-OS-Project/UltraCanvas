// core/UltraNet/UltraNetHttpAsync.cpp
// Async HTTP via curl_multi on a singleton worker thread. Also implements
// UltraNet_CancelRequest / IsRequestActive / GetTransferStats (declared in
// UltraNetCore.h) because they operate on the same async request registry.
//
// Threading model:
//   - One worker thread per process, lazily spawned on first Enqueue.
//   - Per-request onComplete callback fires on the worker thread; callers
//     must marshal back to their own loop and must not block in the cb.
//   - All shared state (request maps, pending queues, running flag) lives
//     under one mutex; the worker drops the lock for curl_multi_perform /
//     curl_multi_poll so other threads can Enqueue / Cancel without waiting
//     on network I/O.
//   - UltraNet_Shutdown drives StopAsync() via the internal hook below so
//     the worker thread is joined before curl_global_cleanup runs.
//   - Pause / resume: UltraNet_PauseRequest sets the request's wish and
//     wakes the worker, which applies it between multi calls (curl_easy_pause
//     wants the transfer's connection to exist, so a wish made before the
//     connect is kept and applied on a later turn). The stall check is
//     switched off while paused.
// Version: 0.3.0 (Stage 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNetHttpEasy.h"

#include <curl/curl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ultranet_internal {
    // Called by UltraNet_Shutdown — joins the async worker (if running)
    // before libcurl global state is torn down.
    void StopAsyncWorker();
}

namespace {

using ultranet_internal::WriteSink;

struct AsyncRequest {
    UltraNetHandle handle = UltraNetInvalidHandle;
    CURL*          easy   = nullptr;
    curl_slist*    slist  = nullptr;

    std::vector<uint8_t> bodyOwned;     // keeps request body alive for libcurl
    std::string          url;
    UltraNetResponse     response;
    WriteSink            sink;

    std::function<void(const UltraNetResponse&)> onComplete;
    std::atomic<bool> cancelledBeforeStart{false};
    std::atomic<bool> wantPaused{false};   // what the caller asked for
    std::atomic<bool> paused{false};       // what libcurl was told

    ~AsyncRequest() {
        sink.CloseFile();
        if (slist) curl_slist_free_all(slist);
        if (easy)  curl_easy_cleanup(easy);
    }
};

class MultiWorker {
public:
    static MultiWorker& Instance() {
        static MultiWorker w;
        return w;
    }

    UltraNetHandle Enqueue(const UltraNetHttpRequest& req,
                           std::function<void(const UltraNetResponse&)> cb) {
        if (!UltraNet_IsInitialized()) UltraNet_Initialize();

        auto a = std::make_unique<AsyncRequest>();
        a->handle     = nextHandle_.fetch_add(1, std::memory_order_relaxed);
        a->easy       = curl_easy_init();
        if (!a->easy) {
            if (cb) {
                UltraNetResponse r;
                r.statusMessage = "curl_easy_init() failed";
                r.transferError = r.statusMessage;
                cb(r);
            }
            return UltraNetInvalidHandle;
        }
        a->url        = req.url;
        a->onComplete = std::move(cb);
        a->bodyOwned  = req.body;
        a->sink.body  = &a->response.body;

        // The body goes to the file when the options name one; it is closed
        // before onComplete fires, so the callback can open it.
        const UltraNetResult opened =
            ultranet_internal::OpenOutputFile(req.options, a->sink);
        if (!opened) {
            if (a->onComplete) {
                UltraNetResponse r;
                r.statusMessage = opened.message;
                r.transferError = r.statusMessage;
                a->onComplete(r);
            }
            return UltraNetInvalidHandle;
        }
        if (a->sink.ownsFile) a->sink.body = nullptr;

        const UltraNetConfig cfg = UltraNet_GetConfig();
        a->slist = ultranet_internal::ConfigureEasyHandle(
            a->easy, req, cfg, &a->sink, &a->response);

        if (!a->bodyOwned.empty()) {
            curl_easy_setopt(a->easy, CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(a->bodyOwned.size()));
            curl_easy_setopt(a->easy, CURLOPT_POSTFIELDS, a->bodyOwned.data());
        }

        const UltraNetHandle handle = a->handle;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            EnsureThreadLocked();
            pendingAdd_.push_back(std::move(a));
        }
        if (multi_) curl_multi_wakeup(multi_);
        return handle;
    }

    UltraNetResult Cancel(UltraNetHandle h) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (owned_.count(h)) {
            pendingCancel_.push_back(h);
            if (multi_) curl_multi_wakeup(multi_);
            return UltraNetResult::Ok();
        }
        for (auto& p : pendingAdd_) {
            if (p && p->handle == h) {
                p->cancelledBeforeStart.store(true, std::memory_order_release);
                return UltraNetResult::Ok();
            }
        }
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no such request handle");
    }

    bool IsActive(UltraNetHandle h) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (owned_.count(h)) return true;
        for (const auto& p : pendingAdd_) if (p && p->handle == h) return true;
        return false;
    }

    UltraNetResult SetPaused(UltraNetHandle h, bool pause) {
        std::lock_guard<std::mutex> lk(mutex_);
        AsyncRequest* a = nullptr;
        auto it = owned_.find(h);
        if (it != owned_.end()) {
            a = it->second.get();
        } else {
            for (auto& p : pendingAdd_) if (p && p->handle == h) { a = p.get(); break; }
        }
        if (!a) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                         "no such request handle");
        }
        a->wantPaused.store(pause, std::memory_order_release);
        if (multi_) curl_multi_wakeup(multi_);
        return UltraNetResult::Ok();
    }

    bool IsPaused(UltraNetHandle h) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = owned_.find(h);
        if (it != owned_.end()) return it->second->paused.load(std::memory_order_acquire);
        for (const auto& p : pendingAdd_) {
            if (p && p->handle == h) return p->wantPaused.load(std::memory_order_acquire);
        }
        return false;
    }

    UltraNetTransferStats GetStats(UltraNetHandle h) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = owned_.find(h);
        if (it == owned_.end()) return {};
        return ultranet_internal::ReadTransferStats(it->second->easy);
    }

    void Stop() {
        std::thread join;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!running_) return;
            running_ = false;
            join = std::move(thread_);
        }
        if (multi_) curl_multi_wakeup(multi_);
        if (join.joinable()) join.join();
        if (multi_) { curl_multi_cleanup(multi_); multi_ = nullptr; }
    }

private:
    MultiWorker() = default;
    ~MultiWorker() { Stop(); }
    MultiWorker(const MultiWorker&) = delete;
    MultiWorker& operator=(const MultiWorker&) = delete;

    // Must be called with mutex_ held.
    void EnsureThreadLocked() {
        if (running_) return;
        if (!multi_) multi_ = curl_multi_init();
        running_ = true;
        thread_ = std::thread(&MultiWorker::Loop, this);
    }

    void Loop() {
        while (true) {
            std::vector<std::unique_ptr<AsyncRequest>> adds;
            std::vector<UltraNetHandle>                cancels;
            bool keepRunning;
            bool anyOwned;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                adds.swap(pendingAdd_);
                cancels.swap(pendingCancel_);
                keepRunning = running_;
                anyOwned    = !owned_.empty();
            }

            // Process adds.
            for (auto& a : adds) {
                if (!a) continue;
                if (a->cancelledBeforeStart.load(std::memory_order_acquire)) {
                    InvokeCancelled(*a);
                    continue;
                }
                CURL* easy = a->easy;
                UltraNetHandle h = a->handle;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    byEasy_[easy] = h;
                    owned_[h]     = std::move(a);
                }
                curl_multi_add_handle(multi_, easy);
            }

            // Process cancels.
            for (UltraNetHandle h : cancels) {
                std::unique_ptr<AsyncRequest> victim;
                CURL* easy = nullptr;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    auto it = owned_.find(h);
                    if (it == owned_.end()) continue;
                    easy   = it->second->easy;
                    victim = std::move(it->second);
                    owned_.erase(it);
                    byEasy_.erase(easy);
                }
                if (easy) curl_multi_remove_handle(multi_, easy);
                InvokeCancelled(*victim);
            }

            // Apply pause wishes. curl_easy_pause needs the connection to
            // exist, so one that cannot be applied yet is tried again on
            // the next turn; the stall check is off while paused.
            {
                std::vector<std::pair<AsyncRequest*, bool>> wishes;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    for (auto& [h, a] : owned_) {
                        const bool want = a->wantPaused.load(std::memory_order_acquire);
                        if (want != a->paused.load(std::memory_order_acquire)) {
                            wishes.emplace_back(a.get(), want);
                        }
                    }
                }
                for (auto& [a, want] : wishes) {
                    if (curl_easy_pause(a->easy, want ? CURLPAUSE_ALL : CURLPAUSE_CONT) != CURLE_OK) {
                        continue;
                    }
                    if (a->sink.stallTimeoutMs > 0) {
                        curl_easy_setopt(a->easy, CURLOPT_LOW_SPEED_TIME,
                                         want ? 0L
                                              : static_cast<long>((a->sink.stallTimeoutMs + 999) / 1000));
                    }
                    a->paused.store(want, std::memory_order_release);
                }
            }

            int running = 0;
            curl_multi_perform(multi_, &running);

            // Reap completions.
            int msgsLeft = 0;
            CURLMsg* msg = nullptr;
            while ((msg = curl_multi_info_read(multi_, &msgsLeft))) {
                if (msg->msg != CURLMSG_DONE) continue;
                CURL* easy   = msg->easy_handle;
                CURLcode rc  = msg->data.result;

                std::unique_ptr<AsyncRequest> done;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    auto eit = byEasy_.find(easy);
                    if (eit == byEasy_.end()) continue;
                    UltraNetHandle h = eit->second;
                    auto oit = owned_.find(h);
                    if (oit == owned_.end()) { byEasy_.erase(eit); continue; }
                    done = std::move(oit->second);
                    owned_.erase(oit);
                    byEasy_.erase(eit);
                }

                done->sink.CloseFile();
                UltraNetResult finalRes = ultranet_internal::FinalizeFromEasy(
                    easy, rc, done->url, done->response, done->sink);
                if (!finalRes && done->response.statusMessage.empty()) {
                    done->response.statusMessage = finalRes.message;
                }
                curl_multi_remove_handle(multi_, easy);

                if (done->onComplete) done->onComplete(done->response);
                // ~AsyncRequest destroys easy + slist after callback returns.
            }

            // Termination: only exit when shutdown was requested AND we
            // have drained every live request.
            if (!keepRunning && !anyOwned && adds.empty() && cancels.empty()) {
                std::lock_guard<std::mutex> lk(mutex_);
                if (!running_ && owned_.empty() &&
                    pendingAdd_.empty() && pendingCancel_.empty()) {
                    break;
                }
            }

            const int timeoutMs = running > 0 ? 200 : 1000;
            curl_multi_poll(multi_, nullptr, 0, timeoutMs, nullptr);
        }
    }

    void InvokeCancelled(AsyncRequest& a) {
        a.sink.CloseFile();   // a partial file is closed before the caller hears
        UltraNetResponse r;
        r.statusCode    = 0;
        r.statusMessage = "Cancelled";
        r.transferError = r.statusMessage;
        if (a.onComplete) a.onComplete(r);
    }

    CURLM* multi_ = nullptr;
    std::thread thread_;

    std::mutex mutex_;
    bool running_ = false;
    std::atomic<UltraNetHandle> nextHandle_{1};

    // All four maps below are protected by mutex_.
    std::unordered_map<UltraNetHandle, std::unique_ptr<AsyncRequest>> owned_;
    std::unordered_map<CURL*, UltraNetHandle> byEasy_;
    std::vector<std::unique_ptr<AsyncRequest>> pendingAdd_;
    std::vector<UltraNetHandle>                pendingCancel_;
};

} // namespace

namespace ultranet_internal {
    void StopAsyncWorker() { MultiWorker::Instance().Stop(); }
}

UltraNetHandle UltraNet_HttpRequestAsync(
    const UltraNetHttpRequest& request,
    std::function<void(const UltraNetResponse&)> onComplete) {
    if (request.url.empty()) {
        if (onComplete) {
            UltraNetResponse r;
            r.statusMessage = "URL is empty";
            r.transferError = r.statusMessage;
            onComplete(r);
        }
        return UltraNetInvalidHandle;
    }
    return MultiWorker::Instance().Enqueue(request, std::move(onComplete));
}

UltraNetResult UltraNet_CancelRequest(UltraNetHandle handle) {
    if (handle == UltraNetInvalidHandle) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "invalid handle");
    }
    return MultiWorker::Instance().Cancel(handle);
}

bool UltraNet_IsRequestActive(UltraNetHandle handle) {
    if (handle == UltraNetInvalidHandle) return false;
    return MultiWorker::Instance().IsActive(handle);
}

UltraNetTransferStats UltraNet_GetTransferStats(UltraNetHandle handle) {
    if (handle == UltraNetInvalidHandle) return {};
    return MultiWorker::Instance().GetStats(handle);
}

UltraNetResult UltraNet_PauseRequest(UltraNetHandle handle) {
    if (handle == UltraNetInvalidHandle) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "invalid handle");
    }
    return MultiWorker::Instance().SetPaused(handle, true);
}

UltraNetResult UltraNet_ResumeRequest(UltraNetHandle handle) {
    if (handle == UltraNetInvalidHandle) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "invalid handle");
    }
    return MultiWorker::Instance().SetPaused(handle, false);
}

bool UltraNet_IsRequestPaused(UltraNetHandle handle) {
    if (handle == UltraNetInvalidHandle) return false;
    return MultiWorker::Instance().IsPaused(handle);
}
