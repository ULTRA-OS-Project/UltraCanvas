// Apps/UltraWeb/browser/test/wasmhost-browser-test.cpp
// Unit test of the browser engine behind UltraCanvasWasmHost
// (UltraCanvas/OS/WASM/UltraCanvasWASMWasmHost.cpp), run under Node: the
// same promises Tests/WasmHostTest.cpp checks on wasmtime - calls in both
// directions with every value type, guest memory through the caller, traps
// from the guest and from the host, refused imports, WASI, the memory cap -
// on the JavaScript engine's WebAssembly instead. The guests are
// wasmhost-guest.c, built twice by ../CMakeLists.txt and embedded at /.
//
//   node wasmhost-browser-test.js
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "WasmHost/UltraCanvasWasmHost.h"

#include <cstdio>
#include <fstream>
#include <iterator>
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

using T = WasmValueType;

std::vector<uint8_t> ReadAll(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Host {
    std::string recorded;
    bool recordFailed = false;
    std::vector<WasmImport> imports;

    Host() {
        imports.push_back({"env", "twice", {T::I32}, {T::I32}, [](WasmCaller&, const WasmValue* a, WasmValue* r) {
            r[0].i32 = a[0].i32 * 2;
        }});
        imports.push_back({"env", "record", {T::I32, T::I32}, {}, [this](WasmCaller& c, const WasmValue* a, WasmValue*) {
            if (!c.ReadMemory(uint32_t(a[0].i32), uint32_t(a[1].i32), recorded)) {
                recordFailed = true;
                c.Trap("record: a pointer outside guest memory");
            }
        }});
        imports.push_back({"env", "fill", {T::I32, T::I32}, {T::I32}, [](WasmCaller& c, const WasmValue* a, WasmValue* r) {
            const char text[] = "\x01\x02\x03\x04";
            r[0].i32 = c.WriteMemory(uint32_t(a[0].i32), text, 4) ? 4 : -1;
        }});
        imports.push_back({"env", "fail", {}, {}, [](WasmCaller& c, const WasmValue*, WasmValue*) {
            c.Trap("the host says no");
        }});
        imports.push_back({"env", "wide", {T::I64}, {T::I64}, [](WasmCaller&, const WasmValue* a, WasmValue* r) {
            r[0].i64 = a[0].i64 * 3;
        }});
        imports.push_back({"env", "scale", {T::F64, T::F32}, {T::F64}, [](WasmCaller&, const WasmValue* a, WasmValue* r) {
            r[0].f64 = a[0].f64 * double(a[1].f32);
        }});
    }
};

std::unique_ptr<UltraCanvasWasmInstance> Load(const std::vector<uint8_t>& module, const Host& host, WasmStatus& status,
                                              WasmLimits limits = WasmLimits{}) {
    return UltraCanvasWasmInstance::Create(module, host.imports, limits, status);
}

void TestCalls(const std::vector<uint8_t>& guest) {
    Host host;
    WasmStatus status;
    auto instance = Load(guest, host, status);
    CHECK(instance != nullptr);
    if (!instance) { std::printf("  load failed: %s\n", status.message.c_str()); return; }
    CHECK(UltraCanvasWasm_IsAvailable());
    std::vector<WasmValue> results;
    // _initialize ran during Create, as a WASI reactor's must.
    CHECK(instance->Call("initialized_value", {}, &results).ok && results.size() == 1 && results[0].i32 == 7);
    CHECK(instance->HasFunction("add") && !instance->HasFunction("missing") && !instance->HasFunction("memory"));
    CHECK(instance->Call("add", {WasmI32(40), WasmI32(2)}, &results).ok && results[0].i32 == 42);
    CHECK(instance->Call("half", {WasmF64(5.0)}, &results).ok && results[0].f64 == 2.5);
    WasmValue big;
    big.i64 = 5000000000LL;
    CHECK(instance->Call("wide", {big}, &results).ok && results[0].i64 == 15000000001LL);
    CHECK(instance->Call("scaled", {WasmF64(8.0)}, &results).ok && results[0].f64 == 2.0);
    CHECK(instance->Call("call_twice", {WasmI32(20)}, &results).ok && results[0].i32 == 41);
    // Refused before the guest runs, and the instance stays usable.
    WasmStatus wrong = instance->Call("add", {WasmI32(1)}, &results);
    CHECK(!wrong.ok && !wrong.trapped);
    CHECK(!instance->Call("missing", {}).ok);
    CHECK(instance->IsUsable());

    CHECK(instance->Call("send", {}).ok && host.recorded == "hello host");
    CHECK(instance->Call("receive", {}, &results).ok && results[0].i32 == 10);
    CHECK(instance->Call("print", {}).ok);
}

void TestHostTrap(const std::vector<uint8_t>& guest) {
    Host host;
    WasmStatus status;
    auto instance = Load(guest, host, status);
    if (!instance) { CHECK(false); return; }
    WasmStatus trapped = instance->Call("host_trap", {});
    CHECK(!trapped.ok && trapped.trapped);
    CHECK(trapped.message.find("the host says no") != std::string::npos);
    CHECK(!instance->IsUsable());
    CHECK(!instance->Call("add", {WasmI32(1), WasmI32(2)}).ok);
}

void TestBadPointer(const std::vector<uint8_t>& guest) {
    Host host;
    WasmStatus status;
    auto instance = Load(guest, host, status);
    if (!instance) { CHECK(false); return; }
    WasmStatus trapped = instance->Call("bad_pointer", {});
    CHECK(host.recordFailed);
    CHECK(!trapped.ok && trapped.message.find("outside guest memory") != std::string::npos);
}

void TestGuestTrap(const std::vector<uint8_t>& guest) {
    Host host;
    WasmStatus status;
    auto instance = Load(guest, host, status);
    if (!instance) { CHECK(false); return; }
    WasmStatus trapped = instance->Call("guest_trap", {});
    CHECK(!trapped.ok && trapped.trapped);
    CHECK(!instance->IsUsable());
}

void TestExit(const std::vector<uint8_t>& guest) {
    Host host;
    WasmStatus status;
    auto instance = Load(guest, host, status);
    if (!instance) { CHECK(false); return; }
    WasmStatus exited = instance->Call("quit", {});
    CHECK(!exited.ok && exited.message.find("exited (status 3)") != std::string::npos);
}

void TestMemoryCap(const std::vector<uint8_t>& guest) {
    Host host;
    WasmStatus status;
    WasmLimits limits;
    limits.memoryBytes = 4u << 20;
    auto instance = Load(guest, host, status, limits);
    if (!instance) { CHECK(false); return; }
    std::vector<WasmValue> results;
    // A page is 64 KiB: 1 fits under 4 MiB, 200 more do not.
    CHECK(instance->Call("grow", {WasmI32(1)}, &results).ok);
    WasmStatus over = instance->Call("grow", {WasmI32(200)}, &results);
    CHECK(!over.ok && over.message.find("over the limit") != std::string::npos);
}

void TestRefusedModules(const std::vector<uint8_t>& guest, const std::vector<uint8_t>& unknownImport) {
    Host host;
    WasmStatus status;
    CHECK(!Load(unknownImport, host, status) && status.message.find("unknown import env.missing") != std::string::npos);

    Host wrongSignature;
    wrongSignature.imports[0].params = {T::I64};
    CHECK(!Load(guest, wrongSignature, status) && status.message.find("wrong signature") != std::string::npos);

    std::string text = "(module)";
    CHECK(!Load(std::vector<uint8_t>(text.begin(), text.end()), host, status));
    CHECK(status.message.find("binary") != std::string::npos);

    std::vector<uint8_t> cut(guest.begin(), guest.begin() + guest.size() / 2);
    CHECK(!Load(cut, host, status));

    std::vector<uint8_t> wat;
    CHECK(!UltraCanvasWasm_WatToWasm("(module)", wat).ok);
}

} // namespace

int main() {
    const std::vector<uint8_t> guest = ReadAll("/wasmhost-guest.wasm");
    const std::vector<uint8_t> unknownImport = ReadAll("/wasmhost-guest-unknown.wasm");
    CHECK(guest.size() > 8 && unknownImport.size() > 8);
    std::printf("engine: %s\n", UltraCanvasWasm_EngineDescription().c_str());
    TestCalls(guest);
    TestHostTrap(guest);
    TestBadPointer(guest);
    TestGuestTrap(guest);
    TestExit(guest);
    TestMemoryCap(guest);
    TestRefusedModules(guest, unknownImport);
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
