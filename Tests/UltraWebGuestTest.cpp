// Tests/UltraWebGuestTest.cpp
// UltraWeb's element bridge (Apps/UltraWeb/host/UltraWebGuest.cpp) and its
// loader, against guests written inline as WebAssembly text and against the
// built-in about:demo app: the element tree a guest builds, events into it,
// insertion order, release of subtrees, the limits, and every misuse of the
// ABI a guest can make - which must come back as an error code, never as a
// crash of the browser. Elements are created without a window; nothing is
// laid out or drawn, so no display is needed.
// ABI v2: timers, fetch, storage and the clipboard against fake services
// (no network, no application loop), the fetch rules (UltraWebFetch.cpp)
// and the store (UltraWebStorage.cpp) on their own, the store's file in a
// temporary folder.
// Without a WebAssembly engine it checks that a start fails and says so.
// Version: 0.2.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "host/UltraWebDemoApp.h"
#include "host/UltraWebFetch.h"
#include "host/UltraWebGuest.h"
#include "host/UltraWebLoader.h"
#include "host/UltraWebStorage.h"

#include "ultraweb.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasPathUtf8.h"
#include "WasmHost/UltraCanvasWasmHost.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

using namespace UltraCanvas;
using namespace UltraWeb;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

namespace {

std::vector<uint8_t> Bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
std::string Text(const std::vector<uint8_t>& b) { return std::string(b.begin(), b.end()); }

// Timers that fire when the test says so.
struct FakeTimers {
    struct Timer {
        uint32_t ms = 0;
        bool repeat = false;
        std::function<void()> fire;
    };
    std::map<uint32_t, Timer> running;
    uint32_t next = 0;

    void Install(GuestServices& services) {
        services.startTimer = [this](uint32_t ms, bool repeat, std::function<void()> fire) {
            running[++next] = Timer{ms, repeat, std::move(fire)};
            return next;
        };
        services.stopTimer = [this](uint32_t id) { running.erase(id); };
    }
    // As the application loop would: a one-shot timer is done once fired.
    void Fire(uint32_t id) {
        auto at = running.find(id);
        if (at == running.end()) return;
        std::function<void()> fire = at->second.fire;
        if (!at->second.repeat) running.erase(at);
        fire();
    }
};

// A network that answers when the test says so - or at once, inside the
// fetch call, which a real one never does.
struct FakeNet {
    struct Sent {
        FetchRequest request;
        std::function<void(FetchResponse)> done;
        bool cancelled = false;
    };
    std::vector<Sent> sent;
    bool answerAtOnce = false;
    FetchResponse atOnce;

    void Install(GuestServices& services) {
        services.fetch = [this](const FetchRequest& request, std::function<void(FetchResponse)> done) {
            const size_t index = sent.size();
            sent.push_back(Sent{request, done, false});
            if (answerAtOnce) done(atOnce);
            return std::function<void()>([this, index]() { sent[index].cancelled = true; });
        };
    }
    void Answer(size_t index, FetchResponse response) {
        if (index < sent.size()) sent[index].done(std::move(response));
    }
};

FetchResponse Ok(const std::string& body, FetchHeaders headers = {}) {
    FetchResponse response;
    response.status = 200;
    response.body = Bytes(body);
    response.headers = std::move(headers);
    return response;
}

constexpr const char* kAppAddress = "https://app.example/dir/index.wasm";

struct Harness {
    std::shared_ptr<UltraCanvasContainer> root = CreateContainer("testRoot", 0, 0, 800, 600);
    // The fakes before the guest: it stops timers and cancels fetches in its
    // destructor, so they must outlive it.
    FakeTimers timers;
    FakeNet net;
    std::shared_ptr<UltraWebStorage> storage = std::make_shared<UltraWebStorage>("https://app.example");
    std::vector<std::string> clipboard;
    std::string failure;
    std::vector<std::string> logs;
    std::unique_ptr<UltraWebGuest> guest;
    std::string error;

