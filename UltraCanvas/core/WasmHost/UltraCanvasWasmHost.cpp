// core/WasmHost/UltraCanvasWasmHost.cpp
// The WebAssembly host on wasmtime's C API (UltraCanvasWasmHost.h). Built
// with ULTRACANVAS_HAS_WASMTIME; without it every entry point reports that
// no engine is present, so applications still link and can say why.
//
// One engine per process, with epoch interruption and the exceptions
// proposal on. A ticker thread advances the epoch every 10 ms; each call
// into a guest sets its deadline to the call time limit in ticks, so a
// guest that never returns traps with an interrupt instead of hanging the
// UI thread. Imports are registered through the unchecked host-function
// API: measured at 7 ns per empty call against 82 ns for the checked one
// (Docs/UltraWeb/Feasibility/results.md).
//
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "WasmHost/UltraCanvasWasmHost.h"

#include <cstring>

#ifdef ULTRACANVAS_HAS_WASMTIME
#include <wasi.h>
#include <wasmtime.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <thread>
#endif

namespace UltraCanvas {

#ifdef ULTRACANVAS_HAS_WASMTIME

namespace {

bool LooksLikeBinary(const std::vector<uint8_t>& module) {
    return module.size() >= 4 && module[0] == 0x00 && module[1] == 'a' && module[2] == 's' && module[3] == 'm';
}

constexpr uint32_t kTickMs = 10;
constexpr size_t kMaxSignatureValues = 16;

// The process's one engine and the thread that ticks its epoch.
class Engine {
public:
    static Engine& Get() {
        static Engine engine;
        return engine;
    }
    wasm_engine_t* Handle() const { return engine_; }

private:
    Engine() {
        wasm_config_t* config = wasm_config_new();
        wasmtime_config_epoch_interruption_set(config, true);
        wasmtime_config_wasm_exceptions_set(config, true);
        engine_ = wasm_engine_new_with_config(config);   // takes the config
        ticker_ = std::thread([this]() {
            while (!stop_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
                wasmtime_engine_increment_epoch(engine_);
            }
        });
    }
    ~Engine() {
        stop_.store(true);
        if (ticker_.joinable()) ticker_.join();
        wasm_engine_delete(engine_);
    }

    wasm_engine_t* engine_ = nullptr;
    std::atomic<bool> stop_{false};
    std::thread ticker_;
};

std::string TakeMessage(wasmtime_error_t* error) {
    wasm_byte_vec_t text;
    wasmtime_error_message(error, &text);
    std::string message(text.data, text.size);
    wasm_byte_vec_delete(&text);
    wasmtime_error_delete(error);
    return message;
}

std::string TakeMessage(wasm_trap_t* trap) {
    wasm_byte_vec_t text;
    wasm_trap_message(trap, &text);
    std::string message(text.data, text.size);
    wasm_byte_vec_delete(&text);
    return message;
}

wasm_valkind_t KindOf(WasmValueType type) {
    switch (type) {
        case WasmValueType::I32: return WASM_I32;
        case WasmValueType::I64: return WASM_I64;
        case WasmValueType::F32: return WASM_F32;
        case WasmValueType::F64: return WASM_F64;
    }
    return WASM_I32;
}

wasm_functype_t* MakeFuncType(const WasmImport& import) {
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, import.params.size());
    for (size_t i = 0; i < import.params.size(); ++i) params.data[i] = wasm_valtype_new(KindOf(import.params[i]));
    wasm_valtype_vec_new_uninitialized(&results, import.results.size());
    for (size_t i = 0; i < import.results.size(); ++i) results.data[i] = wasm_valtype_new(KindOf(import.results[i]));
    return wasm_functype_new(&params, &results);   // takes both vectors
}

uint64_t DeadlineTicks(const WasmLimits& limits) {
    if (limits.callTimeoutMs == 0) return UINT64_MAX / 2;
    return (limits.callTimeoutMs + kTickMs - 1) / kTickMs;
}

} // namespace

