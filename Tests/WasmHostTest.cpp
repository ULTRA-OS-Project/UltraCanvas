// Tests/WasmHostTest.cpp
// Unit tests for the WasmHost module (WasmHost/UltraCanvasWasmHost.h), the
// WebAssembly runner under UltraWeb. Every guest is written inline as
// WebAssembly text, so the test needs no wasm toolchain: calls in both
// directions, guest memory through the caller, the limits (time, memory),
// and every way a guest can fail - which must end in a status, never in a
// crash or a hang of the host.
// Without an engine (ULTRACANVAS_ENABLE_WASM_HOST=OFF) it checks that
// every call says so, and passes.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "WasmHost/UltraCanvasWasmHost.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace UltraCanvas;

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

std::vector<uint8_t> Text(const std::string& wat) { return std::vector<uint8_t>(wat.begin(), wat.end()); }

std::unique_ptr<UltraCanvasWasmInstance> Load(const std::string& wat, const std::vector<WasmImport>& imports = {},
                                              WasmLimits limits = WasmLimits{}) {
    WasmStatus status;
    auto instance = UltraCanvasWasmInstance::Create(Text(wat), imports, limits, status);
    if (!instance) std::printf("  load failed: %s\n", status.message.c_str());
    return instance;
}

void TestNoEngine() {
    WasmStatus status;
    auto instance = UltraCanvasWasmInstance::Create(Text("(module)"), {}, WasmLimits{}, status);
    CHECK(!instance);
    CHECK(!status.ok);
    CHECK(status.message.find("no WebAssembly engine") != std::string::npos);
}

void TestCallExport() {
    auto instance = Load(R"((module
        (func (export "add") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add)
        (func (export "half") (param f64) (result f64) local.get 0 f64.const 0.5 f64.mul)))");
    CHECK(instance != nullptr);
    if (!instance) return;
    CHECK(instance->HasFunction("add"));
    CHECK(!instance->HasFunction("missing"));
    std::vector<WasmValue> results;
    CHECK(instance->Call("add", {WasmI32(40), WasmI32(2)}, &results).ok);
    CHECK(results.size() == 1 && results[0].i32 == 42);
    CHECK(instance->Call("half", {WasmF64(5.0)}, &results).ok);
    CHECK(results.size() == 1 && results[0].f64 == 2.5);
    // Wrong argument count and unknown exports are refused, not called.
    WasmStatus wrong = instance->Call("add", {WasmI32(1)}, &results);
    CHECK(!wrong.ok && !wrong.trapped);
    CHECK(!instance->Call("missing", {}).ok);
    CHECK(instance->IsUsable());
}

void TestImportAndMemory() {
    std::string received;
    int doubled = 0;
    std::vector<WasmImport> imports;
    imports.push_back({"env", "twice", {WasmValueType::I32}, {WasmValueType::I32},
        [&doubled](WasmCaller&, const WasmValue* args, WasmValue* results) {
            ++doubled;
            results[0].i32 = args[0].i32 * 2;
        }});
    imports.push_back({"env", "take", {WasmValueType::I32, WasmValueType::I32}, {WasmValueType::I32},
        [&received](WasmCaller& caller, const WasmValue* args, WasmValue* results) {
            std::string text;
            const bool inside = caller.ReadMemory(uint32_t(args[0].i32), uint32_t(args[1].i32), text);
            if (inside) received = text;
            results[0].i32 = inside ? 1 : 0;
        }});
    imports.push_back({"env", "give", {WasmValueType::I32}, {},
        [](WasmCaller& caller, const WasmValue* args, WasmValue*) {
            const char reply[] = "pong";
            caller.WriteMemory(uint32_t(args[0].i32), reply, 4);
        }});
    auto instance = Load(R"((module
        (import "env" "twice" (func $twice (param i32) (result i32)))
        (import "env" "take" (func $take (param i32 i32) (result i32)))
        (import "env" "give" (func $give (param i32)))
        (memory (export "memory") 1)
        (data (i32.const 16) "hello")
        (func (export "quad") (param i32) (result i32) local.get 0 call $twice call $twice)
        (func (export "send") (result i32) i32.const 16 i32.const 5 call $take)
        (func (export "sendOutside") (result i32) i32.const 65530 i32.const 100 call $take)
        (func (export "receive") (result i32) i32.const 32 call $give i32.const 32 i32.load)))", imports);
    CHECK(instance != nullptr);
    if (!instance) return;
    std::vector<WasmValue> results;
    CHECK(instance->Call("quad", {WasmI32(5)}, &results).ok);
    CHECK(results[0].i32 == 20 && doubled == 2);
    CHECK(instance->Call("send", {}, &results).ok);
    CHECK(results[0].i32 == 1 && received == "hello");
    // A pointer running off the end of guest memory is refused by the
    // caller, and the host function reports it as a value.
    CHECK(instance->Call("sendOutside", {}, &results).ok);
    CHECK(results[0].i32 == 0);
    CHECK(instance->Call("receive", {}, &results).ok);
    CHECK(results[0].i32 == ('p' | ('o' << 8) | ('n' << 16) | ('g' << 24)));
}