    bool Start(const std::string& wat, GuestOptions options = GuestOptions{}, bool withServices = true) {
        options.onFailure = [this](const std::string& message) { failure = message; };
        options.onLog = [this](const std::string& line) { logs.push_back(line); };
        options.defer = [](std::function<void()> task) { task(); };
        if (options.address.empty()) options.address = kAppAddress;
        if (withServices) {
            timers.Install(options.services);
            net.Install(options.services);
            options.services.storage = storage;
            options.services.writeClipboard = [this](const std::string& text) {
                clipboard.push_back(text);
                return true;
            };
        }
        guest = UltraWebGuest::Start(Bytes(wat), root, std::move(options), error);
        if (!guest) std::printf("  start failed: %s\n", error.c_str());
        return guest != nullptr;
    }
    template <class T> std::shared_ptr<T> Get(uint32_t handle) const {
        return std::dynamic_pointer_cast<T>(guest->ElementForTest(handle));
    }
    // What the app logged, without UltraWeb's own lines about it.
    std::vector<std::string> AppLogs() const {
        std::vector<std::string> app;
        for (const std::string& line : logs) {
            if (line.rfind("UltraWeb: ", 0) != 0) app.push_back(line);
        }
        return app;
    }
    bool HostLogged(const std::string& part) const {
        for (const std::string& line : logs) {
            if (line.rfind("UltraWeb: ", 0) == 0 && line.find(part) != std::string::npos) return true;
        }
        return false;
    }
};

// Checks the app's log lines, printing them when they differ.
bool SameLines(const std::vector<std::string>& got, const std::vector<std::string>& want) {
    if (got == want) return true;
    std::printf("  log lines differ:\n");
    for (size_t i = 0; i < std::max(got.size(), want.size()); ++i) {
        std::printf("    %-28s %s\n", i < got.size() ? got[i].c_str() : "-", i < want.size() ? want[i].c_str() : "-");
    }
    return false;
}

// The imports every test guest uses, and a $check helper that traps when a
// call does not return what the ABI promises.
const char* const kPrelude = R"(
  (import "ultracanvas" "uc_create" (func $create (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_release" (func $release (param i32) (result i32)))
  (import "ultracanvas" "uc_insert" (func $insert (param i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_remove" (func $remove (param i32) (result i32)))
  (import "ultracanvas" "uc_set_text" (func $set_text (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_set_number" (func $set_number (param i32 i32 f64) (result i32)))
  (import "ultracanvas" "uc_get_text" (func $get_text (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_get_number" (func $get_number (param i32 i32) (result f64)))
  (import "ultracanvas" "uc_listen" (func $listen (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_bounds" (func $bounds (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_log" (func $log (param i32 i32)))
  (import "ultracanvas" "uc_timer_start" (func $timer_start (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_timer_stop" (func $timer_stop (param i32) (result i32)))
  (import "ultracanvas" "uc_fetch" (func $fetch (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_fetch_status" (func $fetch_status (param i32) (result i32)))
  (import "ultracanvas" "uc_fetch_body" (func $fetch_body (param i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_fetch_header" (func $fetch_header (param i32 i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_fetch_close" (func $fetch_close (param i32) (result i32)))
  (import "ultracanvas" "uc_storage_get" (func $storage_get (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_storage_set" (func $storage_set (param i32 i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_storage_remove" (func $storage_remove (param i32 i32) (result i32)))
  (import "ultracanvas" "uc_storage_key" (func $storage_key (param i32 i32 i32) (result i32)))
  (import "ultracanvas" "uc_storage_clear" (func $storage_clear (result i32)))
  (import "ultracanvas" "uc_clipboard_write" (func $clipboard_write (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "Container")
  (data (i32.const 16) "Label")
  (data (i32.const 32) "Button")
  (data (i32.const 48) "Checkbox")
  (data (i32.const 64) "Spaceship")
  (data (i32.const 80) "A")
  (data (i32.const 81) "B")
  (data (i32.const 82) "C")
  (func $check (param $got i32) (param $want i32)
    (if (i32.ne (local.get $got) (local.get $want)) (then unreachable)))
  ;; A signed decimal at $dst; returns its length.
  (func $num (param $v i32) (param $dst i32) (result i32)
    (local $u i32) (local $n i32) (local $i i32) (local $t i32)
    (local.set $u (local.get $v))
    (if (i32.lt_s (local.get $v) (i32.const 0)) (then (local.set $u (i32.sub (i32.const 0) (local.get $v)))))
    (loop $emit
      (i32.store8 (i32.add (local.get $dst) (local.get $n))
                  (i32.add (i32.const 48) (i32.rem_u (local.get $u) (i32.const 10))))
      (local.set $n (i32.add (local.get $n) (i32.const 1)))
      (local.set $u (i32.div_u (local.get $u) (i32.const 10)))
      (br_if $emit (local.get $u)))
    (if (i32.lt_s (local.get $v) (i32.const 0))
      (then (i32.store8 (i32.add (local.get $dst) (local.get $n)) (i32.const 45))
            (local.set $n (i32.add (local.get $n) (i32.const 1)))))
    (block $done
      (loop $swap
        (br_if $done (i32.ge_u (local.get $i) (i32.div_u (local.get $n) (i32.const 2))))
        (local.set $t (i32.load8_u (i32.add (local.get $dst) (local.get $i))))
        (i32.store8 (i32.add (local.get $dst) (local.get $i))
                    (i32.load8_u (i32.add (local.get $dst) (i32.sub (i32.sub (local.get $n) (i32.const 1)) (local.get $i)))))
        (i32.store8 (i32.add (local.get $dst) (i32.sub (i32.sub (local.get $n) (i32.const 1)) (local.get $i))) (local.get $t))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $swap)))
    (local.get $n))
  ;; Logs "<tag> <value>", the tag at ($ptr, $len).
  (func $say (param $ptr i32) (param $len i32) (param $v i32)
    (memory.copy (i32.const 60000) (local.get $ptr) (local.get $len))
    (i32.store8 (i32.add (i32.const 60000) (local.get $len)) (i32.const 32))
    (call $log (i32.const 60000)
      (i32.add (i32.add (local.get $len) (i32.const 1))
               (call $num (local.get $v) (i32.add (i32.const 60001) (local.get $len))))))
)";

std::string Guest(const std::string& body) { return std::string("(module ") + kPrelude + body + ")"; }

void TestDemoApp() {
    Harness h;
    CHECK(h.Start(DemoAppWat()));
    if (!h.guest) return;
    // Title, description, row, button, count, input, echo, checkbox, copy
    // button, uptime.
    CHECK(h.guest->ElementCount() == 10);
    CHECK(h.root->GetChildren().size() == 8);
    CHECK(!h.logs.empty() && h.logs[0] == "about:demo started");

    // Handles are handed out in creation order from 2 (1 is the root).
    auto button = h.Get<UltraCanvasButton>(5);
    auto counter = h.Get<UltraCanvasLabel>(6);
    auto input = h.Get<UltraCanvasTextInput>(7);
    auto echo = h.Get<UltraCanvasLabel>(8);
    auto check = h.Get<UltraCanvasCheckbox>(9);
    auto copy = h.Get<UltraCanvasButton>(10);
    auto uptime = h.Get<UltraCanvasLabel>(11);
    CHECK(button && counter && input && echo && check && copy && uptime);
    if (!(button && counter && input && echo && check && copy && uptime)) return;
    // The copy button sits between the input and its echo.
    const auto children = h.root->GetChildren();
    CHECK(children.size() == 8 && children[4] == copy && children[5] == echo);
    CHECK(button->GetText() == "Count");
    CHECK(counter->GetText() == "Clicked 0 times");
    CHECK(input->GetPlaceholder() == "Type something");
    // The guest checked the box itself in uc_main; that raised no event
    // into it (it would have hidden the counter).
    CHECK(check->IsChecked());
    CHECK(counter->IsVisible());

    // A click, through the button's own callback; the count goes to storage.
    for (int i = 0; i < 12; ++i) button->onClick();
    CHECK(counter->GetText() == "Clicked 12 times");
    std::string stored;
    CHECK(h.storage->Get("clicks", stored) && stored == "12");

    // Typing: the input's text, then the change callback a key press runs.
    input->SetText("hello, wasm");
    input->onTextChanged("hello, wasm");
    CHECK(echo->GetText() == "You typed: hello, wasm");
    // Copy: a click, so the clipboard takes it.
    copy->onClick();
    CHECK(h.clipboard.size() == 1 && h.clipboard[0] == "hello, wasm");
    CHECK(echo->GetText() == "Copied to the clipboard");

    // The uptime timer: one, every second.
    CHECK(h.timers.running.size() == 1);
    if (h.timers.running.size() == 1) {
        const auto [id, timer] = *h.timers.running.begin();
        CHECK(timer.ms == 1000 && timer.repeat);
        CHECK(uptime->GetText() == "Running for 0 s");
        for (int i = 0; i < 3; ++i) h.timers.Fire(id);
        CHECK(uptime->GetText() == "Running for 3 s");
    }

    // Unchecking hides the counter.
    check->SetChecked(false);
    CHECK(!counter->IsVisible());
    check->SetChecked(true);
    CHECK(counter->IsVisible());
    CHECK(h.failure.empty() && !h.guest->HasFailed());

    // Destroying the guest takes its elements out of the root and stops
    // its timer.
    h.guest.reset();
    CHECK(h.root->GetChildren().empty());
    CHECK(h.timers.running.empty());

    // A second run on the same store starts from the stored count.
    Harness again;
    again.storage = h.storage;
    CHECK(again.Start(DemoAppWat()));
    if (!again.guest) return;
    auto counterAgain = again.Get<UltraCanvasLabel>(6);
    CHECK(counterAgain && counterAgain->GetText() == "Clicked 12 times");

    // Without the services the demo still runs: nothing stored, no timer,
    // and the clipboard is not there.
    Harness bare;
    CHECK(bare.Start(DemoAppWat(), GuestOptions{}, false));
    if (!bare.guest) return;
    auto bareCopy = bare.Get<UltraCanvasButton>(10);
    auto bareEcho = bare.Get<UltraCanvasLabel>(8);
    if (bareCopy && bareEcho) {
        bareCopy->onClick();
        CHECK(bareEcho->GetText() == "The clipboard did not take it");
    }
    CHECK(bare.guest->TimerCount() == 0 && !bare.guest->HasFailed());
}

void TestTreeAndMisuse() {
    Harness h;
    const bool started = h.Start(Guest(R"(
  (func (export "uc_main")
    (local $box i32) (local $a i32) (local $b i32) (local $c i32) (local $label i32) (local $inner i32)
    (local.set $box (call $create (i32.const 0) (i32.const 9)))
    (call $check (call $insert (i32.const 1) (local.get $box) (i32.const 0)) (i32.const 0))
    (local.set $a (call $create (i32.const 16) (i32.const 5)))
    (local.set $c (call $create (i32.const 16) (i32.const 5)))
    (local.set $b (call $create (i32.const 16) (i32.const 5)))
    (drop (call $set_text (local.get $a) (i32.const 1) (i32.const 80) (i32.const 1)))
    (drop (call $set_text (local.get $b) (i32.const 1) (i32.const 81) (i32.const 1)))
    (drop (call $set_text (local.get $c) (i32.const 1) (i32.const 82) (i32.const 1)))
    ;; A, C, then B before C: A B C.
    (call $check (call $insert (local.get $box) (local.get $a) (i32.const 0)) (i32.const 0))
    (call $check (call $insert (local.get $box) (local.get $c) (i32.const 0)) (i32.const 0))
    (call $check (call $insert (local.get $box) (local.get $b) (local.get $c)) (i32.const 0))
    ;; Unknown kinds and kinds longer than the limit give no handle.
    (call $check (call $create (i32.const 64) (i32.const 9)) (i32.const 0))
    (call $check (call $create (i32.const 0) (i32.const 1000)) (i32.const 0))
    ;; Bad handles.
    (call $check (call $set_text (i32.const 999) (i32.const 1) (i32.const 80) (i32.const 1)) (i32.const -1))
    (call $check (call $insert (local.get $box) (i32.const 0) (i32.const 0)) (i32.const -1))
    ;; A property the kind does not have.
    (call $check (call $set_text (local.get $box) (i32.const 1) (i32.const 80) (i32.const 1)) (i32.const -3))
    (call $check (call $set_number (local.get $a) (i32.const 9) (f64.const 1)) (i32.const -3))
    ;; Out-of-range numbers.
    (call $check (call $set_number (local.get $a) (i32.const 6) (f64.const -5)) (i32.const -6))
    (call $check (call $set_number (local.get $a) (i32.const 6) (f64.const nan)) (i32.const -6))
    ;; Pointers outside memory, reading and writing.
    (call $check (call $set_text (local.get $a) (i32.const 1) (i32.const 65530) (i32.const 100)) (i32.const -4))
    (call $check (call $get_text (local.get $a) (i32.const 1) (i32.const 65536) (i32.const 10)) (i32.const -4))
    (call $check (call $bounds (local.get $a) (i32.const 65530)) (i32.const -4))
    ;; get_text returns the full length and copies what fits.
    (call $check (call $get_text (local.get $a) (i32.const 1) (i32.const 200) (i32.const 0)) (i32.const 1))
    ;; Structure: no inserting into a label, no cycles, the root stays.
    (local.set $label (call $create (i32.const 16) (i32.const 5)))
    (call $check (call $insert (local.get $label) (local.get $a) (i32.const 0)) (i32.const -5))
    (call $check (call $insert (local.get $box) (local.get $box) (i32.const 0)) (i32.const -5))
    (local.set $inner (call $create (i32.const 0) (i32.const 9)))
    (call $check (call $insert (local.get $box) (local.get $inner) (i32.const 0)) (i32.const 0))
    (call $check (call $insert (local.get $inner) (local.get $box) (i32.const 0)) (i32.const -5))
    (call $check (call $insert (local.get $box) (i32.const 1) (i32.const 0)) (i32.const -5))
    (call $check (call $release (i32.const 1)) (i32.const -5))
    (call $check (call $remove (i32.const 1)) (i32.const -5))
    ;; "before" must be a child of the parent.
    (call $check (call $insert (local.get $box) (local.get $label) (local.get $label)) (i32.const -5))
    ;; Listening needs uc_event, which this guest does not export.
    (call $check (call $listen (local.get $a) (i32.const 1)) (i32.const -5))
    ;; A removed element keeps its handle; a released one does not.
    (call $check (call $remove (local.get $label)) (i32.const 0))
    (call $check (call $release (local.get $label)) (i32.const 0))
    (call $check (call $set_text (local.get $label) (i32.const 1) (i32.const 80) (i32.const 1)) (i32.const -1)))
)"));
    CHECK(started);
    if (!started) return;
    // box (2) holds A (3), B (5), C (4), inner (7), in that order. A create
    // that fails hands its handle back, so the label got 6 and inner 7.
    auto box = h.Get<UltraCanvasContainer>(2);
    CHECK(box != nullptr);
    if (!box) return;
    const auto children = box->GetChildren();
    CHECK(children.size() == 4);
    if (children.size() == 4) {
        auto first = std::dynamic_pointer_cast<UltraCanvasLabel>(children[0]);
        auto second = std::dynamic_pointer_cast<UltraCanvasLabel>(children[1]);
        auto third = std::dynamic_pointer_cast<UltraCanvasLabel>(children[2]);
        CHECK(first && first->GetText() == "A");
        CHECK(second && second->GetText() == "B");
        CHECK(third && third->GetText() == "C");
    }
    // box, A, B, C, inner; the label was released.
    CHECK(h.guest->ElementCount() == 5);
}

void TestReleaseSubtree() {
    Harness h;
    const bool started = h.Start(Guest(R"(
  (global $box (mut i32) (i32.const 0))
  (func (export "uc_main")
    (local $inner i32)
    (global.set $box (call $create (i32.const 0) (i32.const 9)))
    (drop (call $insert (i32.const 1) (global.get $box) (i32.const 0)))
    (local.set $inner (call $create (i32.const 0) (i32.const 9)))
    (drop (call $insert (global.get $box) (local.get $inner) (i32.const 0)))
    (drop (call $insert (local.get $inner) (call $create (i32.const 16) (i32.const 5)) (i32.const 0)))
    (drop (call $insert (local.get $inner) (call $create (i32.const 32) (i32.const 6)) (i32.const 0)))
    (call $check (call $release (global.get $box)) (i32.const 0))
    ;; Every handle in the subtree is gone.
    (call $check (call $remove (local.get $inner)) (i32.const -1)))
)"));
    CHECK(started);
    if (!started) return;
    CHECK(h.guest->ElementCount() == 0);
    CHECK(h.root->GetChildren().empty());
}

void TestLimits() {
    GuestOptions options;
    options.maxElements = 3;
    Harness h;
    const bool started = h.Start(Guest(R"(
  (func (export "uc_main")
    (call $check (i32.ne (call $create (i32.const 16) (i32.const 5)) (i32.const 0)) (i32.const 1))
    (call $check (i32.ne (call $create (i32.const 16) (i32.const 5)) (i32.const 0)) (i32.const 1))
    (call $check (i32.ne (call $create (i32.const 16) (i32.const 5)) (i32.const 0)) (i32.const 1))
    (call $check (call $create (i32.const 16) (i32.const 5)) (i32.const 0)))
)"), options);
    CHECK(started);
    if (started) CHECK(h.guest->ElementCount() == 3);
}

void TestFailuresAfterStart() {
    // A trap in an event handler is reported once, through onFailure.
    Harness trap;
    const bool started = trap.Start(Guest(R"(
  (func (export "uc_main")
    (local $b i32)
    (local.set $b (call $create (i32.const 32) (i32.const 6)))
    (drop (call $insert (i32.const 1) (local.get $b) (i32.const 0)))
    (drop (call $listen (local.get $b) (i32.const 1))))
  (func (export "uc_event") (param i32 i32 i32) unreachable)
)"));
    CHECK(started);
    if (started) {
        auto button = trap.Get<UltraCanvasButton>(2);
        CHECK(button != nullptr);
        if (button) button->onClick();
        CHECK(trap.guest->HasFailed());
        CHECK(!trap.failure.empty());
        // Later events are ignored rather than delivered to a broken guest.
        trap.failure.clear();
        if (button) button->onClick();
        CHECK(trap.failure.empty());
    }

    // A handler that never returns is stopped by the time limit.
    GuestOptions options;
    options.limits.callTimeoutMs = 100;
    Harness spin;
    const bool spinStarted = spin.Start(Guest(R"(
  (func (export "uc_main")
    (local $b i32)
    (local.set $b (call $create (i32.const 32) (i32.const 6)))
    (drop (call $insert (i32.const 1) (local.get $b) (i32.const 0)))
    (drop (call $listen (local.get $b) (i32.const 1))))
  (func (export "uc_event") (param i32 i32 i32) (loop $forever br $forever))
)"), options);
    CHECK(spinStarted);
    if (spinStarted) {
        spin.guest->DeliverEventForTest(2, 1, 0);
        CHECK(spin.guest->HasFailed());
        CHECK(spin.failure.find("longer than") != std::string::npos);
    }

    // Failing in uc_main is a failed start, and leaves nothing behind.
    Harness early;
    CHECK(!early.Start(Guest(R"(
  (func (export "uc_main")
    (drop (call $insert (i32.const 1) (call $create (i32.const 16) (i32.const 5)) (i32.const 0)))
    unreachable)
)")));
    CHECK(early.root->GetChildren().empty());

    // Not an app: no uc_main.
    Harness none;
    CHECK(!none.Start("(module (func (export \"main\")))"));
    CHECK(none.error.find("uc_main") != std::string::npos);

    // An app built for a newer ABI is refused.
    Harness future;
    CHECK(!future.Start("(module (func (export \"uc_abi_version\") (result i32) i32.const 99) (func (export \"uc_main\")))"));
    CHECK(future.error.find("version 99") != std::string::npos);
}

void TestLoader() {
    CHECK(UltraWebLoader::Normalise("") == "about:demo");
    CHECK(UltraWebLoader::Normalise("  about:demo ") == "about:demo");
    CHECK(UltraWebLoader::Normalise("example.org/app.wasm") == "https://example.org/app.wasm");
    CHECK(UltraWebLoader::Normalise("http://example.org/a.wasm") == "http://example.org/a.wasm");
    CHECK(UltraWebLoader::LooksLikeModule({0x00, 'a', 's', 'm', 1, 0, 0, 0}));
    CHECK(UltraWebLoader::LooksLikeModule(Bytes(";; a comment\n  (module)")));
    CHECK(!UltraWebLoader::LooksLikeModule(Bytes("<!doctype html><html>")));
    CHECK(!UltraWebLoader::LooksLikeModule({}));

    LoadedApp demo = UltraWebLoader::LoadOffline("about:demo");
    CHECK(demo.ok && demo.address == "about:demo" && !demo.module.empty());
    CHECK(!UltraWebLoader::LoadOffline("about:nothing").ok);
    CHECK(!UltraWebLoader::LoadOffline("/no/such/file.wasm").ok);
    CHECK(!UltraWebLoader::LoadOffline("https://example.org/app.wasm").ok);
}

CSSLayout::AlignSelf AlignOf(const std::shared_ptr<UltraCanvasUIElement>& element) {
    const auto* item = element ? std::get_if<CSSLayout::FlexItem>(&element->layoutItem.data) : nullptr;
    return item ? item->alignSelf : CSSLayout::AlignSelf::Auto;
}

// A container stretches its children across, except one the guest sized
// on that axis - CSS stretches only an automatic cross size.
void TestSizedChildrenNotStretched() {
    Harness h;
    const bool started = h.Start(Guest(R"(
  (global $row (mut i32) (i32.const 0))
  (func (export "uc_main")
    (local $b i32)
    (drop (call $insert (i32.const 1) (call $create (i32.const 16) (i32.const 5)) (i32.const 0)))   ;; 2 label
    (local.set $b (call $create (i32.const 32) (i32.const 6)))                                      ;; 3 button
    (drop (call $set_number (local.get $b) (i32.const 6) (f64.const 200)))
    (drop (call $insert (i32.const 1) (local.get $b) (i32.const 0)))
    (global.set $row (call $create (i32.const 0) (i32.const 9)))                                    ;; 4 row
    (drop (call $set_number (global.get $row) (i32.const 9) (f64.const 1)))
    (drop (call $insert (i32.const 1) (global.get $row) (i32.const 0)))
    (local.set $b (call $create (i32.const 32) (i32.const 6)))                                      ;; 5 tall
    (drop (call $insert (global.get $row) (local.get $b) (i32.const 0)))
    (drop (call $set_number (local.get $b) (i32.const 7) (f64.const 40)))
    (local.set $b (call $create (i32.const 32) (i32.const 6)))                                      ;; 6 wide
    (drop (call $set_number (local.get $b) (i32.const 6) (f64.const 90)))
    (drop (call $insert (global.get $row) (local.get $b) (i32.const 0)))
    (local.set $b (call $create (i32.const 32) (i32.const 6)))                                      ;; 7 wide, unsized again
    (drop (call $set_number (local.get $b) (i32.const 6) (f64.const 90)))
    (drop (call $insert (i32.const 1) (local.get $b) (i32.const 0)))
    (drop (call $set_number (local.get $b) (i32.const 6) (f64.const 0)))
    (drop (call $listen (i32.const 3) (i32.const 1))))
  (func (export "uc_event") (param i32 i32 i32)
    ;; The row turns into a column.
    (drop (call $set_number (global.get $row) (i32.const 9) (f64.const 0))))
)"));
    CHECK(started);
    if (!started) return;
    using CSSLayout::AlignSelf;
    CHECK(AlignOf(h.guest->ElementForTest(2)) == AlignSelf::Auto);
    CHECK(AlignOf(h.guest->ElementForTest(3)) == AlignSelf::Start);    // width in a column
    CHECK(AlignOf(h.guest->ElementForTest(5)) == AlignSelf::Start);    // height in a row
    CHECK(AlignOf(h.guest->ElementForTest(6)) == AlignSelf::Auto);     // width in a row: the main axis
    CHECK(AlignOf(h.guest->ElementForTest(7)) == AlignSelf::Auto);
    // The row turns into a column: now the width counts, not the height.
    auto button = h.Get<UltraCanvasButton>(3);
    if (button) button->onClick();
    CHECK(AlignOf(h.guest->ElementForTest(5)) == AlignSelf::Auto);
    CHECK(AlignOf(h.guest->ElementForTest(6)) == AlignSelf::Start);
    // A text input is 240 x 28 unless sized, so it keeps its width too.
    Harness input;
    CHECK(input.Start(Guest(R"(
  (data (i32.const 1024) "TextInput")
  (func (export "uc_main")
    (drop (call $insert (i32.const 1) (call $create (i32.const 1024) (i32.const 9)) (i32.const 0))))
)")));
    if (input.guest) CHECK(AlignOf(input.guest->ElementForTest(2)) == AlignSelf::Start);
}

// ===== ABI v2: TIMERS =====

void TestTimers() {
    Harness h;
    const bool started = h.Start(Guest(R"(
  (data (i32.const 1024) "start")
  (data (i32.const 1032) "once")
  (data (i32.const 1040) "every")
  (data (i32.const 1048) "stop")
  (global $once (mut i32) (i32.const 0))
  (global $every (mut i32) (i32.const 0))
  (global $ticks (mut i32) (i32.const 0))
  (func (export "uc_main")
    (global.set $once (call $timer_start (i32.const 0) (i32.const 0)))
    (global.set $every (call $timer_start (i32.const 50) (i32.const 1)))
    (call $say (i32.const 1024) (i32.const 5) (global.get $once))
    (call $say (i32.const 1024) (i32.const 5) (global.get $every))
    ;; Past 2^31 - 1 ms, and an id that is not a timer.
    (call $check (call $timer_start (i32.const -1) (i32.const 0)) (i32.const -6))
    (call $check (call $timer_stop (i32.const 999)) (i32.const -7)))
  (func (export "uc_event") (param $h i32) (param $e i32) (param $id i32)
    (call $check (local.get $h) (i32.const 0))
    (call $check (local.get $e) (i32.const 16))
    (if (i32.eq (local.get $id) (global.get $once))
      (then
        (call $say (i32.const 1032) (i32.const 4) (local.get $id))
        ;; A one-shot timer is gone once it has fired.
        (call $check (call $timer_stop (local.get $id)) (i32.const -7))))
    (if (i32.eq (local.get $id) (global.get $every))
      (then
        (global.set $ticks (i32.add (global.get $ticks) (i32.const 1)))
        (call $say (i32.const 1040) (i32.const 5) (global.get $ticks))
        (if (i32.eq (global.get $ticks) (i32.const 3))
          (then (call $say (i32.const 1048) (i32.const 4) (call $timer_stop (local.get $id))))))))
)"));
    CHECK(started);
    if (!started) return;
    CHECK(h.guest->TimerCount() == 2);
    CHECK(h.timers.running.size() == 2);
    if (h.timers.running.size() != 2) return;
    // Host timers 1 and 2: the 0 ms one raised to the 4 ms minimum.
    CHECK(h.timers.running[1].ms == 4 && !h.timers.running[1].repeat);
    CHECK(h.timers.running[2].ms == 50 && h.timers.running[2].repeat);
    h.timers.Fire(1);
    h.timers.Fire(1);   // gone: nothing
    for (int i = 0; i < 5; ++i) h.timers.Fire(2);   // the guest stops it on the third
    CHECK(SameLines(h.AppLogs(), {"start 1", "start 2", "once 1", "every 1", "every 2", "every 3", "stop 0"}));
    CHECK(h.timers.running.empty());
    CHECK(h.guest->TimerCount() == 0);
    CHECK(!h.guest->HasFailed());

    // Destroying the guest stops what still runs, and a timer that fires
    // after that (already queued by the loop) reaches nothing.
    Harness late;
    CHECK(late.Start(Guest(R"(
  (func (export "uc_main") (drop (call $timer_start (i32.const 10) (i32.const 1))))
  (func (export "uc_event") (param i32 i32 i32) unreachable)
)")));
    if (late.guest && late.timers.running.size() == 1) {
        std::function<void()> queued = late.timers.running.begin()->second.fire;
        late.guest.reset();
        CHECK(late.timers.running.empty());
        queued();
        CHECK(late.failure.empty());
    }

    // No uc_event: nowhere to deliver, so no timer. 256 at most.
    Harness none;
    CHECK(none.Start(Guest(R"(
  (func (export "uc_main") (call $check (call $timer_start (i32.const 10) (i32.const 0)) (i32.const -5)))
)")));
    Harness many;
    CHECK(many.Start(Guest(R"(
  (func (export "uc_main")
    (local $i i32)
    (loop $more
      (call $check (i32.gt_s (call $timer_start (i32.const 1000) (i32.const 1)) (i32.const 0)) (i32.const 1))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $more (i32.lt_u (local.get $i) (i32.const 256))))
    (call $check (call $timer_start (i32.const 1000) (i32.const 1)) (i32.const -6)))
  (func (export "uc_event") (param i32 i32 i32))
)")));
    // Without a timer service: denied.
    Harness bare;
    CHECK(bare.Start(Guest(R"(
  (func (export "uc_main") (call $check (call $timer_start (i32.const 10) (i32.const 0)) (i32.const -8)))
  (func (export "uc_event") (param i32 i32 i32))
)"), GuestOptions{}, false));
}

// ===== ABI v2: FETCH =====

// A guest that starts fetches in uc_main, logging what each call returned,
// and on every finished fetch logs its status, body, headers and closes it.
const char* const kFetchGuest = R"(
  (data (i32.const 1024) "data.json")
  (data (i32.const 1040) "https://other.example/api")
  (data (i32.const 1072) "http://app.example/x")
  (data (i32.const 1104) "file:///etc/passwd")
  (data (i32.const 1136) "application/json")
  (data (i32.const 1160) "{}")
  (data (i32.const 1168) "Content-Type")
  (data (i32.const 1184) "x-secret")
  (data (i32.const 1200) "set-cookie")
  (data (i32.const 1216) "x-exposed")
  (data (i32.const 1232) "start")
  (data (i32.const 1240) "status")
  (data (i32.const 1248) "body")
  (data (i32.const 1256) "type")
  (data (i32.const 1264) "secret")
  (data (i32.const 1272) "cookie")
  (data (i32.const 1280) "exposed")
  (data (i32.const 1288) "close")
  (data (i32.const 1296) "again")
  (func (export "uc_main")
    ;; same origin, relative: id 1
    (call $say (i32.const 1232) (i32.const 5) (call $fetch (i32.const 1024) (i32.const 9) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0)))
    ;; plain http from an https app; a file; a JSON POST to another origin
    (call $say (i32.const 1232) (i32.const 5) (call $fetch (i32.const 1072) (i32.const 20) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0)))
    (call $say (i32.const 1232) (i32.const 5) (call $fetch (i32.const 1104) (i32.const 18) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0)))
    (call $say (i32.const 1232) (i32.const 5) (call $fetch (i32.const 1040) (i32.const 25) (i32.const 1) (i32.const 1160) (i32.const 2) (i32.const 1136) (i32.const 16)))
    ;; another origin, GET: id 2; a GET with a body; a POST with no type (text/plain, simple): id 3
    (call $say (i32.const 1232) (i32.const 5) (call $fetch (i32.const 1040) (i32.const 25) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0)))
    (call $say (i32.const 1232) (i32.const 5) (call $fetch (i32.const 1024) (i32.const 9) (i32.const 0) (i32.const 1160) (i32.const 2) (i32.const 0) (i32.const 0)))
    (call $say (i32.const 1232) (i32.const 5) (call $fetch (i32.const 1040) (i32.const 25) (i32.const 1) (i32.const 1160) (i32.const 2) (i32.const 0) (i32.const 0)))
    ;; an unknown method, a URL past guest memory
    (call $say (i32.const 1232) (i32.const 5) (call $fetch (i32.const 1024) (i32.const 9) (i32.const 77) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0)))
    (call $say (i32.const 1232) (i32.const 5) (call $fetch (i32.const 65530) (i32.const 100) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0)))
    ;; still running; not a fetch
    (call $say (i32.const 1240) (i32.const 6) (call $fetch_status (i32.const 1)))
    (call $say (i32.const 1248) (i32.const 4) (call $fetch_body (i32.const 1) (i32.const 0) (i32.const 0)))
    (call $say (i32.const 1240) (i32.const 6) (call $fetch_status (i32.const 99))))
  (func (export "uc_event") (param $h i32) (param $e i32) (param $id i32)
    (local $length i32)
    (call $check (local.get $h) (i32.const 0))
    (call $check (local.get $e) (i32.const 32))
    (call $say (i32.const 1240) (i32.const 6) (call $fetch_status (local.get $id)))
    (local.set $length (call $fetch_body (local.get $id) (i32.const 4096) (i32.const 1024)))
    (call $say (i32.const 1248) (i32.const 4) (local.get $length))
    (if (i32.gt_s (local.get $length) (i32.const 0)) (then (call $log (i32.const 4096) (local.get $length))))
    (call $say (i32.const 1256) (i32.const 4) (call $fetch_header (local.get $id) (i32.const 1168) (i32.const 12) (i32.const 4096) (i32.const 0)))
    (call $say (i32.const 1264) (i32.const 6) (call $fetch_header (local.get $id) (i32.const 1184) (i32.const 8) (i32.const 4096) (i32.const 0)))
    (call $say (i32.const 1280) (i32.const 7) (call $fetch_header (local.get $id) (i32.const 1216) (i32.const 9) (i32.const 4096) (i32.const 0)))
    (call $say (i32.const 1272) (i32.const 6) (call $fetch_header (local.get $id) (i32.const 1200) (i32.const 10) (i32.const 4096) (i32.const 0)))
    (call $say (i32.const 1288) (i32.const 5) (call $fetch_close (local.get $id)))
    (call $say (i32.const 1296) (i32.const 5) (call $fetch_close (local.get $id))))
)";

void TestFetchThroughGuest() {
    Harness h;
    const bool started = h.Start(Guest(kFetchGuest));
    CHECK(started);
    if (!started) return;
    CHECK(SameLines(h.AppLogs(), {"start 1", "start -8", "start -8", "start -8", "start 2", "start -5", "start 3",
                                  "start -5", "start -4", "status 0", "body -5", "status -7"}));
    // Each refusal said why, on UltraWeb's console.
    CHECK(h.HostLogged("cannot fetch over plain http"));
    CHECK(h.HostLogged("file: URLs cannot be fetched"));
    CHECK(h.HostLogged("needs a CORS preflight"));
    CHECK(h.guest->FetchCount() == 3);
    CHECK(h.net.sent.size() == 3);
    if (h.net.sent.size() != 3) return;

    const FetchRequest& same = h.net.sent[0].request;
    CHECK(same.url == "https://app.example/dir/data.json");
    CHECK(same.method == "GET" && same.sameOrigin && !same.sendOrigin && same.body.empty());
    const FetchRequest& cross = h.net.sent[1].request;
    CHECK(cross.url == "https://other.example/api");
    CHECK(!cross.sameOrigin && cross.sendOrigin && cross.origin == "https://app.example");
    const FetchRequest& post = h.net.sent[2].request;
    CHECK(post.method == "POST" && Text(post.body) == "{}" && post.contentType == "text/plain;charset=UTF-8");

    h.logs.clear();
    // Same origin: every header but Set-Cookie, in any case.
    h.net.Answer(0, Ok("hello", {{"Content-Type", "text/plain"}, {"X-Secret", "s3cret"}, {"Set-Cookie", "a=b"}}));
    CHECK(SameLines(h.AppLogs(), {"status 200", "body 5", "hello", "type 10", "secret 6", "exposed -7", "cookie -7",
                                  "close 0", "again -7"}));
    // Another origin that allows everyone: the safelisted headers and the
    // ones it exposes.
    h.logs.clear();
    h.net.Answer(1, Ok("{}", {{"Access-Control-Allow-Origin", "*"}, {"Access-Control-Expose-Headers", "X-Exposed"},
                              {"X-Exposed", "yes"}, {"X-Secret", "s"}, {"Content-Type", "application/json"},
                              {"Set-Cookie", "a=b"}}));
    CHECK(SameLines(h.AppLogs(), {"status 200", "body 2", "{}", "type 16", "secret -7", "exposed 3", "cookie -7",
                                  "close 0", "again -7"}));
    // Another origin that says nothing: the app sees a refusal, not the answer.
    h.logs.clear();
    h.net.Answer(2, Ok("private", {{"Content-Type", "text/plain"}}));
    CHECK(SameLines(h.AppLogs(), {"status -8", "body -8", "type -8", "secret -8", "exposed -8", "cookie -8",
                                  "close 0", "again -7"}));
    CHECK(h.HostLogged("does not allow origin https://app.example"));
    CHECK(h.guest->FetchCount() == 0);
    CHECK(!h.guest->HasFailed());
}

// One relative fetch per call of $go, logging its status when it finishes.
const char* const kFetchEach = R"(
  (data (i32.const 1024) "data.json")
  (data (i32.const 1040) "status")
  (data (i32.const 1048) "start")
  (func $go (result i32)
    (call $fetch (i32.const 1024) (i32.const 9) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0)))
  (func (export "uc_event") (param $h i32) (param $e i32) (param $id i32)
    (call $say (i32.const 1040) (i32.const 6) (call $fetch_status (local.get $id))))
)";

void TestFetchOutcomes() {
    // Failures, and redirects to where the request could not have gone.
    Harness h;
    CHECK(h.Start(Guest(std::string(kFetchEach) + R"(
  (func (export "uc_main")
    (local $i i32)
    (loop $more
      (drop (call $go))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $more (i32.lt_u (local.get $i) (i32.const 6))))
    ;; Closing a running fetch cancels it.
    (call $check (call $fetch_close (i32.const 6)) (i32.const 0)))
)")));
    if (!h.guest || h.net.sent.size() != 6) { CHECK(false); return; }
    FetchResponse noAnswer;
    noAnswer.error = "Timeout was reached";
    h.net.Answer(0, noAnswer);
    FetchResponse cut = Ok("partial");
    cut.error = "Connection reset";
    h.net.Answer(1, cut);
    FetchResponse large = Ok("");
    large.tooLarge = true;
    large.error = "response exceeded maxReceiveSize";
    h.net.Answer(2, large);
    FetchResponse away = Ok("x");
    away.finalUrl = "https://elsewhere.example/x";
    h.net.Answer(3, away);
    FetchResponse down = Ok("x");
    down.finalUrl = "http://app.example/x";
    h.net.Answer(4, down);
    CHECK(h.net.sent[5].cancelled);
    h.net.Answer(5, Ok("too late"));   // closed: nothing
    CHECK(SameLines(h.AppLogs(), {"status -9", "status -9", "status -6", "status -8", "status -8"}));
    CHECK(h.guest->FetchCount() == 5);

    // An answer inside uc_fetch itself (a real network never does that)
    // waits for the call to return: no re-entry into the guest.
    Harness quick;
    quick.net.answerAtOnce = true;
    quick.net.atOnce = Ok("x");
    CHECK(quick.Start(Guest(std::string(kFetchEach) + R"(
  (func (export "uc_main") (call $say (i32.const 1048) (i32.const 5) (call $go)))
)")));
    CHECK(SameLines(quick.AppLogs(), {"start 1", "status 200"}));

    // 16 open at most, also once they have finished; closing makes room.
    Harness many;
    CHECK(many.Start(Guest(std::string(kFetchEach) + R"(
  (func (export "uc_main")
    (local $i i32)
    (loop $more
      (call $check (i32.gt_s (call $go) (i32.const 0)) (i32.const 1))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $more (i32.lt_u (local.get $i) (i32.const 16))))
    (call $check (call $go) (i32.const -6))
    (call $check (call $fetch_close (i32.const 3)) (i32.const 0))
    (call $check (call $go) (i32.const 17)))
)")));
    CHECK(many.HostLogged("16 fetches are open already"));

    // Destroying the guest cancels what still runs; a late answer is harmless.
    Harness gone;
    CHECK(gone.Start(Guest(std::string(kFetchEach) + R"(
  (func (export "uc_main") (drop (call $go)))
)")));
    if (gone.guest && gone.net.sent.size() == 1) {
        gone.guest.reset();
        CHECK(gone.net.sent[0].cancelled);
        gone.net.Answer(0, Ok("late"));
        CHECK(gone.failure.empty());
    }

    // No uc_event: nowhere to say it finished. No service: denied.
    Harness deaf;
    CHECK(deaf.Start(Guest(R"(
  (data (i32.const 1024) "data.json")
  (func (export "uc_main")
    (call $check (call $fetch (i32.const 1024) (i32.const 9) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0)) (i32.const -5)))
)")));
    Harness bare;
    CHECK(bare.Start(Guest(std::string(kFetchEach) + R"(
  (func (export "uc_main") (call $check (call $go) (i32.const -8)))
)"), GuestOptions{}, false));
}

void TestFetchRules() {
    using namespace FetchRules;
    CHECK(OriginOf("https://App.Example:443/a/b?c#d") == "https://app.example");
    CHECK(OriginOf("http://app.example:8080/") == "http://app.example:8080");
    CHECK(OriginOf("http://app.example:80/") == "http://app.example");
    CHECK(OriginOf("https://[::1]:8443/") == "https://[::1]:8443");
    CHECK(OriginOf("about:demo").empty());
    CHECK(OriginOf("file:///home/me/app.wasm").empty());
    CHECK(OriginOf("/home/me/app.wasm").empty());
    CHECK(OriginOf("ftp://files.example/").empty());

    FetchRequest r;
    std::string why;
    // Relative URLs, resolved against the app; the fragment dropped.
    CHECK(Prepare(kAppAddress, "../api/items?page=2#top", UC_METHOD_GET, {}, "", r, why) == UC_OK);
    CHECK(r.url == "https://app.example/api/items?page=2" && r.sameOrigin && r.origin == "https://app.example");
    // Same origin: any method and type, with an Origin header when not GET.
    CHECK(Prepare(kAppAddress, "/api", UC_METHOD_PUT, Bytes("{}"), "application/json", r, why) == UC_OK);
    CHECK(r.method == "PUT" && r.sendOrigin && r.contentType == "application/json");
    CHECK(Prepare(kAppAddress, "/api", UC_METHOD_DELETE, {}, "", r, why) == UC_OK);
    // Another origin: simple requests only.
    CHECK(Prepare(kAppAddress, "https://api.example/x", UC_METHOD_POST, Bytes("a=1"),
                  "application/x-www-form-urlencoded; charset=utf-8", r, why) == UC_OK);
    CHECK(Prepare(kAppAddress, "https://api.example/x", UC_METHOD_POST, Bytes("{}"), "Application/JSON", r, why) == UC_ERR_DENIED);
    CHECK(Prepare(kAppAddress, "https://api.example/x", UC_METHOD_DELETE, {}, "", r, why) == UC_ERR_DENIED);
    CHECK(Prepare(kAppAddress, "https://api.example/x", UC_METHOD_HEAD, {}, "", r, why) == UC_OK);
    // Never: http from https, other schemes, credentials in the URL, a
    // content type that would end its header line.
    CHECK(Prepare(kAppAddress, "http://app.example/x", UC_METHOD_GET, {}, "", r, why) == UC_ERR_DENIED);
    CHECK(Prepare(kAppAddress, "ftp://files.example/x", UC_METHOD_GET, {}, "", r, why) == UC_ERR_DENIED);
    CHECK(Prepare(kAppAddress, "https://user:pw@app.example/x", UC_METHOD_GET, {}, "", r, why) == UC_ERR_DENIED);
    CHECK(Prepare(kAppAddress, "/x", UC_METHOD_POST, Bytes("a"), "text/plain\r\nX-Evil: 1", r, why) == UC_ERR_STATE);
    CHECK(Prepare(kAppAddress, "/x", UC_METHOD_GET, Bytes("a"), "", r, why) == UC_ERR_STATE);
    CHECK(Prepare(kAppAddress, "/x\r\nHost: evil.example", UC_METHOD_GET, {}, "", r, why) == UC_ERR_STATE);
    CHECK(Prepare(kAppAddress, "data:text/plain,hi", UC_METHOD_GET, {}, "", r, why) != UC_OK);
    CHECK(Prepare(kAppAddress, "/x", 99, {}, "", r, why) == UC_ERR_STATE);
    CHECK(Prepare(kAppAddress, std::string(9000, 'a'), UC_METHOD_GET, {}, "", r, why) == UC_ERR_LIMIT);
    // An http app may fetch https.
    CHECK(Prepare("http://app.example/a.wasm", "https://app.example/x", UC_METHOD_GET, {}, "", r, why) == UC_OK);
    CHECK(!r.sameOrigin);
    // No origin (a file, about:): absolute http(s) only, and as "null".
    CHECK(Prepare("about:demo", "data.json", UC_METHOD_GET, {}, "", r, why) == UC_ERR_DENIED);
    CHECK(Prepare("/home/me/app.wasm", "https://api.example/x", UC_METHOD_GET, {}, "", r, why) == UC_OK);
    CHECK(r.origin == "null" && !r.sameOrigin && r.sendOrigin);
    const FetchRequest opaque = r;

    FetchHeaders visible;
    FetchRequest same;
    CHECK(Prepare(kAppAddress, "data.json", UC_METHOD_GET, {}, "", same, why) == UC_OK);
    // Redirected to another origin: then CORS decides.
    FetchResponse away = Ok("x", {{"Access-Control-Allow-Origin", "https://app.example"}, {"X-A", "1"}});
    away.finalUrl = "https://cdn.example/x";
    CHECK(Admit(same, away, visible, why) == UC_OK);
    CHECK(visible.empty());   // X-A is not safelisted, and nothing is exposed
    away.headers = {{"Access-Control-Allow-Origin", "https://evil.example"}};
    CHECK(Admit(same, away, visible, why) == UC_ERR_DENIED);
    // "null" is never an allowed origin: only "*" lets an app without one in.
    CHECK(Admit(opaque, Ok("x", {{"Access-Control-Allow-Origin", "null"}}), visible, why) == UC_ERR_DENIED);
    CHECK(Admit(opaque, Ok("x", {{"Access-Control-Allow-Origin", "*"}}), visible, why) == UC_OK);
    // Expose "*": every header but Set-Cookie; names in lower case.
    CHECK(Admit(opaque, Ok("x", {{"Access-Control-Allow-Origin", "*"}, {"Access-Control-Expose-Headers", "*"},
                                 {"X-Trace", "t"}, {"Set-Cookie2", "a"}}), visible, why) == UC_OK);
    bool trace = false, cookie = false;
    for (const auto& [name, value] : visible) {
        trace = trace || name == "x-trace";
        cookie = cookie || name.rfind("set-cookie", 0) == 0;
    }
    CHECK(trace && !cookie);
    // An HTTP error status is an answer, not a failure.
    FetchResponse missing = Ok("no");
    missing.status = 404;
    CHECK(Admit(same, missing, visible, why) == UC_OK);
    FetchResponse ftp = Ok("x");
    ftp.finalUrl = "ftp://files.example/x";
    CHECK(Admit(same, ftp, visible, why) == UC_ERR_DENIED);
}

// ===== ABI v2: STORAGE =====

void TestStorageThroughGuest() {
    Harness h;
    h.storage->Set("old", "1");
    const bool started = h.Start(Guest(R"(
  (data (i32.const 1024) "theme")
  (data (i32.const 1032) "dark")
  (data (i32.const 1040) "\00\ff\01binary")
  (func (export "uc_main")
    (call $check (call $storage_get (i32.const 1024) (i32.const 5) (i32.const 2048) (i32.const 16)) (i32.const -7))
    (call $check (call $storage_set (i32.const 1024) (i32.const 5) (i32.const 1032) (i32.const 4)) (i32.const 0))
    ;; the full length, and what fits
    (call $check (call $storage_get (i32.const 1024) (i32.const 5) (i32.const 2048) (i32.const 2)) (i32.const 4))
    (call $check (i32.load16_u (i32.const 2048)) (i32.const 0x6164))
    ;; values are bytes, not text
    (call $check (call $storage_set (i32.const 1032) (i32.const 4) (i32.const 1040) (i32.const 9)) (i32.const 0))
    ;; keys in byte order: dark, old, theme
    (call $check (call $storage_key (i32.const 0) (i32.const 2048) (i32.const 16)) (i32.const 4))
    (call $check (call $storage_key (i32.const 1) (i32.const 2048) (i32.const 16)) (i32.const 3))
    (call $check (call $storage_key (i32.const 3) (i32.const 2048) (i32.const 16)) (i32.const -7))
    (call $check (call $storage_remove (i32.const 1024) (i32.const 5)) (i32.const 0))
    (call $check (call $storage_remove (i32.const 1024) (i32.const 5)) (i32.const -7))
    ;; a key over 1 KB, a value over the quota, memory the guest does not have
    (call $check (call $storage_set (i32.const 0) (i32.const 1025) (i32.const 0) (i32.const 1)) (i32.const -6))
    (call $check (call $storage_set (i32.const 1024) (i32.const 5) (i32.const 0) (i32.const 6000000)) (i32.const -6))
    (call $check (call $storage_set (i32.const 1024) (i32.const 5) (i32.const 65000) (i32.const 1000)) (i32.const -4)))
)"));
    CHECK(started);
    if (!started) return;
    std::string value;
    CHECK(h.storage->Get("dark", value) && value == std::string("\0\xff\x01" "binary", 9));
    CHECK(!h.storage->Get("theme", value));
    CHECK(h.storage->KeyCount() == 2);

    Harness clear;
    clear.storage->Set("a", "1");
    CHECK(clear.Start(Guest(R"(
  (func (export "uc_main") (call $check (call $storage_clear) (i32.const 0)))
)")));
    CHECK(clear.storage->KeyCount() == 0);

    Harness bare;
    CHECK(bare.Start(Guest(R"(
  (func (export "uc_main")
    (call $check (call $storage_get (i32.const 0) (i32.const 1) (i32.const 0) (i32.const 0)) (i32.const -8))
    (call $check (call $storage_set (i32.const 0) (i32.const 1) (i32.const 0) (i32.const 1)) (i32.const -8))
    (call $check (call $storage_clear) (i32.const -8)))
)"), GuestOptions{}, false));
}

void TestStorageStore() {
    UltraWebStorage store("https://app.example");
    CHECK(store.Set("a", "1") == UC_OK);
    CHECK(store.TakeFlushRequest());
    CHECK(!store.TakeFlushRequest());   // one flush for a run of changes
    CHECK(store.Set("b", "22") == UC_OK);
    CHECK(!store.TakeFlushRequest());
    CHECK(store.Flush());
    CHECK(store.Set("b", "22") == UC_OK);   // no change: nothing to write
    CHECK(!store.TakeFlushRequest());
    CHECK(store.UsedBytes() == 1 + 1 + 1 + 2 + 2 * UltraWebStorage::kEntryOverhead);
    // The quota: a value that would go over it is refused and the old stays.
    CHECK(store.Set("big", std::string(UltraWebStorage::kQuotaBytes - 200, 'x')) == UC_OK);
    CHECK(store.Set("a", std::string(200, 'y')) == UC_ERR_LIMIT);
    std::string value;
    CHECK(store.Get("a", value) && value == "1");
    CHECK(store.Set(std::string(UltraWebStorage::kMaxKeyBytes + 1, 'k'), "") == UC_ERR_LIMIT);
    // Keys walk in byte order, from anywhere.
    std::string key;
    CHECK(store.KeyAt(0, key) && key == "a");
    CHECK(store.KeyAt(2, key) && key == "big");
    CHECK(store.KeyAt(1, key) && key == "b");
    CHECK(!store.KeyAt(3, key));
    CHECK(store.Remove("big") && !store.Remove("big"));
    store.Clear();
    CHECK(store.KeyCount() == 0 && store.UsedBytes() == 0);

    // Partitions: an origin, whatever the path; a file, whatever named it.
    CHECK(UltraWebStorage::PartitionFor("https://App.example/x/y.wasm") == "https://app.example");
    CHECK(UltraWebStorage::PartitionFor("about:demo") == "about:demo");
    const std::string local = UltraWebStorage::PartitionFor("app.wasm");
    CHECK(local.rfind("file://", 0) == 0 && local.size() > 15);
    CHECK(UltraWebStorage::PartitionFor("./app.wasm") == local);
    CHECK(UltraWebStorage::PartitionFor("sub/../app.wasm") == local);
    CHECK(UltraWebStorage::FileNameFor("https://x.org") == "https_3a_2f_2fx.org.json");
    CHECK(UltraWebStorage::FileNameFor("a_b") != UltraWebStorage::FileNameFor("a/b"));
    const std::string longName = UltraWebStorage::FileNameFor("file://" + std::string(300, 'd'));
    CHECK(longName.size() < 130 && longName != UltraWebStorage::FileNameFor("file://" + std::string(301, 'd')));
}

void TestStorageFile() {
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "UltraWebGuestTest-storage";
    std::filesystem::remove_all(dir, ec);
    const std::string folder = PathToUtf8(dir);
    std::string problem;
    {
        auto store = UltraWebStorage::Open(folder, "https://app.example", problem);
        CHECK(problem.empty());
        CHECK(store->Set("count", "12") == UC_OK);
        CHECK(store->Set(std::string("\0key", 4), std::string("\xff\0v", 3)) == UC_OK);
        CHECK(store->Flush());
        CHECK(std::filesystem::exists(PathFromUtf8(store->FilePath())));
        CHECK(store->Set("later", "written when the store closes") == UC_OK);
    }
    {
        auto store = UltraWebStorage::Open(folder, "https://app.example", problem);
        CHECK(problem.empty());
        std::string value;
        CHECK(store->Get("count", value) && value == "12");
        CHECK(store->Get(std::string("\0key", 4), value) && value == std::string("\xff\0v", 3));
        CHECK(store->Get("later", value));
        CHECK(store->KeyCount() == 3);
        // An emptied store takes its file with it.
        store->Clear();
        CHECK(store->Flush());
        CHECK(!std::filesystem::exists(PathFromUtf8(store->FilePath())));
    }
    {
        // A file that holds another partition (a hash collision, or a copy)
        // is left alone, and the store lives in memory.
        auto other = UltraWebStorage::Open(folder, "https://other.example", problem);
        other->Set("k", "v");
        other->Flush();
        const std::string otherFile = other->FilePath();
        const std::string mine = PathToUtf8(dir / UltraWebStorage::FileNameFor("https://app.example"));
        std::filesystem::copy_file(PathFromUtf8(otherFile), PathFromUtf8(mine), ec);
        auto store = UltraWebStorage::Open(folder, "https://app.example", problem);
        CHECK(problem.find("https://other.example") != std::string::npos);
        CHECK(store->KeyCount() == 0 && store->FilePath().empty());
        store->Set("x", "y");
        store->Flush();
        auto again = UltraWebStorage::Open(folder, "https://other.example", problem);
        std::string value;
        CHECK(again->Get("k", value) && value == "v");
    }
    std::filesystem::remove_all(dir, ec);
}

// ===== ABI v2: CLIPBOARD =====

void TestClipboard() {
    Harness h;
    const bool started = h.Start(Guest(R"(
  (data (i32.const 1024) "copied")
  (data (i32.const 1032) "write")
  (global $button (mut i32) (i32.const 0))
  (func (export "uc_main")
    (global.set $button (call $create (i32.const 32) (i32.const 6)))
    (drop (call $insert (i32.const 1) (global.get $button) (i32.const 0)))
    (drop (call $listen (global.get $button) (i32.const 1)))
    (drop (call $timer_start (i32.const 10) (i32.const 0)))
    ;; Not in a user action: refused.
    (call $say (i32.const 1032) (i32.const 5) (call $clipboard_write (i32.const 1024) (i32.const 6))))
  (func (export "uc_event") (param $h i32) (param $e i32) (param $d i32)
    (call $say (i32.const 1032) (i32.const 5) (call $clipboard_write (i32.const 1024) (i32.const 6))))
)"));
    CHECK(started);
    if (!started) return;
    auto button = h.Get<UltraCanvasButton>(2);
    if (button) button->onClick();   // a click: allowed
    if (!h.timers.running.empty()) h.timers.Fire(h.timers.running.begin()->first);   // a timer: not
    CHECK(SameLines(h.AppLogs(), {"write -8", "write 0", "write -8"}));
    CHECK(h.clipboard.size() == 1 && h.clipboard[0] == "copied");
    CHECK(h.HostLogged("only while it handles a click"));
}

} // namespace

int main() {
    std::printf("UltraWebGuestTest: %s\n", UltraCanvasWasm_EngineDescription().c_str());
    TestLoader();
    if (!UltraCanvasWasm_IsAvailable()) {
        Harness h;
        CHECK(!h.Start(DemoAppWat()));
        CHECK(h.error.find("no WebAssembly engine") != std::string::npos);
    } else {
        TestDemoApp();
        TestTreeAndMisuse();
        TestReleaseSubtree();
        TestLimits();
        TestFailuresAfterStart();
        TestSizedChildrenNotStretched();
        TestTimers();
        TestFetchThroughGuest();
        TestFetchOutcomes();
        TestStorageThroughGuest();
        TestClipboard();
    }
    // The rules and the store need no engine.
    TestFetchRules();
    TestStorageStore();
    TestStorageFile();
    std::printf("UltraWebGuestTest: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
