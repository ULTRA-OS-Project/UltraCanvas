// Apps/UltraWeb/host/UltraWebGuest.h
// One running WebAssembly app: its module instance (WasmHost) and the
// UltraCanvas elements it built through the element ABI
// (UltraWeb/guest/ultraweb.h). The guest only ever holds handles; this
// class owns the elements, checks every handle, pointer and length it is
// given, and turns the elements' callbacks into uc_event calls.
//
// Lives on the UI thread. A failure after start (a trap, the time limit)
// is reported through onFailure on a later UI-thread turn, never from
// inside the element callback that ran the guest, so the owner can tear
// the guest down safely.
// Version: 0.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasContainer.h"
#include "WasmHost/UltraCanvasWasmHost.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraWeb {

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
};

class UltraWebGuest {
public:
    // Instantiates `module` (.wasm, or WebAssembly text) and runs its
    // uc_main with `root` as UC_ROOT_HANDLE. Null and `error` set when the
    // module cannot load, has no uc_main, or fails in it.
    static std::unique_ptr<UltraWebGuest> Start(const std::vector<uint8_t>& module,
                                                std::shared_ptr<UltraCanvas::UltraCanvasContainer> root,
                                                GuestOptions options, std::string& error);
    // Removes everything the guest built from the root.
    ~UltraWebGuest();
    UltraWebGuest(const UltraWebGuest&) = delete;
    UltraWebGuest& operator=(const UltraWebGuest&) = delete;

    // Elements the guest currently holds, the root not counted.
    size_t ElementCount() const;
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
    };

    explicit UltraWebGuest(GuestOptions options);
    std::vector<UltraCanvas::WasmImport> MakeImports();

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

    Entry* Find(uint32_t handle);
    const Entry* Find(uint32_t handle) const;
    bool IsAncestor(uint32_t ancestor, uint32_t handle) const;
    void Detach(uint32_t handle);
    void WireCallbacks(uint32_t handle);
    void Deliver(uint32_t handle, uint32_t event, int32_t detail);
    void Fail(const std::string& message);

    GuestOptions options_;
    std::unique_ptr<UltraCanvas::UltraCanvasWasmInstance> instance_;
    std::vector<Entry> entries_;          // index = handle; 0 unused, 1 the root
    std::vector<uint32_t> freeHandles_;
    size_t liveCount_ = 0;
    bool inGuest_ = false;                // a call into the guest is running
    bool failed_ = false;
};

} // namespace UltraWeb
