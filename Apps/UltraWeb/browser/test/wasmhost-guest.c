/*
 * Apps/UltraWeb/browser/test/wasmhost-guest.c
 * The guest wasmhost-browser-test.cpp drives: one export per thing the
 * browser engine of UltraCanvasWasmHost must get right. Freestanding C,
 * built by the clang inside emsdk (see ../CMakeLists.txt).
 *
 * Built twice: plain, and with -DUNKNOWN_IMPORT, which adds an import no
 * host offers.
 */
#include <stdint.h>

#define EXPORT(name) __attribute__((export_name(#name)))
#define IMPORT(module, name) __attribute__((import_module(#module), import_name(#name)))

IMPORT(env, twice) int32_t twice(int32_t);
IMPORT(env, record) void record(const char* text, uint32_t length);
IMPORT(env, fill) int32_t fill(char* out, uint32_t capacity);
IMPORT(env, fail) void fail(void);
IMPORT(env, wide) int64_t host_wide(int64_t);
IMPORT(env, scale) double scale(double, float);
IMPORT(wasi_snapshot_preview1, fd_write) int32_t fd_write(int32_t fd, const void* iovs, uint32_t count, uint32_t* written);
IMPORT(wasi_snapshot_preview1, proc_exit) void proc_exit(int32_t code);
#ifdef UNKNOWN_IMPORT
IMPORT(env, missing) void missing(void);
EXPORT(use_missing) void use_missing(void) { missing(); }   /* or the linker drops the import */
#endif

static int32_t initialized;
static char buffer[64];

EXPORT(_initialize) void initialize(void) { initialized = 7; }
EXPORT(initialized_value) int32_t initialized_value(void) { return initialized; }

EXPORT(add) int32_t add(int32_t a, int32_t b) { return a + b; }
EXPORT(half) double half(double v) { return v * 0.5; }
EXPORT(wide) int64_t wide(int64_t v) { return host_wide(v) + 1; }
EXPORT(scaled) double scaled(double v) { return scale(v, 0.25f); }

EXPORT(call_twice) int32_t call_twice(int32_t v) { return twice(v) + 1; }

EXPORT(send) void send(void) { record("hello host", 10); }

EXPORT(receive) int32_t receive(void) {
    int32_t n = fill(buffer, sizeof buffer);
    int32_t sum = 0;
    for (int32_t i = 0; i < n; ++i) sum += buffer[i];
    return sum;
}

EXPORT(bad_pointer) void bad_pointer(void) { record((const char*)0x7ffffff0, 1000); }

EXPORT(host_trap) int32_t host_trap(void) {
    fail();
    return 1;
}

EXPORT(guest_trap) void guest_trap(void) { __builtin_trap(); }

EXPORT(print) void print(void) {
    static const char line[] = "printed by the guest\n";
    struct { const char* base; uint32_t length; } iov = { line, sizeof line - 1 };
    uint32_t written = 0;
    fd_write(1, &iov, 1, &written);
}

EXPORT(quit) void quit(void) { proc_exit(3); }

EXPORT(grow) int32_t grow(int32_t pages) { return (int32_t)__builtin_wasm_memory_grow(0, pages); }
