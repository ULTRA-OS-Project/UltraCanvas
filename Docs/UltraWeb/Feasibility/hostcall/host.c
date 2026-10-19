// Host side of the call-cost test: two functions a guest can import,
// shaped like an element ABI (a handle, a property key, a string value).
#include <string.h>
#include <stdint.h>
#include "wasm_export.h"
static volatile int sink;
static char last[256];
static int uc_noop_wrapper(wasm_exec_env_t env, int handle, int key) { sink += handle ^ key; return sink; }
static int uc_set_prop_wrapper(wasm_exec_env_t env, int handle, int key, const char* value, uint32_t len) {
    if (len > sizeof(last)) len = sizeof(last);
    memcpy(last, value, len);           // the host copies the value out of guest memory
    sink += handle + key + (int)len;
    return 0;
}
static NativeSymbol syms[] = {
    { "uc_noop", uc_noop_wrapper, "(ii)i", NULL },
    { "uc_set_prop", uc_set_prop_wrapper, "(ii*~)i", NULL },
};
uint32_t get_native_lib(char** module, NativeSymbol** symbols) { *module = "env"; *symbols = syms; return 2; }
