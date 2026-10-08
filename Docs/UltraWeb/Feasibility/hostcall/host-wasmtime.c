// Host side of the call-cost test for wasmtime: the same two imports as
// host.c (WAMR), defined through the wasmtime C API, then guest.wasm's
// _start. uc_set_prop finds the guest's memory the way the element bridge
// does: once, after instantiation, then wasmtime_memory_data per call.
// UC_UNCHECKED=1 registers the imports through the unchecked API (raw
// argument slots, no wasmtime_val_t conversion) instead.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasi.h>
#include <wasmtime.h>

static wasmtime_memory_t g_memory;
static volatile int g_sink;
static char g_last[256];

static wasm_trap_t* uc_noop(void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args, size_t nargs,
                            wasmtime_val_t* results, size_t nresults) {
    g_sink += args[0].of.i32 ^ args[1].of.i32;
    results[0].kind = WASMTIME_I32; results[0].of.i32 = g_sink;
    return NULL;
}

static wasm_trap_t* uc_set_prop(void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args, size_t nargs,
                                wasmtime_val_t* results, size_t nresults) {
    wasmtime_context_t* context = wasmtime_caller_context(caller);
    uint8_t* base = wasmtime_memory_data(context, &g_memory);
    size_t size = wasmtime_memory_data_size(context, &g_memory);
    uint32_t ptr = (uint32_t)args[2].of.i32, len = (uint32_t)args[3].of.i32;
    if ((uint64_t)ptr + len > size) return wasmtime_trap_new_code(WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS);
    if (len > sizeof g_last) len = sizeof g_last;
    memcpy(g_last, base + ptr, len);
    g_sink += args[0].of.i32 + args[1].of.i32 + (int)len;
    results[0].kind = WASMTIME_I32; results[0].of.i32 = 0;
    return NULL;
}

static wasm_trap_t* uc_noop_raw(void* env, wasmtime_caller_t* caller, wasmtime_val_raw_t* io, size_t n) {
    g_sink += io[0].i32 ^ io[1].i32;
    io[0].i32 = g_sink;
    return NULL;
}

static wasm_trap_t* uc_set_prop_raw(void* env, wasmtime_caller_t* caller, wasmtime_val_raw_t* io, size_t n) {
    wasmtime_context_t* context = wasmtime_caller_context(caller);
    uint8_t* base = wasmtime_memory_data(context, &g_memory);
    size_t size = wasmtime_memory_data_size(context, &g_memory);
    uint32_t ptr = (uint32_t)io[2].i32, len = (uint32_t)io[3].i32;
    if ((uint64_t)ptr + len > size) return wasmtime_trap_new_code(WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS);
    if (len > sizeof g_last) len = sizeof g_last;
    memcpy(g_last, base + ptr, len);
    g_sink += io[0].i32 + io[1].i32 + (int)len;
    io[0].i32 = 0;
    return NULL;
}

static void die(const char* what, wasmtime_error_t* error, wasm_trap_t* trap) {
    wasm_byte_vec_t msg;
    if (error) wasmtime_error_message(error, &msg); else wasm_trap_message(trap, &msg);
    fprintf(stderr, "%s: %.*s\n", what, (int)msg.size, msg.data);
    exit(1);
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "guest.wasm";
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* bytes = malloc(n);
    if (fread(bytes, 1, n, f) != (size_t)n) { fprintf(stderr, "%s: short read\n", path); return 1; }
    fclose(f);

    wasm_engine_t* engine = wasm_engine_new();
    wasmtime_store_t* store = wasmtime_store_new(engine, NULL, NULL);
    wasmtime_context_t* context = wasmtime_store_context(store);
    wasmtime_module_t* module = NULL;
    wasmtime_error_t* error = wasmtime_module_new(engine, bytes, n, &module);
    if (error) die("compile", error, NULL);

    wasmtime_linker_t* linker = wasmtime_linker_new(engine);
    if ((error = wasmtime_linker_define_wasi(linker))) die("wasi", error, NULL);
    wasm_valtype_t* p2[2] = { wasm_valtype_new_i32(), wasm_valtype_new_i32() };
    wasm_valtype_t* p4[4] = { wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32() };
    wasm_valtype_t* r1a[1] = { wasm_valtype_new_i32() };
    wasm_valtype_t* r1b[1] = { wasm_valtype_new_i32() };
    wasm_valtype_vec_t params2, params4, res1a, res1b;
    wasm_valtype_vec_new(&params2, 2, p2); wasm_valtype_vec_new(&params4, 4, p4);
    wasm_valtype_vec_new(&res1a, 1, r1a); wasm_valtype_vec_new(&res1b, 1, r1b);
    wasm_functype_t* noopType = wasm_functype_new(&params2, &res1a);
    wasm_functype_t* setType = wasm_functype_new(&params4, &res1b);
    const char* unchecked = getenv("UC_UNCHECKED");
    if (unchecked && *unchecked == '1') {
        if ((error = wasmtime_linker_define_func_unchecked(linker, "env", 3, "uc_noop", 7, noopType, uc_noop_raw, NULL, NULL))) die("define", error, NULL);
        if ((error = wasmtime_linker_define_func_unchecked(linker, "env", 3, "uc_set_prop", 11, setType, uc_set_prop_raw, NULL, NULL))) die("define", error, NULL);
    } else {
        if ((error = wasmtime_linker_define_func(linker, "env", 3, "uc_noop", 7, noopType, uc_noop, NULL, NULL))) die("define", error, NULL);
        if ((error = wasmtime_linker_define_func(linker, "env", 3, "uc_set_prop", 11, setType, uc_set_prop, NULL, NULL))) die("define", error, NULL);
    }

    wasi_config_t* wasi = wasi_config_new();
    wasi_config_set_argv(wasi, argc - 1, (const char**)argv + 1);
    wasi_config_inherit_stdout(wasi);
    wasi_config_inherit_stderr(wasi);
    if ((error = wasmtime_context_set_wasi(context, wasi))) die("wasi config", error, NULL);

    wasmtime_instance_t instance;
    wasm_trap_t* trap = NULL;
    if ((error = wasmtime_linker_instantiate(linker, context, module, &instance, &trap)) || trap) die("instantiate", error, trap);
    wasmtime_extern_t item;
    if (!wasmtime_instance_export_get(context, &instance, "memory", 6, &item) || item.kind != WASMTIME_EXTERN_MEMORY) { fprintf(stderr, "no memory\n"); return 1; }
    g_memory = item.of.memory;
    if (!wasmtime_instance_export_get(context, &instance, "_start", 6, &item) || item.kind != WASMTIME_EXTERN_FUNC) { fprintf(stderr, "no _start\n"); return 1; }
    error = wasmtime_func_call(context, &item.of.func, NULL, 0, NULL, 0, &trap);
    if (error) {
        // proc_exit(0) arrives as an error carrying the exit status.
        int status = 0;
        if (!wasmtime_error_exit_status(error, &status)) die("run", error, NULL);
    } else if (trap) die("run", NULL, trap);
    return 0;
}