struct UltraCanvasWasmInstance::Impl {
    struct Thunk {
        Impl* owner = nullptr;
        WasmImport import;
    };

    // What a host function sees: the calling store and the guest memory.
    class Caller : public WasmCaller {
    public:
        Caller(Impl* impl, wasmtime_caller_t* caller) : impl_(impl), caller_(caller) {}

        bool ReadMemory(uint32_t offset, uint32_t length, std::string& out) const override {
            uint8_t* base = nullptr; size_t size = 0;
            if (!Memory(base, size)) return false;
            if (uint64_t(offset) + length > size) return false;
            out.assign(reinterpret_cast<const char*>(base + offset), length);
            return true;
        }
        bool WriteMemory(uint32_t offset, const void* data, uint32_t length) override {
            uint8_t* base = nullptr; size_t size = 0;
            if (!Memory(base, size)) return false;
            if (uint64_t(offset) + length > size) return false;
            if (length > 0) std::memcpy(base + offset, data, length);
            return true;
        }
        void Trap(const std::string& message) override {
            trapped = true;
            trapMessage = message;
        }

        bool trapped = false;
        std::string trapMessage;

    private:
        // The base pointer is fetched per access: memory.grow can move it.
        bool Memory(uint8_t*& base, size_t& size) const {
            wasmtime_context_t* context = wasmtime_caller_context(caller_);
            wasmtime_memory_t memory;
            if (impl_->hasMemory) {
                memory = impl_->memory;
            } else {
                // Called during instantiation, before the export was read.
                wasmtime_extern_t item;
                if (!wasmtime_caller_export_get(caller_, "memory", 6, &item)) return false;
                if (item.kind != WASMTIME_EXTERN_MEMORY) { wasmtime_extern_delete(&item); return false; }
                memory = item.of.memory;
            }
            base = wasmtime_memory_data(context, &memory);
            size = wasmtime_memory_data_size(context, &memory);
            return true;
        }

        Impl* impl_;
        wasmtime_caller_t* caller_;
    };

    static wasm_trap_t* Trampoline(void* env, wasmtime_caller_t* caller,
                                   wasmtime_val_raw_t* io, size_t /*ioCount*/) {
        auto* thunk = static_cast<Thunk*>(env);
        const WasmImport& import = thunk->import;
        WasmValue args[kMaxSignatureValues];
        WasmValue results[kMaxSignatureValues];
        std::memset(results, 0, sizeof(results));
        for (size_t i = 0; i < import.params.size(); ++i) {
            switch (import.params[i]) {
                case WasmValueType::I32: args[i].i32 = io[i].i32; break;
                case WasmValueType::I64: args[i].i64 = io[i].i64; break;
                case WasmValueType::F32: args[i].f32 = io[i].f32; break;
                case WasmValueType::F64: args[i].f64 = io[i].f64; break;
            }
        }
        Caller view(thunk->owner, caller);
        try {
            import.function(view, args, results);
        } catch (const std::exception& e) {
            view.Trap(std::string("host function ") + import.name + " threw: " + e.what());
        } catch (...) {
            view.Trap(std::string("host function ") + import.name + " threw");
        }
        if (view.trapped) return wasmtime_trap_new(view.trapMessage.data(), view.trapMessage.size());
        for (size_t i = 0; i < import.results.size(); ++i) {
            switch (import.results[i]) {
                case WasmValueType::I32: io[i].i32 = results[i].i32; break;
                case WasmValueType::I64: io[i].i64 = results[i].i64; break;
                case WasmValueType::F32: io[i].f32 = results[i].f32; break;
                case WasmValueType::F64: io[i].f64 = results[i].f64; break;
            }
        }
        return nullptr;
    }

    ~Impl() {
        if (module) wasmtime_module_delete(module);
        if (linker) wasmtime_linker_delete(linker);
        if (store) wasmtime_store_delete(store);
    }

