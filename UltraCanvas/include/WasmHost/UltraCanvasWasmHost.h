// include/WasmHost/UltraCanvasWasmHost.h
// Runs WebAssembly modules for UltraCanvas applications: compile, link host
// functions, instantiate, call exports, with a memory cap and a time limit
// per call. The engine behind it (wasmtime, through its C API) is an
// implementation detail: nothing here names a wasmtime type, so it can be
// replaced without touching callers (Docs/UltraWeb/UltraWebProposal.md §4.1).
//
// UI-free and thread-agnostic: an instance is used from one thread at a
// time, the one that created it. UltraWeb runs its guests on the UI thread.
//
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace UltraCanvas {

enum class WasmValueType { I32, I64, F32, F64 };

// One argument or result. The type is fixed by the import or export
// signature, so the value carries no tag of its own.
union WasmValue {
    int32_t i32;
    int64_t i64;
    float   f32;
    double  f64;
};

inline WasmValue WasmI32(int32_t v) { WasmValue x; x.i64 = 0; x.i32 = v; return x; }
inline WasmValue WasmF64(double v)  { WasmValue x; x.f64 = v; return x; }

// What a host function sees of the guest that called it.
class WasmCaller {
public:
    virtual ~WasmCaller() = default;
    // The guest's linear memory, bounds-checked: false when [offset,
    // offset + length) is not inside it. A guest pointer is an offset.
    virtual bool ReadMemory(uint32_t offset, uint32_t length, std::string& out) const = 0;
    virtual bool WriteMemory(uint32_t offset, const void* data, uint32_t length) = 0;
    // Make the guest's call fail with this message once the host function
    // returns (a trap). Use it for a broken contract, not for a recoverable
    // error the ABI can report as a return value.
    virtual void Trap(const std::string& message) = 0;
};

using WasmHostFunction = std::function<void(WasmCaller& caller, const WasmValue* args, WasmValue* results)>;

// A function the host offers to guests under (module, name).
struct WasmImport {
    std::string module;
    std::string name;
    std::vector<WasmValueType> params;
    std::vector<WasmValueType> results;
    WasmHostFunction function;
};

struct WasmLimits {
    // Linear memory a guest may grow to.
    uint64_t memoryBytes = 256ull * 1024 * 1024;
    // Longest a single call into the guest may run before it is
    // interrupted; 0 means no limit. Resolution is about 10 ms.
    uint32_t callTimeoutMs = 2000;
};

// The outcome of a load or a call. `interrupted` means the time limit hit;
// `trapped` covers every other runtime failure (a guest bug, a host Trap()).
struct WasmStatus {
    bool ok = true;
    bool trapped = false;
    bool interrupted = false;
    std::string message;

    static WasmStatus Success() { return {}; }
    static WasmStatus Failure(std::string text) { WasmStatus s; s.ok = false; s.message = std::move(text); return s; }
};

class UltraCanvasWasmInstance {
public:
    // Compiles `module` (binary .wasm, or WebAssembly text starting with
    // "(module"), links the imports plus WASI preview 1 (clocks, random,
    // stdout/stderr; no files, no environment), and instantiates it. WASI
    // reactors get their `_initialize` called here.
    static std::unique_ptr<UltraCanvasWasmInstance> Create(const std::vector<uint8_t>& module,
                                                           const std::vector<WasmImport>& imports,
                                                           const WasmLimits& limits,
                                                           WasmStatus& status);
    ~UltraCanvasWasmInstance();
    UltraCanvasWasmInstance(const UltraCanvasWasmInstance&) = delete;
    UltraCanvasWasmInstance& operator=(const UltraCanvasWasmInstance&) = delete;

    bool HasFunction(const std::string& exportName) const;
    // Calls an exported function. `args` must match its parameters and
    // `results` is resized to its result count.
    WasmStatus Call(const std::string& exportName, const std::vector<WasmValue>& args,
                    std::vector<WasmValue>* results = nullptr);
    // False after a trap or an interruption: the guest's state may be
    // half-updated, so callers should stop using it.
    bool IsUsable() const;

private:
    UltraCanvasWasmInstance();
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// True when the library was built with a WebAssembly engine. Without one,
// Create() always fails with an explanation, so callers can say so.
bool UltraCanvasWasm_IsAvailable();
// "wasmtime 49.0.2", or the reason there is no engine.
std::string UltraCanvasWasm_EngineDescription();
// WebAssembly text to binary; Create() does this itself for text input.
WasmStatus UltraCanvasWasm_WatToWasm(std::string_view wat, std::vector<uint8_t>& out);

} // namespace UltraCanvas
