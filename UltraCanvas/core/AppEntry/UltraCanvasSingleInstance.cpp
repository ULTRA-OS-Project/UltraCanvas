// core/AppEntry/UltraCanvasSingleInstance.cpp
// One running copy per user: the lock, the hand-off to the copy already
// running, and bringing its window forward. See UltraCanvasSingleInstance.h.
//
// Compiled into each application that calls ultracanvas_single_instance()
// (cmake/UltraCanvasSingleInstance.cmake), never into the core library: the
// hand-off needs UltraMessage, a separate module the core does not link, and
// the function defines ULTRACANVAS_SINGLE_INSTANCE_HAVE_ULTRAMESSAGE where
// the build has it.
// Version: 1.4.0 - a listening copy that lost the bus (the process hosting
//                  the broker ended) is on it again within a second, hosting
//                  it when nobody does - the endpoint reconnects by itself
//                  (UltraMsgConnectOptions::autoReconnect) and the watch
//                  joins a copy that never got on; it stayed deaf until it
//                  was restarted, and every later start said "already
//                  running" - a pinned UltraSnip preloaded by the desktop, say
// Version: 1.3.0 - a start that cannot hand over retries the lock while it
//                  waits: a copy whose window has closed but whose process is
//                  still ending no longer makes it say "already running"
// Version: 1.2.0 - the answer goes out from the bus thread, so a busy window
//                  no longer makes the other start wait and give up
// Version: 1.1.0 - MakeOpenRequest / RequestPaths / ListenOpen
// Version: 1.0.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework

#include "UltraCanvasSingleInstance.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasModalDialog.h"   // UltraCanvasDialogManager
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasWindow.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

#ifdef ULTRACANVAS_SINGLE_INSTANCE_HAVE_ULTRAMESSAGE
#include "UltraMessage/UltraMessage.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#endif

namespace UltraCanvas {

    UltraCanvasSingleInstance::UltraCanvasSingleInstance(std::string appId, std::string displayName)
        : appId_(std::move(appId)), displayName_(std::move(displayName)) {}

    UltraCanvasSingleInstance::~UltraCanvasSingleInstance() {
        Close();
    }

#ifdef ULTRACANVAS_SINGLE_INSTANCE_HAVE_ULTRAMESSAGE
    namespace {

        // The running copy answers from its UI thread, before it acts.
        constexpr int kAnswerTimeoutMs = 3000;
        // A copy started a moment earlier may still be opening and not listen
        // yet; a later start keeps asking for this long. Longer than the
        // Windows exit watchdog's 10 s, so a start made just after a close
        // outlasts a copy that never finishes ending, and then starts.
        constexpr int kStartingGraceMs = 15000;
        constexpr int kRetryPauseMs = 250;

        // Each application's endpoint has an app id of its own, ".instance"
        // after the application's, so an endpoint the application keeps for
        // something else (UltraMail's feed publisher) is never asked.
        std::string EndpointAppId(const std::string& appId) { return appId + ".instance"; }
        std::string ShowTopic(const std::string& appId) { return appId + ".show"; }

        // `quiet` keeps the retry loop from saying the same thing forty times.
        UltraMsgHandle Connect(const std::string& appId, const std::string& displayName,
                               bool hostBus, bool quiet) {
            UltraMsgConnectOptions options;
            options.appId = EndpointAppId(appId);
            options.displayName = displayName;
            options.startBrokerIfAbsent = hostBus;   // the first copy hosts, so the next start finds it
            UltraMsgResult error;
            const UltraMsgHandle endpoint = UltraMsg_Connect(options, &error);
            if (endpoint == UltraMsgInvalidHandle && !quiet)
                debugOutput << displayName << ": single instance: no UltraMessage bus ("
                            << error.message << ")" << std::endl;
            return endpoint;
        }

    } // namespace