    // A failed call leaves the guest's state unknown: report it and refuse
    // further calls.
    WasmStatus Fail(wasmtime_error_t* error, wasm_trap_t* trap, const char* what) {
        WasmStatus status;
        status.ok = false;
        status.trapped = true;
        if (error) {
            int exitStatus = 0;
            if (wasmtime_error_exit_status(error, &exitStatus)) {
                wasmtime_error_delete(error);
                status.message = std::string(what) + ": the module exited (status " + std::to_string(exitStatus) + ")";
            } else {
                status.message = std::string(what) + ": " + TakeMessage(error);
            }
        } else if (trap) {
            wasmtime_trap_code_t code;
            if (wasmtime_trap_code(trap, &code) && code == WASMTIME_TRAP_CODE_INTERRUPT) {
                status.trapped = false;
                status.interrupted = true;
                status.message = std::string(what) + ": stopped after running longer than "
                                 + std::to_string(limits.callTimeoutMs) + " ms";
            } else {
                status.message = std::string(what) + ": " + TakeMessage(trap);
            }
            wasm_trap_delete(trap);
        }
        usable = false;
        return status;
    }

    wasmtime_store_t* store = nullptr;
    wasmtime_context_t* context = nullptr;
    wasmtime_linker_t* linker = nullptr;
    wasmtime_module_t* module = nullptr;
    wasmtime_instance_t instance{};
    wasmtime_memory_t memory{};
    bool hasMemory = false;
    bool usable = false;
    WasmLimits limits;
    std::vector<std::unique_ptr<Thunk>> thunks;
};

UltraCanvasWasmInstance::UltraCanvasWasmInstance() : impl(std::make_unique<Impl>()) {}
UltraCanvasWasmInstance::~UltraCanvasWasmInstance() = default;

std::unique_ptr<UltraCanvasWasmInstance> UltraCanvasWasmInstance::Create(const std::vector<uint8_t>& module,
                                                                        const std::vector<WasmImport>& imports,
                                                                        const WasmLimits& limits,
                                                                        WasmStatus& status) {
    std::vector<uint8_t> converted;
    const std::vector<uint8_t>* binary = &module;
    if (!LooksLikeBinary(module)) {
        status = UltraCanvasWasm_WatToWasm(std::string_view(reinterpret_cast<const char*>(module.data()), module.size()), converted);
        if (!status.ok) {
            status.message = "not a WebAssembly module: " + status.message;
            return nullptr;
        }
        binary = &converted;
    }
    for (const WasmImport& import : imports) {
        if (import.params.size() > kMaxSignatureValues || import.results.size() > kMaxSignatureValues || !import.function) {
            status = WasmStatus::Failure("import " + import.module + "." + import.name + " has no function or too many values");
            return nullptr;
        }
    }

    std::unique_ptr<UltraCanvasWasmInstance> self(new UltraCanvasWasmInstance());
    Impl& impl = *self->impl;
    impl.limits = limits;
    wasm_engine_t* engine = Engine::Get().Handle();

    impl.store = wasmtime_store_new(engine, nullptr, nullptr);
    impl.context = wasmtime_store_context(impl.store);
    const int64_t memoryCap = limits.memoryBytes > uint64_t(INT64_MAX) ? -1 : int64_t(limits.memoryBytes);
    wasmtime_store_limiter(impl.store, memoryCap, -1, -1, -1, -1);

    if (wasmtime_error_t* error = wasmtime_module_new(engine, binary->data(), binary->size(), &impl.module)) {
        status = WasmStatus::Failure("the module does not compile: " + TakeMessage(error));
        return nullptr;
    }

    impl.linker = wasmtime_linker_new(engine);
    if (wasmtime_error_t* error = wasmtime_linker_define_wasi(impl.linker)) {
        status = WasmStatus::Failure("WASI: " + TakeMessage(error));
        return nullptr;
    }
    for (const WasmImport& import : imports) {
        auto thunk = std::make_unique<Impl::Thunk>();
        thunk->owner = &impl;
        thunk->import = import;
        wasm_functype_t* type = MakeFuncType(import);
        wasmtime_error_t* error = wasmtime_linker_define_func_unchecked(
            impl.linker, import.module.data(), import.module.size(), import.name.data(), import.name.size(),
            type, &Impl::Trampoline, thunk.get(), nullptr);
        wasm_functype_delete(type);
        if (error) {
            status = WasmStatus::Failure("import " + import.module + "." + import.name + ": " + TakeMessage(error));
            return nullptr;
        }
        impl.thunks.push_back(std::move(thunk));
    }

    // WASI preview 1 with nothing granted: no arguments, no environment, no
    // preopened directories. stdout and stderr are the application's.
    wasi_config_t* wasi = wasi_config_new();
    wasi_config_inherit_stdout(wasi);
    wasi_config_inherit_stderr(wasi);
    if (wasmtime_error_t* error = wasmtime_context_set_wasi(impl.context, wasi)) {   // takes the config
        status = WasmStatus::Failure("WASI: " + TakeMessage(error));
        return nullptr;
    }

    wasmtime_context_set_epoch_deadline(impl.context, DeadlineTicks(limits));
    wasm_trap_t* trap = nullptr;
    wasmtime_error_t* error = wasmtime_linker_instantiate(impl.linker, impl.context, impl.module, &impl.instance, &trap);
    if (error || trap) {
        status = impl.Fail(error, trap, "the module does not start");
        return nullptr;
    }
    impl.usable = true;

    wasmtime_extern_t item;
    if (wasmtime_instance_export_get(impl.context, &impl.instance, "memory", 6, &item)) {
        if (item.kind == WASMTIME_EXTERN_MEMORY) {
            impl.memory = item.of.memory;
            impl.hasMemory = true;
        } else {
            wasmtime_extern_delete(&item);
        }
    }

    if (self->HasFunction("_initialize")) {
        status = self->Call("_initialize", {});
        if (!status.ok) return nullptr;
    }
    status = WasmStatus::Success();
    return self;
}

