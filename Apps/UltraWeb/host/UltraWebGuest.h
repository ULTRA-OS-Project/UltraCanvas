// Apps/UltraWeb/host/UltraWebGuest.h
// One running WebAssembly app: its module instance (WasmHost) and the
// UltraCanvas elements it built through the element ABI
// (UltraWeb/guest/ultraweb.h). The guest only ever holds handles; this
// class owns the elements, checks every handle, pointer and length it is
// given, and turns the elements' callbacks into uc_event calls. Since ABI
// v2 it also runs the app's timers, fetches, storage and clipboard writes,
// through services the owner hands in (GuestServices) and under the rules
// in ultraweb.h.
//
// Lives on the UI thread. A failure after start (a trap, the time limit)
// is reported through onFailure on a later UI-thread turn, never from
// inside the element callback that ran the guest, so the owner can tear
// the guest down safely.
// Version: 0.2.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraWebFetch.h"
#include "UltraWebStorage.h"

#include "UltraCanvasContainer.h"
#include "WasmHost/UltraCanvasWasmHost.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraWeb {

// What an app reaches beside its elements (element ABI v2). UltraWeb hands
// in the real services (UltraWebWindow.cpp); the tests hand in fakes. A
// service left empty answers UC_ERR_DENIED.
struct GuestServices {
    // Runs `fire` on the UI thread after `ms`, and every `ms` after that
    // when `repeat`, until stopTimer; returns an id for stopTimer, 0 when it
    // cannot. UltraWeb: the application's StartTimer.
    std::function<uint32_t(uint32_t ms, bool repeat, std::function<void()> fire)> startTimer;
    std::function<void(uint32_t id)> stopTimer;
    // Sends what uc_fetch asked for, once FetchRules allowed it.
    FetchService fetch;
    // The app's store; flushed a turn after a change, and when the guest goes.
    std::shared_ptr<UltraWebStorage> storage;
    // Puts text on the system clipboard; false when it could not.
    std::function<bool(const std::string&)> writeClipboard;
};

struct GuestOptions {
    UltraCanvas::WasmLimits limits;
    // Elements one app may create; a guest past it gets UC_NO_HANDLE.
    size_t maxElements = 20000;
    // Longest text property a guest may set, in bytes.
    uint32_t maxTextBytes = 1u << 20;
    // Where uc_log lines go (UltraWeb prints them to stderr).
    std::function<void(const std::string&)> onLog;
    // A failure after start, reported once and later (see above).
    std::function<void(const std::string&)> onFailure;
    // Runs a task on a later UI-thread turn. UltraWeb passes the
    // application's PostToUIThread; tests can run it immediately.
    std::function<void(std::function<void()>)> defer;
    // The address the app was loaded from: its origin for fetch, and what a
    // relative fetch URL is resolved against.
    std::string address;
    GuestServices services;
};

class UltraWebGuest {
public:
    // Instantiates `module` (.wasm, or WebAssembly text) and runs its
    // uc_main with `root` as UC_ROOT_HANDLE. Null and `error` set when the
    // module cannot load, has no uc_main, or fails in it.
    static std::unique_ptr<UltraWebGuest> Start(const std::vector<uint8_t>& module,
                                                std::shared_ptr<UltraCanvas::UltraCanvasContainer> root,
                                                GuestOptions options, std::string& error);
    // Removes everything the guest built from the root, stops its timers,
    // cancels its fetches and writes its storage.
    ~UltraWebGuest();
    UltraWebGuest(const UltraWebGuest&) = delete;
    UltraWebGuest& operator=(const UltraWebGuest&) = delete;

    // Elements the guest currently holds, the root not counted.
    size_t ElementCount() const;
    size_t TimerCount() const { return timers_.size(); }
    size_t FetchCount() const { return fetches_.size(); }
    bool HasFailed() const { return failed_; }

