// Tests/UltraWebGuestTest.cpp
// UltraWeb's element bridge (Apps/UltraWeb/host/UltraWebGuest.cpp) and its
// loader, against guests written inline as WebAssembly text and against the
// built-in about:demo app: the element tree a guest builds, events into it,
// insertion order, release of subtrees, the limits, and every misuse of the
// ABI a guest can make - which must come back as an error code, never as a
// crash of the browser. Elements are created without a window; nothing is
// laid out or drawn, so no display is needed.
// Without a WebAssembly engine it checks that a start fails and says so.
// Version: 0.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "host/UltraWebDemoApp.h"
#include "host/UltraWebGuest.h"
#include "host/UltraWebLoader.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "WasmHost/UltraCanvasWasmHost.h"

#include <cstdio>
#include <cstring>
#include <string>
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

struct Harness {
    std::shared_ptr<UltraCanvasContainer> root = CreateContainer("testRoot", 0, 0, 800, 600);
    std::string failure;
    std::vector<std::string> logs;
    std::unique_ptr<UltraWebGuest> guest;
    std::string error;

    bool Start(const std::string& wat, GuestOptions options = GuestOptions{}) {
        options.onFailure = [this](const std::string& message) { failure = message; };
        options.onLog = [this](const std::string& line) { logs.push_back(line); };
        options.defer = [](std::function<void()> task) { task(); };
        guest = UltraWebGuest::Start(Bytes(wat), root, std::move(options), error);
        if (!guest) std::printf("  start failed: %s\n", error.c_str());
        return guest != nullptr;
    }
    template <class T> std::shared_ptr<T> Get(uint32_t handle) const {
        return std::dynamic_pointer_cast<T>(guest->ElementForTest(handle));
    }
};

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
)";

std::string Guest(const std::string& body) { return std::string("(module ") + kPrelude + body + ")"; }

void TestDemoApp() {
    Harness h;
    CHECK(h.Start(DemoAppWat()));
    if (!h.guest) return;
    // Title, description, row, button, count, input, echo, checkbox.
    CHECK(h.guest->ElementCount() == 8);
    CHECK(h.root->GetChildren().size() == 6);
    CHECK(!h.logs.empty() && h.logs[0] == "about:demo started");

    // Handles are handed out in creation order from 2 (1 is the root).
    auto button = h.Get<UltraCanvasButton>(5);
    auto counter = h.Get<UltraCanvasLabel>(6);
    auto input = h.Get<UltraCanvasTextInput>(7);
    auto echo = h.Get<UltraCanvasLabel>(8);
    auto check = h.Get<UltraCanvasCheckbox>(9);
    CHECK(button && counter && input && echo && check);
    if (!(button && counter && input && echo && check)) return;
    CHECK(button->GetText() == "Count");
    CHECK(counter->GetText() == "Clicked 0 times");
    CHECK(input->GetPlaceholder() == "Type something");
    // The guest checked the box itself in uc_main; that raised no event
    // into it (it would have hidden the counter).
    CHECK(check->IsChecked());
    CHECK(counter->IsVisible());

    // A click, through the button's own callback.
    for (int i = 0; i < 12; ++i) button->onClick();
    CHECK(counter->GetText() == "Clicked 12 times");

    // Typing: the input's text, then the change callback a key press runs.
    input->SetText("hello, wasm");
    input->onTextChanged("hello, wasm");
    CHECK(echo->GetText() == "You typed: hello, wasm");

    // Unchecking hides the counter.
    check->SetChecked(false);
    CHECK(!counter->IsVisible());
    check->SetChecked(true);
    CHECK(counter->IsVisible());
    CHECK(h.failure.empty() && !h.guest->HasFailed());

    // Destroying the guest takes its elements out of the root.
    h.guest.reset();
    CHECK(h.root->GetChildren().empty());
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
    }
    std::printf("UltraWebGuestTest: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