bool UltraCanvasWasmInstance::HasFunction(const std::string& exportName) const {
    wasmtime_extern_t item;
    if (!wasmtime_instance_export_get(impl->context, &impl->instance, exportName.data(), exportName.size(), &item)) return false;
    const bool isFunction = item.kind == WASMTIME_EXTERN_FUNC;
    wasmtime_extern_delete(&item);
    return isFunction;
}

WasmStatus UltraCanvasWasmInstance::Call(const std::string& exportName, const std::vector<WasmValue>& args,
                                         std::vector<WasmValue>* results) {
    if (!impl->usable) return WasmStatus::Failure("the module stopped after an earlier failure");
    wasmtime_extern_t item;
    if (!wasmtime_instance_export_get(impl->context, &impl->instance, exportName.data(), exportName.size(), &item))
        return WasmStatus::Failure("the module has no export " + exportName);
    if (item.kind != WASMTIME_EXTERN_FUNC) {
        wasmtime_extern_delete(&item);
        return WasmStatus::Failure("export " + exportName + " is not a function");
    }
    wasmtime_func_t func = item.of.func;

    wasm_functype_t* type = wasmtime_func_type(impl->context, &func);
    const wasm_valtype_vec_t* paramTypes = wasm_functype_params(type);
    const wasm_valtype_vec_t* resultTypes = wasm_functype_results(type);
    if (paramTypes->size != args.size()) {
        const size_t expected = paramTypes->size;
        wasm_functype_delete(type);
        wasmtime_extern_delete(&item);
        return WasmStatus::Failure(exportName + " takes " + std::to_string(expected) + " arguments, not "
                                   + std::to_string(args.size()));
    }
    std::vector<wasmtime_val_t> in(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        switch (wasm_valtype_kind(paramTypes->data[i])) {
            case WASM_I32: in[i].kind = WASMTIME_I32; in[i].of.i32 = args[i].i32; break;
            case WASM_I64: in[i].kind = WASMTIME_I64; in[i].of.i64 = args[i].i64; break;
            case WASM_F32: in[i].kind = WASMTIME_F32; in[i].of.f32 = args[i].f32; break;
            case WASM_F64: in[i].kind = WASMTIME_F64; in[i].of.f64 = args[i].f64; break;
            default:
                wasm_functype_delete(type);
                wasmtime_extern_delete(&item);
                return WasmStatus::Failure(exportName + " takes a reference or vector argument");
        }
    }
    std::vector<wasmtime_val_t> out(resultTypes->size);
    wasm_functype_delete(type);

    wasmtime_context_set_epoch_deadline(impl->context, DeadlineTicks(impl->limits));
    wasm_trap_t* trap = nullptr;
    wasmtime_error_t* error = wasmtime_func_call(impl->context, &func, in.data(), in.size(), out.data(), out.size(), &trap);
    wasmtime_extern_delete(&item);
    if (error || trap) return impl->Fail(error, trap, exportName.c_str());

    if (results) {
        results->assign(out.size(), WasmValue{});
        for (size_t i = 0; i < out.size(); ++i) {
            switch (out[i].kind) {
                case WASMTIME_I32: (*results)[i].i32 = out[i].of.i32; break;
                case WASMTIME_I64: (*results)[i].i64 = out[i].of.i64; break;
                case WASMTIME_F32: (*results)[i].f32 = out[i].of.f32; break;
                case WASMTIME_F64: (*results)[i].f64 = out[i].of.f64; break;
                default: break;
            }
        }
    }
    for (wasmtime_val_t& value : out) wasmtime_val_unroot(&value);
    return WasmStatus::Success();
}