    // For tests: deliver an event as if the user had caused it.
    void DeliverEventForTest(uint32_t handle, uint32_t event, int32_t detail) { Deliver(handle, event, detail); }
    std::shared_ptr<UltraCanvas::UltraCanvasUIElement> ElementForTest(uint32_t handle) const;

private:
    struct Entry {
        std::shared_ptr<UltraCanvas::UltraCanvasUIElement> element;
        std::string kind;
        uint32_t parent = 0;
        std::vector<uint32_t> children;   // in order, mirrored by the container
        uint32_t listening = 0;
        bool live = false;
        float width = 0, height = 0;      // px the guest set (0 = automatic)
        bool row = false;                 // a container laid out as a row
    };

    struct Timer {
        uint32_t hostId = 0;
        bool repeat = false;
    };
    struct Fetch {
        FetchRequest request;
        int32_t status = 0;               // 0 while running; the HTTP status; or the UC_ERR_* it failed with
        std::vector<uint8_t> body;
        FetchHeaders headers;             // what the app may read, names in lower case
        std::function<void()> cancel;     // while running
    };

    explicit UltraWebGuest(GuestOptions options);
    std::vector<UltraCanvas::WasmImport> MakeImports();
    void AddServiceImports(std::vector<UltraCanvas::WasmImport>& imports);

    // The ABI, one function per import; the results are UC_* codes.
    uint32_t Create(const std::string& kind);
    int32_t Release(uint32_t handle);
    int32_t Insert(uint32_t parent, uint32_t child, uint32_t before);
    int32_t Remove(uint32_t child);
    int32_t SetText(uint32_t handle, uint32_t property, const std::string& value);
    int32_t SetNumber(uint32_t handle, uint32_t property, double value);
    int32_t GetText(uint32_t handle, uint32_t property, std::string& out);
    double GetNumber(uint32_t handle, uint32_t property);
    int32_t Listen(uint32_t handle, uint32_t mask);
    int32_t Bounds(uint32_t handle, float out[4]);

    int32_t StartTimer(uint32_t delayMs, bool repeat);
    int32_t StopTimer(int32_t id);
    void TimerFired(int32_t id);
    int32_t StartFetch(const std::string& url, uint32_t method, std::vector<uint8_t> body, std::string contentType);
    void FetchDone(int32_t id, FetchResponse response);
    // The finished fetch `id`, or null with `result` set to why not.
    const Fetch* FinishedFetch(int32_t id, int32_t& result) const;
    int32_t CloseFetch(int32_t id);
    void StorageChanged();
    int32_t WriteClipboard(const std::string& text);
    void Log(const std::string& line);

    Entry* Find(uint32_t handle);
    const Entry* Find(uint32_t handle) const;
    bool IsAncestor(uint32_t ancestor, uint32_t handle) const;
    void Detach(uint32_t handle);
    void UpdateCrossAlignment(uint32_t handle);
    void WireCallbacks(uint32_t handle);
    void Deliver(uint32_t handle, uint32_t event, int32_t detail);
    // An event of the app itself (timer, fetch): now, or - while a call into
    // the guest runs - as soon as it has returned.
    void DeliverAppEvent(uint32_t event, int32_t detail);
    void DrainAppEvents();
    void CallEvent(uint32_t handle, uint32_t event, int32_t detail);
    void Fail(const std::string& message);

    GuestOptions options_;
    std::unique_ptr<UltraCanvas::UltraCanvasWasmInstance> instance_;
    std::vector<Entry> entries_;          // index = handle; 0 unused, 1 the root
    std::vector<uint32_t> freeHandles_;
    size_t liveCount_ = 0;
    bool inGuest_ = false;                // a call into the guest is running
    bool inUserAction_ = false;           // ... and it handles a click, a toggle, an edit
    bool failed_ = false;

    std::map<int32_t, Timer> timers_;
    std::map<int32_t, Fetch> fetches_;
    int32_t nextTimerId_ = 1;
    int32_t nextFetchId_ = 1;
    std::deque<std::pair<uint32_t, int32_t>> appEvents_;   // event, detail
    // Timer and network callbacks hold this weakly: the guest may be gone
    // by the time they run.
    std::shared_ptr<int> alive_;
};

} // namespace UltraWeb
