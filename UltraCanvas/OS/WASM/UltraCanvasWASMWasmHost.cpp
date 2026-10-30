// OS/WASM/UltraCanvasWASMWasmHost.cpp
// The WebAssembly host (include/WasmHost/UltraCanvasWasmHost.h) inside a
// browser: a guest module runs on the browser's own WebAssembly engine,
// beside the Emscripten program that hosts it, instead of on wasmtime. The
// same callers - UltraWeb's element ABI (Apps/UltraWeb/host/UltraWebGuest)
// - then run unchanged in an ordinary browser page
// (Docs/UltraWeb/UltraWebInTheBrowser.md).
//
// How a call travels: Create() compiles the module with
// `new WebAssembly.Module` (synchronous; Chromium allows that on the main
// thread up to 8 MB), gives every function import a JavaScript thunk and
// instantiates it. A thunk writes its arguments into a scratch area of this
// program's heap and calls uc_wasmhost_dispatch, which runs the WasmImport's
// function with a WasmCaller over the guest's exported memory. A trap is
// thrown back into the guest as a JavaScript error and comes out of the
// Call() that entered it. Signatures are read from the module's own type,
// import and export sections, since a browser does not expose them to
// script.
//
// What the browser cannot give: WasmLimits::callTimeoutMs is not enforced -
// a guest that never returns hangs the page until the browser offers to
// stop it - and the memory cap is checked after each call rather than at
// each memory.grow. WebAssembly text is not accepted: serve the binary.
// WASI preview 1 is the same small set wasmtime grants (clocks, random,
// stdout / stderr to the console, no files, no environment).
//
// Version: 0.1.0 - first version
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework / ULTRA OS

#include "WasmHost/UltraCanvasWasmHost.h"

#include <emscripten.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>

// ===== JAVASCRIPT SIDE =====
// One registry per page, Module.ucWasmHost: id -> { instance, exports,
// memory, cap, io }. `io` is the scratch area in this program's heap that
// carries arguments and results (16 slots of 8 bytes).