    UltraCanvasSingleInstance::Start UltraCanvasSingleInstance::HandOff(const JSONValue& request,
                                                                        const std::string& lockPath) {
        const JSONValue body = request.IsObject() ? request : JSONValue::MakeObject();
        bool firstTry = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kStartingGraceMs);
        do {
            // The copy holding the lock may be on its way out - its window
            // closed, its process still ending - and no longer answers. Once
            // it is gone the lock is free, and this start is the application.
            if (!firstTry && lock_.Acquire(lockPath) == InstanceLockResult::Acquired) {
                debugOutput << displayName_ << ": the running copy has ended; starting" << std::endl;
                return Start::First;
            }
            // The running copy hosts the bus once it listens, so this start
            // never hosts one of its own.
            if (endpoint_ == 0) {
                const UltraMsgHandle endpoint = Connect(appId_, displayName_, false, !firstTry);
                if (endpoint != UltraMsgInvalidHandle) endpoint_ = endpoint;
            }
            if (endpoint_ != 0) {
                UltraMsgEndpointInfo self;
                std::vector<std::string> instances;
                if (UltraMsg_GetEndpointInfo(endpoint_, self) &&
                    UltraMsg_ResolveApp(endpoint_, EndpointAppId(appId_), instances)) {
                    for (const std::string& instance : instances) {
                        if (instance == self.instanceId) continue;
                        UltraMsgMessage reply;
                        if (UltraMsg_Request(endpoint_, instance, ShowTopic(appId_), body, kAnswerTimeoutMs, reply))
                            return Start::HandedOff;
                    }
                }
            }
            firstTry = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryPauseMs));
        } while (std::chrono::steady_clock::now() < deadline);
        if (lock_.Acquire(lockPath) == InstanceLockResult::Acquired) return Start::First;
        return Start::AlreadyRunning;
    }

    bool UltraCanvasSingleInstance::JoinBus(bool quiet) {
        if (endpoint_ == 0) {
            const UltraMsgHandle endpoint = Connect(appId_, displayName_, true, quiet);
            if (endpoint == UltraMsgInvalidHandle) return false;
            endpoint_ = endpoint;
        }
        // The answer goes out from the bus thread at once, and only the
        // handler waits for the UI thread. A window that is busy - a native
        // dialog that blocks the loop, a long load - would otherwise leave the
        // other start waiting until it gave up and said "already running";
        // now it quits at once and the request is carried out when the window
        // is free again.
        const auto run = handler_;
        const auto live = live_;
        const std::string name = displayName_;
        const UltraMsgHandle endpoint = endpoint_;
        UltraMsgSubscribeOptions options;
        options.onWorkerThread = true;
        const UltraMsgHandle subscription = UltraMsg_Subscribe(endpoint, ShowTopic(appId_),
            [endpoint, run, live, name](const UltraMsgMessage& message) {
                UltraMsg_Reply(endpoint, message, JSONValue::MakeObject());
                auto* app = UltraCanvasApplicationBase::GetCurrent();
                if (!app) return;
                app->PostToUIThread([run, live, name, body = message.body]() {
                    if (!live->load()) return;   // Close() came first
                    debugOutput << name << ": another start asked for the window" << std::endl;
                    if (*run) (*run)(body);
                });
            },
            options);
        if (subscription == UltraMsgInvalidHandle) return false;
        subscription_ = subscription;
        return true;
    }

    void UltraCanvasSingleInstance::WatchBus() {
        std::unique_lock<std::mutex> wait(watchMutex_);
        while (!watchWake_.wait_for(wait, std::chrono::seconds(1), [this] { return watchStop_; })) {
            wait.unlock();
            {
                std::lock_guard<std::mutex> bus(busMutex_);
                // An endpoint that loses the bus - the process hosting the
                // broker ended - reconnects by itself and keeps its
                // subscription (UltraMsgConnectOptions::autoReconnect). What
                // is left here is a copy that never got onto the bus, or
                // whose subscription was refused: it tries again.
                if (subscription_ == 0) {
                    if (endpoint_ != 0 && !UltraMsg_IsConnected(endpoint_)) {
                        UltraMsg_Disconnect(endpoint_);
                        endpoint_ = 0;
                    }
                    if (JoinBus(true))
                        debugOutput << displayName_ << ": single instance: joined the bus" << std::endl;
                }
            }
            wait.lock();
        }
    }

    void UltraCanvasSingleInstance::Listen(Handler handler) {
        if (watch_.joinable() || !lock_.IsHeld()) return;
        handler_ = std::make_shared<Handler>(std::move(handler));
        live_ = std::make_shared<std::atomic<bool>>(true);
        {
            std::lock_guard<std::mutex> bus(busMutex_);
            JoinBus(false);
        }
        // Even when the bus could not be reached now: the watch keeps trying.
        {
            std::lock_guard<std::mutex> wait(watchMutex_);
            watchStop_ = false;
        }
        watch_ = std::thread([this] { WatchBus(); });
    }

    void UltraCanvasSingleInstance::Close() {
        {
            std::lock_guard<std::mutex> wait(watchMutex_);
            watchStop_ = true;
        }
        watchWake_.notify_all();
        if (watch_.joinable()) watch_.join();
        std::lock_guard<std::mutex> bus(busMutex_);
        if (subscription_ != 0) UltraMsg_Unsubscribe(subscription_);
        subscription_ = 0;
        if (endpoint_ != 0) UltraMsg_Disconnect(endpoint_);
        endpoint_ = 0;
        if (live_) live_->store(false);
        live_.reset();
    }