void TestTraps() {
    std::vector<WasmImport> imports;
    imports.push_back({"env", "refuse", {}, {},
        [](WasmCaller& caller, const WasmValue*, WasmValue*) { caller.Trap("refused by the host"); }});
    imports.push_back({"env", "throws", {}, {},
        [](WasmCaller&, const WasmValue*, WasmValue*) { throw std::runtime_error("boom"); }});
    auto instance = Load(R"((module
        (import "env" "refuse" (func $refuse))
        (import "env" "throws" (func $throws))
        (func (export "callRefuse") call $refuse)
        (func (export "callThrows") call $throws)
        (func (export "crash") unreachable)
        (func (export "ok") (result i32) i32.const 7)))", imports);
    CHECK(instance != nullptr);
    if (!instance) return;
    WasmStatus refused = instance->Call("callRefuse", {});
    CHECK(!refused.ok && refused.trapped && !refused.interrupted);
    CHECK(refused.message.find("refused by the host") != std::string::npos);
    // After a trap the instance refuses further calls.
    CHECK(!instance->IsUsable());
    CHECK(!instance->Call("ok", {}).ok);

    auto second = Load(R"((module
        (import "env" "refuse" (func $refuse))
        (import "env" "throws" (func $throws))
        (func (export "callThrows") call $throws)))", imports);
    CHECK(second != nullptr);
    if (second) {
        // A C++ exception in a host function becomes a trap, not a crash.
        WasmStatus thrown = second->Call("callThrows", {});
        CHECK(!thrown.ok && thrown.trapped);
        CHECK(thrown.message.find("boom") != std::string::npos);
    }

    auto third = Load(R"((module (func (export "crash") unreachable)))");
    CHECK(third != nullptr);
    if (third) {
        WasmStatus crashed = third->Call("crash", {});
        CHECK(!crashed.ok && crashed.trapped);
    }
}

void TestTimeLimit() {
    WasmLimits limits;
    limits.callTimeoutMs = 100;
    auto instance = Load(R"((module (func (export "spin") (loop $l br $l))))", {}, limits);
    CHECK(instance != nullptr);
    if (!instance) return;
    const auto start = std::chrono::steady_clock::now();
    WasmStatus status = instance->Call("spin", {});
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK(!status.ok && status.interrupted && !status.trapped);
    CHECK(elapsed >= 90 && elapsed < 2000);
    CHECK(!instance->IsUsable());
}

void TestMemoryLimit() {
    WasmLimits limits;
    limits.memoryBytes = 1024 * 1024;   // 16 pages
    auto instance = Load(R"((module (memory 1)
        (func (export "grow") (param i32) (result i32) local.get 0 memory.grow)))", {}, limits);
    CHECK(instance != nullptr);
    if (!instance) return;
    std::vector<WasmValue> results;
    // Within the cap: the old size in pages. Past it: -1, as the spec says
    // for a refused grow, and the guest carries on.
    CHECK(instance->Call("grow", {WasmI32(4)}, &results).ok);
    CHECK(results[0].i32 == 1);
    CHECK(instance->Call("grow", {WasmI32(100)}, &results).ok);
    CHECK(results[0].i32 == -1);
    CHECK(instance->IsUsable());
}

void TestReactorAndBadInput() {
    // A WASI reactor exports _initialize, which Create runs first.
    auto reactor = Load(R"((module
        (global $ready (mut i32) (i32.const 0))
        (func (export "_initialize") i32.const 1 global.set $ready)
        (func (export "ready") (result i32) global.get $ready)))");
    CHECK(reactor != nullptr);
    if (reactor) {
        std::vector<WasmValue> results;
        CHECK(reactor->Call("ready", {}, &results).ok);
        CHECK(results[0].i32 == 1);
    }

    WasmStatus status;
    std::vector<uint8_t> garbage = {0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00, 0xff, 0xff};
    CHECK(UltraCanvasWasmInstance::Create(garbage, {}, WasmLimits{}, status) == nullptr);
    CHECK(!status.ok && status.message.find("does not compile") != std::string::npos);
    CHECK(UltraCanvasWasmInstance::Create(Text("<html>not wasm</html>"), {}, WasmLimits{}, status) == nullptr);
    CHECK(!status.ok && status.message.find("not a WebAssembly module") != std::string::npos);
    // An import the module asks for and the host does not offer.
    CHECK(UltraCanvasWasmInstance::Create(Text(R"((module (import "env" "nothing" (func))))"), {}, WasmLimits{}, status) == nullptr);
    CHECK(!status.ok);
}

void TestWatToWasm() {
    std::vector<uint8_t> binary;
    CHECK(UltraCanvasWasm_WatToWasm("(module)", binary).ok);
    CHECK(binary.size() == 8 && binary[0] == 0x00 && binary[1] == 'a');
    CHECK(!UltraCanvasWasm_WatToWasm("(module (func (i32.bogus)))", binary).ok);
}

} // namespace

int main() {
    std::printf("WasmHostTest: %s\n", UltraCanvasWasm_EngineDescription().c_str());
    if (!UltraCanvasWasm_IsAvailable()) {
        TestNoEngine();
    } else {
        TestCallExport();
        TestImportAndMemory();
        TestTraps();
        TestTimeLimit();
        TestMemoryLimit();
        TestReactorAndBadInput();
        TestWatToWasm();
    }
    std::printf("WasmHostTest: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