EM_JS(int, uc_wasmhost_js_instantiate, (int id, const uint8_t* bytes, int length, const char* importList,
                                        uint8_t* io, double memoryCap), {
    const host = Module.ucWasmHost || (Module.ucWasmHost = { instances: new Map(), lastError: "" });
    const state = { instance: null, memory: null, cap: memoryCap, io: io, exited: null };
    const view = () => new DataView(HEAPU8.buffer);
    const writeValue = (at, type, value) => {
        const v = view();
        if (type === "i") v.setInt32(at, value, true);
        else if (type === "I") v.setBigInt64(at, BigInt(value), true);
        else if (type === "f") v.setFloat32(at, value, true);
        else if (type === "d") v.setFloat64(at, value, true);
    };
    const readValue = (at, type) => {
        const v = view();
        if (type === "i") return v.getInt32(at, true);
        if (type === "I") return v.getBigInt64(at, true);
        if (type === "f") return v.getFloat32(at, true);
        return v.getFloat64(at, true);
    };
    state.writeValue = writeValue;
    state.readValue = readValue;

    // WASI preview 1, the part wasmtime grants a guest here: no arguments,
    // no environment, no files; stdout and stderr go to the console.
    const ERRNO_BADF = 8;
    const ERRNO_NOSYS = 52;
    const lines = { 1: "", 2: "" };
    const guestView = () => new DataView(state.memory.buffer);
    const guestBytes = () => new Uint8Array(state.memory.buffer);
    const decoder = new TextDecoder();
    const wasi = {
        args_sizes_get: (argc, size) => { guestView().setUint32(argc, 0, true); guestView().setUint32(size, 0, true); return 0; },
        args_get: () => 0,
        environ_sizes_get: (count, size) => { guestView().setUint32(count, 0, true); guestView().setUint32(size, 0, true); return 0; },
        environ_get: () => 0,
        clock_res_get: (clock, out) => { guestView().setBigUint64(out, 1000n, true); return 0; },
        clock_time_get: (clock, precision, out) => {
            const ns = clock === 0 ? BigInt(Date.now()) * 1000000n
                                   : BigInt(Math.round(performance.now() * 1000000));
            guestView().setBigUint64(out, ns, true);
            return 0;
        },
        random_get: (at, length) => {
            const bytes = guestBytes();
            for (let done = 0; done < length; done += 65536) {
                crypto.getRandomValues(bytes.subarray(at + done, at + Math.min(length, done + 65536)));
            }
            return 0;
        },
        fd_write: (fd, iovs, count, written) => {
            if (fd !== 1 && fd !== 2) return ERRNO_BADF;
            const v = guestView();
            let total = 0;
            for (let k = 0; k < count; ++k) {
                const ptr = v.getUint32(iovs + k * 8, true);
                const len = v.getUint32(iovs + k * 8 + 4, true);
                lines[fd] += decoder.decode(guestBytes().slice(ptr, ptr + len));
                total += len;
            }
            let newline;
            while ((newline = lines[fd].indexOf("\n")) >= 0) {
                (fd === 1 ? console.log : console.warn)(lines[fd].slice(0, newline));
                lines[fd] = lines[fd].slice(newline + 1);
            }
            v.setUint32(written, total, true);
            return 0;
        },
        fd_close: () => ERRNO_BADF,
        fd_seek: () => ERRNO_BADF,
        fd_read: () => ERRNO_BADF,
        fd_fdstat_get: (fd) => (fd <= 2 ? 0 : ERRNO_BADF),
        fd_prestat_get: () => ERRNO_BADF,
        fd_prestat_dir_name: () => ERRNO_BADF,
        sched_yield: () => 0,
        proc_exit: (code) => { state.exited = code; throw new Error("exit " + code); }
    };

    try {
        const module = new WebAssembly.Module(HEAPU8.slice(bytes, bytes + length));
        const imports = {};
        for (const line of UTF8ToString(importList).split("\n")) {
            if (!line) continue;
            const [moduleName, name, index, params, results] = line.split("\t");
            imports[moduleName] = imports[moduleName] || {};
            if (index === "wasi") {
                imports[moduleName][name] = wasi[name] || (() => ERRNO_NOSYS);
                continue;
            }
            const which = Number(index);
            imports[moduleName][name] = (...args) => {
                for (let k = 0; k < params.length; ++k) writeValue(io + k * 8, params[k], args[k]);
                if (_uc_wasmhost_dispatch(id, which, io)) {
                    const text = UTF8ToString(_uc_wasmhost_trap_text(id));
                    throw new WebAssembly.RuntimeError(text);
                }
                if (results.length === 0) return undefined;
                if (results.length === 1) return readValue(io, results[0]);
                return Array.from(results, (type, k) => readValue(io + k * 8, type));
            };
        }
        state.instance = new WebAssembly.Instance(module, imports);
        state.exports = state.instance.exports;
        if (state.exports.memory instanceof WebAssembly.Memory) state.memory = state.exports.memory;
        host.instances.set(id, state);
        return 1;
    } catch (e) {
        host.lastError = String(e && e.message ? e.message : e);
        return 0;
    }
});

// 0 when the call returned, 1 for a trap, 2 for proc_exit; the message is
// lastError either way.
EM_JS(int, uc_wasmhost_js_call, (int id, const char* name, const char* params, const char* results), {
    const host = Module.ucWasmHost;
    const state = host && host.instances.get(id);
    if (!state) { host.lastError = "no such instance"; return 1; }
    const fn = state.exports[UTF8ToString(name)];
    const paramTypes = UTF8ToString(params);
    const resultTypes = UTF8ToString(results);
    const args = [];
    for (let k = 0; k < paramTypes.length; ++k) args.push(state.readValue(state.io + k * 8, paramTypes[k]));
    try {
        const value = fn(...args);
        if (resultTypes.length === 1) state.writeValue(state.io, resultTypes[0], value);
        else for (let k = 0; k < resultTypes.length; ++k) state.writeValue(state.io + k * 8, resultTypes[k], value[k]);
    } catch (e) {
        if (state.exited !== null) {
            host.lastError = "the module exited (status " + state.exited + ")";
            return 2;
        }
        host.lastError = String(e && e.message ? e.message : e);
        return 1;
    }
    if (state.memory && state.memory.buffer.byteLength > state.cap) {
        host.lastError = "it grew its memory to " + state.memory.buffer.byteLength + " bytes, over the limit of " + state.cap;
        return 1;
    }
    return 0;
});

EM_JS(int, uc_wasmhost_js_read, (int id, uint32_t offset, uint32_t length, uint8_t* out), {
    const state = Module.ucWasmHost && Module.ucWasmHost.instances.get(id);
    if (!state || !state.memory) return 0;
    offset >>>= 0;
    length >>>= 0;
    const buffer = state.memory.buffer;
    if (offset + length > buffer.byteLength) return 0;
    if (length > 0) HEAPU8.set(new Uint8Array(buffer, offset, length), out);
    return 1;
});