#else
    UltraCanvasSingleInstance::Start UltraCanvasSingleInstance::HandOff(const JSONValue&,
                                                                        const std::string& lockPath) {
        // Nothing to hand to: the lock alone decides, once more.
        return lock_.Acquire(lockPath) == InstanceLockResult::Acquired ? Start::First : Start::AlreadyRunning;
    }
    void UltraCanvasSingleInstance::Listen(Handler) {}
    void UltraCanvasSingleInstance::Close() {}
#endif

    UltraCanvasSingleInstance::Start UltraCanvasSingleInstance::Claim(const JSONValue& request,
                                                                      const std::string& lockDirectory) {
        std::string directory = lockDirectory;
        if (directory.empty()) {
            const std::filesystem::path settings = UltraCanvasSettingsFolder();
            if (settings.empty()) return Start::First;   // no per-user folder: nowhere to lock
            directory = PathToUtf8(settings / "instances");
        }
        std::string error;
        const std::string lockPath = directory + "/" + appId_ + ".lock";
        switch (lock_.Acquire(lockPath, &error)) {
            case InstanceLockResult::Acquired:
            case InstanceLockResult::Unsupported:
                return Start::First;
            case InstanceLockResult::Failed:
                // Better two copies than none.
                debugOutput << displayName_ << ": the instance lock could not be taken (" << error
                            << "); starting without it" << std::endl;
                return Start::First;
            case InstanceLockResult::HeldElsewhere:
                break;
        }
        debugOutput << displayName_ << " is already running; asking it to come forward" << std::endl;
        const Start start = HandOff(request, lockPath);
        Close();
        return start;
    }

    void UltraCanvasSingleInstance::ListenRaise() {
        Listen([](const JSONValue&) { BringToFront(); });
    }

    void UltraCanvasSingleInstance::ListenOpen(OpenHandler openPaths) {
        Listen([openPaths = std::move(openPaths)](const JSONValue& request) {
            const std::vector<std::string> paths = RequestPaths(request);
            if (paths.empty() || !openPaths) {
                BringToFront();
                return;
            }
            openPaths(paths);
        });
    }

    JSONValue UltraCanvasSingleInstance::MakeOpenRequest(const std::vector<std::string>& paths) {
        JSONValue request = JSONValue::MakeObject();
        JSONValue list = JSONValue::MakeArray();
        for (const std::string& path : paths) {
            if (path.empty()) continue;
            if (path.find("://") != std::string::npos) {
                list.Append(JSONValue(path));
                continue;
            }
            std::error_code ec;
            const std::filesystem::path absolute = std::filesystem::absolute(PathFromUtf8(path), ec);
            list.Append(JSONValue(ec ? path : PathToUtf8(absolute.lexically_normal())));
        }
        request.Set("paths", list);
        return request;
    }

    std::vector<std::string> UltraCanvasSingleInstance::RequestPaths(const JSONValue& request) {
        std::vector<std::string> paths;
        const JSONValue& list = request.Get("paths");
        if (!list.IsArray()) return paths;
        for (size_t i = 0; i < list.GetSize(); ++i) {
            const JSONValue& path = list.At(i);
            if (path.IsString() && !path.GetString().empty()) paths.push_back(path.GetString());
        }
        return paths;
    }

    void UltraCanvasSingleInstance::BringToFront() {
        // An open dialog keeps what is typed in it: it is what comes forward.
        const auto dialogs = UltraCanvasDialogManager::GetActiveDialogs();
        auto* app = UltraCanvasApplication::GetInstance();
        UltraCanvasWindowBase* main = nullptr;
        if (app) {
            for (const auto& window : app->GetWindows()) {
                if (!window || !window->IsCreated() || window->GetParentWindow()) continue;
                if (window->GetConfig().type == WindowType::Tooltip || window->GetConfig().type == WindowType::Popup) continue;
                const WindowState state = window->GetState();
                if (state == WindowState::Closing || state == WindowState::Closed) continue;
                main = window.get();
                break;
            }
        }
        if (main) {
            if (main->IsMinimized()) main->Restore();
            else if (main->GetState() == WindowState::Hidden) main->Show();
        }
        if (!dialogs.empty()) {
            dialogs.back()->RaiseAndFocus();
            return;
        }
        if (main) main->RaiseAndFocus();
    }

    int UltraCanvasSingleInstance::RunAlreadyRunningAlert(UltraCanvasApplication& app,
                                                          const std::string& displayName) {
        AlertOptions opts;
        opts.severity = AlertSeverity::Info;
        opts.title    = displayName;
        opts.message  = displayName + " is already running.";
        opts.details  = "Only one " + displayName + " runs at a time. "
                        "Switch to the window that is already open.";
        opts.onResult = [&app](DialogResult) { app.Exit(); };
        UltraCanvasAlert::Show(opts);
        app.Run();
        return EXIT_SUCCESS;
    }

} // namespace UltraCanvas