bool UltraCanvasWasmInstance::IsUsable() const { return impl->usable; }

bool UltraCanvasWasm_IsAvailable() { return true; }

std::string UltraCanvasWasm_EngineDescription() { return std::string("wasmtime ") + WASMTIME_VERSION; }

WasmStatus UltraCanvasWasm_WatToWasm(std::string_view wat, std::vector<uint8_t>& out) {
    wasm_byte_vec_t bytes;
    if (wasmtime_error_t* error = wasmtime_wat2wasm(wat.data(), wat.size(), &bytes))
        return WasmStatus::Failure(TakeMessage(error));
    out.assign(reinterpret_cast<const uint8_t*>(bytes.data), reinterpret_cast<const uint8_t*>(bytes.data) + bytes.size);
    wasm_byte_vec_delete(&bytes);
    return WasmStatus::Success();
}

#else // !ULTRACANVAS_HAS_WASMTIME

namespace {
const char* const kNoEngine =
    "this build has no WebAssembly engine (configure with ULTRACANVAS_ENABLE_WASM_HOST=ON "
    "on a platform with a wasmtime C API)";
}

struct UltraCanvasWasmInstance::Impl {};

UltraCanvasWasmInstance::UltraCanvasWasmInstance() : impl(std::make_unique<Impl>()) {}
UltraCanvasWasmInstance::~UltraCanvasWasmInstance() = default;

std::unique_ptr<UltraCanvasWasmInstance> UltraCanvasWasmInstance::Create(const std::vector<uint8_t>&,
                                                                        const std::vector<WasmImport>&,
                                                                        const WasmLimits&, WasmStatus& status) {
    status = WasmStatus::Failure(kNoEngine);
    return nullptr;
}

bool UltraCanvasWasmInstance::HasFunction(const std::string&) const { return false; }

WasmStatus UltraCanvasWasmInstance::Call(const std::string&, const std::vector<WasmValue>&, std::vector<WasmValue>*) {
    return WasmStatus::Failure(kNoEngine);
}

bool UltraCanvasWasmInstance::IsUsable() const { return false; }

bool UltraCanvasWasm_IsAvailable() { return false; }

std::string UltraCanvasWasm_EngineDescription() { return kNoEngine; }

WasmStatus UltraCanvasWasm_WatToWasm(std::string_view, std::vector<uint8_t>&) {
    return WasmStatus::Failure(kNoEngine);
}

#endif // ULTRACANVAS_HAS_WASMTIME

} // namespace UltraCanvas