EM_JS(int, uc_wasmhost_js_write, (int id, uint32_t offset, const uint8_t* data, uint32_t length), {
    const state = Module.ucWasmHost && Module.ucWasmHost.instances.get(id);
    if (!state || !state.memory) return 0;
    offset >>>= 0;
    length >>>= 0;
    const buffer = state.memory.buffer;
    if (offset + length > buffer.byteLength) return 0;
    if (length > 0) new Uint8Array(buffer, offset, length).set(HEAPU8.subarray(data, data + length));
    return 1;
});

EM_JS(char*, uc_wasmhost_js_last_error, (), {
    return stringToNewUTF8(Module.ucWasmHost ? Module.ucWasmHost.lastError : "");
});

EM_JS(void, uc_wasmhost_js_forget, (int id), {
    if (Module.ucWasmHost) Module.ucWasmHost.instances.delete(id);
});

namespace UltraCanvas {

namespace {

constexpr size_t kMaxSignatureValues = 16;

// Value types as one letter each, the way the JavaScript side reads them.
char LetterOf(WasmValueType type) {
    switch (type) {
        case WasmValueType::I32: return 'i';
        case WasmValueType::I64: return 'I';
        case WasmValueType::F32: return 'f';
        case WasmValueType::F64: return 'd';
    }
    return '?';
}

std::string LettersOf(const std::vector<WasmValueType>& types) {
    std::string out;
    for (WasmValueType type : types) out += LetterOf(type);
    return out;
}

// ===== READING THE MODULE =====
// Just enough of the binary format to know each function's signature: the
// type, import, function, memory and export sections.

struct FunctionType {
    std::string params;    // letters; '?' for a type the host cannot pass
    std::string results;
};

struct ImportEntry {
    std::string module;
    std::string name;
    uint8_t kind = 0;      // 0 function, 1 table, 2 memory, 3 global, 4 tag
    uint32_t typeIndex = 0;
};

struct ModuleShape {
    std::vector<FunctionType> types;
    std::vector<ImportEntry> imports;
    std::vector<uint32_t> functionTypes;   // every function: imported first, then defined
    std::map<std::string, uint32_t> exportedFunctions;   // name -> function index
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool AtEnd() const { return at_ >= size_; }
    size_t Position() const { return at_; }
    void Seek(size_t at) { at_ = at; }
    bool Byte(uint8_t& out) {
        if (at_ >= size_) return false;
        out = data_[at_++];
        return true;
    }
    bool U32(uint32_t& out) {
        out = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            uint8_t b;
            if (!Byte(b)) return false;
            out |= uint32_t(b & 0x7f) << shift;
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    // A signed LEB128 of up to 33 bits (a heap type), value discarded.
    bool SkipS33() {
        for (int i = 0; i < 5; ++i) {
            uint8_t b;
            if (!Byte(b)) return false;
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    bool Name(std::string& out) {
        uint32_t length;
        if (!U32(length) || length > size_ - at_) return false;
        out.assign(reinterpret_cast<const char*>(data_ + at_), length);
        at_ += length;
        return true;
    }
    bool Skip(uint32_t length) {
        if (length > size_ - at_) return false;
        at_ += length;
        return true;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t at_ = 0;
};

// A value type, as a letter; '?' for the reference and vector types (a
// typed reference carries a heap type after it).
bool ValueType(Reader& r, char& out) {
    uint8_t b;
    if (!r.Byte(b)) return false;
    switch (b) {
        case 0x7f: out = 'i'; return true;
        case 0x7e: out = 'I'; return true;
        case 0x7d: out = 'f'; return true;
        case 0x7c: out = 'd'; return true;
        case 0x63: case 0x64: out = '?'; return r.SkipS33();
        default:   out = '?'; return true;
    }
}

bool Limits(Reader& r) {
    uint8_t flags;
    uint32_t value;
    if (!r.Byte(flags) || !r.U32(value)) return false;
    return !(flags & 0x01) || r.U32(value);
}

bool ReadShape(const std::vector<uint8_t>& module, ModuleShape& shape, std::string& problem) {
    Reader r(module.data(), module.size());
    if (!r.Skip(8)) { problem = "too short"; return false; }
    while (!r.AtEnd()) {
        uint8_t id;
        uint32_t size;
        if (!r.Byte(id) || !r.U32(size)) { problem = "a section header is cut off"; return false; }
        const size_t end = r.Position() + size;
        if (end > module.size()) { problem = "a section runs past the end"; return false; }
        uint32_t count = 0;
        bool ok = true;
        switch (id) {
            case 1:   // types
                ok = r.U32(count);
                for (uint32_t i = 0; ok && i < count; ++i) {
                    uint8_t form;
                    if (!r.Byte(form) || form != 0x60) {
                        problem = "it uses a type form this host cannot read (garbage collection types)";
                        return false;
                    }
                    FunctionType type;
                    uint32_t n;
                    ok = r.U32(n);
                    for (uint32_t k = 0; ok && k < n; ++k) { char c; ok = ValueType(r, c); type.params += c; }
                    ok = ok && r.U32(n);
                    for (uint32_t k = 0; ok && k < n; ++k) { char c; ok = ValueType(r, c); type.results += c; }
                    shape.types.push_back(std::move(type));
                }
                break;
            case 2:   // imports
                ok = r.U32(count);
                for (uint32_t i = 0; ok && i < count; ++i) {
                    ImportEntry entry;
                    ok = r.Name(entry.module) && r.Name(entry.name) && r.Byte(entry.kind);
                    if (!ok) break;
                    switch (entry.kind) {
                        case 0: ok = r.U32(entry.typeIndex); shape.functionTypes.push_back(entry.typeIndex); break;
                        case 1: { char c; ok = ValueType(r, c) && Limits(r); break; }
                        case 2: ok = Limits(r); break;
                        case 3: { char c; uint8_t mut; ok = ValueType(r, c) && r.Byte(mut); break; }
                        case 4: { uint8_t attribute; ok = r.Byte(attribute) && r.U32(entry.typeIndex); break; }
                        default: ok = false;
                    }
                    shape.imports.push_back(std::move(entry));
                }
                break;
            case 3:   // functions
                ok = r.U32(count);
                for (uint32_t i = 0; ok && i < count; ++i) {
                    uint32_t typeIndex;
                    ok = r.U32(typeIndex);
                    shape.functionTypes.push_back(typeIndex);
                }
                break;
            case 7:   // exports
                ok = r.U32(count);
                for (uint32_t i = 0; ok && i < count; ++i) {
                    std::string name;
                    uint8_t kind;
                    uint32_t index;
                    ok = r.Name(name) && r.Byte(kind) && r.U32(index);
                    if (ok && kind == 0) shape.exportedFunctions[name] = index;
                }
                break;
            default:
                break;
        }
        if (!ok) { problem = "section " + std::to_string(id) + " is malformed"; return false; }
        r.Seek(end);
    }
    for (uint32_t typeIndex : shape.functionTypes) {
        if (typeIndex >= shape.types.size()) { problem = "a function names a type that does not exist"; return false; }
    }
    return true;
}

std::string LastJsError() {
    char* text = uc_wasmhost_js_last_error();
    std::string message = text ? text : "";
    free(text);
    return message;
}

// What the dispatcher needs of an instance, reachable by id from the
// JavaScript thunks.
struct HostState {
    std::vector<WasmImport> imports;
    std::string trapText;
};

std::map<int, HostState*>& Registry() {
    static std::map<int, HostState*> registry;
    return registry;
}

class BrowserCaller : public WasmCaller {
public:
    explicit BrowserCaller(int id) : id_(id) {}
    bool ReadMemory(uint32_t offset, uint32_t length, std::string& out) const override {
        out.assign(length, '\0');
        return uc_wasmhost_js_read(id_, offset, length, reinterpret_cast<uint8_t*>(out.data())) != 0;
    }
    bool WriteMemory(uint32_t offset, const void* data, uint32_t length) override {
        return uc_wasmhost_js_write(id_, offset, static_cast<const uint8_t*>(data), length) != 0;
    }
    void Trap(const std::string& message) override {
        trapped = true;
        trapMessage = message;
    }
    bool trapped = false;
    std::string trapMessage;

private:
    int id_;
};

} // namespace

// ===== THE INSTANCE =====

struct UltraCanvasWasmInstance::Impl {
    ~Impl() {
        if (id == 0) return;
        uc_wasmhost_js_forget(id);
        Registry().erase(id);
    }

    int id = 0;
    HostState host;
    ModuleShape shape;
    WasmLimits limits;
    bool usable = false;
    alignas(8) uint8_t io[kMaxSignatureValues * 8] = {};
};

} // namespace UltraCanvas

// Called by the import thunks (see uc_wasmhost_js_instantiate): runs import
// `index` of instance `id` with the arguments in `io`, and leaves its results
// there. 1 when it trapped; the message is uc_wasmhost_trap_text's.
extern "C" EMSCRIPTEN_KEEPALIVE int uc_wasmhost_dispatch(int id, int index, uint8_t* io) {
    using namespace UltraCanvas;
    auto at = Registry().find(id);
    if (at == Registry().end()) return 1;
    HostState& state = *at->second;
    const WasmImport& import = state.imports[size_t(index)];
    WasmValue args[kMaxSignatureValues];
    WasmValue results[kMaxSignatureValues];
    std::memset(results, 0, sizeof(results));
    // Arguments out first: the import may call into the guest, which reuses io.
    for (size_t i = 0; i < import.params.size(); ++i) std::memcpy(&args[i], io + i * 8, 8);
    BrowserCaller view(id);
    try {
        import.function(view, args, results);
    } catch (const std::exception& e) {
        view.Trap(std::string("host function ") + import.name + " threw: " + e.what());
    } catch (...) {
        view.Trap(std::string("host function ") + import.name + " threw");
    }
    if (view.trapped) {
        state.trapText = view.trapMessage;
        return 1;
    }
    for (size_t i = 0; i < import.results.size(); ++i) {
        switch (import.results[i]) {
            case WasmValueType::I32: std::memcpy(io + i * 8, &results[i].i32, 4); break;
            case WasmValueType::I64: std::memcpy(io + i * 8, &results[i].i64, 8); break;
            case WasmValueType::F32: std::memcpy(io + i * 8, &results[i].f32, 4); break;
            case WasmValueType::F64: std::memcpy(io + i * 8, &results[i].f64, 8); break;
        }
    }
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* uc_wasmhost_trap_text(int id) {
    auto at = UltraCanvas::Registry().find(id);
    return at == UltraCanvas::Registry().end() ? "" : at->second->trapText.c_str();
}

namespace UltraCanvas {

UltraCanvasWasmInstance::UltraCanvasWasmInstance() : impl(std::make_unique<Impl>()) {}
UltraCanvasWasmInstance::~UltraCanvasWasmInstance() = default;

std::unique_ptr<UltraCanvasWasmInstance> UltraCanvasWasmInstance::Create(const std::vector<uint8_t>& module,
                                                                        const std::vector<WasmImport>& imports,
                                                                        const WasmLimits& limits,
                                                                        WasmStatus& status) {
    if (module.size() < 8 || module[0] != 0x00 || module[1] != 'a' || module[2] != 's' || module[3] != 'm') {
        status = WasmStatus::Failure("not a WebAssembly module (in a browser the binary .wasm is needed; "
                                     "WebAssembly text runs only on wasmtime)");
        return nullptr;
    }
    for (const WasmImport& import : imports) {
        if (import.params.size() > kMaxSignatureValues || import.results.size() > kMaxSignatureValues || !import.function) {
            status = WasmStatus::Failure("import " + import.module + "." + import.name + " has no function or too many values");
            return nullptr;
        }
    }

    std::unique_ptr<UltraCanvasWasmInstance> self(new UltraCanvasWasmInstance());
    Impl& impl = *self->impl;
    impl.host.imports = imports;
    impl.limits = limits;
    std::string problem;
    if (!ReadShape(module, impl.shape, problem)) {
        status = WasmStatus::Failure("the module does not compile: " + problem);
        return nullptr;
    }

    // Every function import must be one the caller offers, with its exact
    // signature, or WASI; the same rule wasmtime's linker applies.
    std::string importList;
    for (const ImportEntry& entry : impl.shape.imports) {
        const std::string what = entry.module + "." + entry.name;
        if (entry.kind != 0) {
            status = WasmStatus::Failure("the module does not start: it imports " + what
                                         + ", which is not a function; this host provides functions only");
            return nullptr;
        }
        if (entry.module == "wasi_snapshot_preview1") {
            importList += entry.module + "\t" + entry.name + "\twasi\t\t\n";
            continue;
        }
        const FunctionType& type = impl.shape.types[entry.typeIndex];
        size_t index = 0;
        while (index < imports.size() && !(imports[index].module == entry.module && imports[index].name == entry.name)) ++index;
        if (index == imports.size()) {
            status = WasmStatus::Failure("the module does not start: unknown import " + what);
            return nullptr;
        }
        const std::string params = LettersOf(imports[index].params), results = LettersOf(imports[index].results);
        if (params != type.params || results != type.results) {
            status = WasmStatus::Failure("the module does not start: import " + what + " has the wrong signature");
            return nullptr;
        }
        importList += entry.module + "\t" + entry.name + "\t" + std::to_string(index) + "\t" + params + "\t" + results + "\n";
    }

    static int nextId = 1;
    impl.id = nextId++;
    Registry()[impl.id] = &impl.host;
    const double cap = limits.memoryBytes > (1ull << 53) ? 9007199254740992.0 : double(limits.memoryBytes);
    if (!uc_wasmhost_js_instantiate(impl.id, module.data(), int(module.size()), importList.c_str(), impl.io, cap)) {
        status = WasmStatus::Failure("the module does not start: " + LastJsError());
        status.trapped = true;
        return nullptr;
    }
    impl.usable = true;
    if (self->HasFunction("_initialize")) {
        status = self->Call("_initialize", {});
        if (!status.ok) return nullptr;
    }
    status = WasmStatus::Ok();
    return self;
}

bool UltraCanvasWasmInstance::HasFunction(const std::string& exportName) const {
    return impl->shape.exportedFunctions.count(exportName) != 0;
}

WasmStatus UltraCanvasWasmInstance::Call(const std::string& exportName, const std::vector<WasmValue>& args,
                                         std::vector<WasmValue>* results) {
    if (!impl->usable) return WasmStatus::Failure("the module stopped after an earlier failure");
    auto at = impl->shape.exportedFunctions.find(exportName);
    if (at == impl->shape.exportedFunctions.end()) return WasmStatus::Failure("the module has no export " + exportName);
    const FunctionType& type = impl->shape.types[impl->shape.functionTypes[at->second]];
    if (type.params.size() != args.size()) {
        return WasmStatus::Failure(exportName + " takes " + std::to_string(type.params.size()) + " arguments, not "
                                   + std::to_string(args.size()));
    }
    if (type.params.find('?') != std::string::npos || type.results.find('?') != std::string::npos
        || type.results.size() > kMaxSignatureValues) {
        return WasmStatus::Failure(exportName + " takes or returns a reference or vector value");
    }
    for (size_t i = 0; i < args.size(); ++i) {
        switch (type.params[i]) {
            case 'i': std::memcpy(impl->io + i * 8, &args[i].i32, 4); break;
            case 'I': std::memcpy(impl->io + i * 8, &args[i].i64, 8); break;
            case 'f': std::memcpy(impl->io + i * 8, &args[i].f32, 4); break;
            case 'd': std::memcpy(impl->io + i * 8, &args[i].f64, 8); break;
        }
    }
    const int outcome = uc_wasmhost_js_call(impl->id, exportName.c_str(), type.params.c_str(), type.results.c_str());
    if (outcome != 0) {
        impl->usable = false;
        WasmStatus status = WasmStatus::Failure(exportName + ": " + LastJsError());
        status.trapped = true;
        return status;
    }
    if (results) {
        results->assign(type.results.size(), WasmValue{});
        for (size_t i = 0; i < type.results.size(); ++i) {
            switch (type.results[i]) {
                case 'i': std::memcpy(&(*results)[i].i32, impl->io + i * 8, 4); break;
                case 'I': std::memcpy(&(*results)[i].i64, impl->io + i * 8, 8); break;
                case 'f': std::memcpy(&(*results)[i].f32, impl->io + i * 8, 4); break;
                case 'd': std::memcpy(&(*results)[i].f64, impl->io + i * 8, 8); break;
            }
        }
    }
    return WasmStatus::Ok();
}

bool UltraCanvasWasmInstance::IsUsable() const { return impl->usable; }

bool UltraCanvasWasm_IsAvailable() { return true; }

std::string UltraCanvasWasm_EngineDescription() { return "the browser's WebAssembly engine"; }

WasmStatus UltraCanvasWasm_WatToWasm(std::string_view, std::vector<uint8_t>&) {
    return WasmStatus::Failure("WebAssembly text runs only on wasmtime; in a browser serve the binary .wasm");
}

} // namespace UltraCanvas
