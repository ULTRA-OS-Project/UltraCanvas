# UltraWeb

**Status:** Phase 1 started — UltraWeb 0.1.0 runs WebAssembly apps on Linux.
**Last Modified:** 2026-10-06
**Author:** UltraCanvas Framework / ULTRA OS

UltraWeb is the ULTRA OS browser. This first version runs **WebAssembly apps
that build their UI out of UltraCanvas elements**: a URL or a file names a
`.wasm` module, UltraWeb runs it in a sandbox, and the module creates real
UltraCanvas elements — containers, labels, buttons, text inputs, checkboxes —
through the *element ABI*, holding only handles to them. HTML pages, and
React or Angular apps through a JavaScript engine compiled to WebAssembly,
come in later phases: the plan, the measurements behind it and the
architecture are in [`UltraWebProposal.md`](UltraWebProposal.md).

## Running it

```bash
UltraWeb                      # opens about:demo
UltraWeb ~/apps/counter.wasm  # a local app (binary .wasm or WebAssembly text)
UltraWeb https://example.org/app.wasm
UltraWeb --check about:demo   # start an app without a window, print what it built
```

The address bar takes the same: `about:demo`, a path, a `file://` URL, or an
`http(s)://` address (`example.org/app.wasm` is read as `https://`). An app
fetched over the network comes through UltraNet with TLS verification on;
an HTML page is refused with a message until the reader exists. The status
bar shows what is running, how many elements it holds and the engine.

`--check` loads local addresses only (about:, paths, `file://`), starts the
app with an invisible root, and exits 0 when it started — useful for an app's
own build checks.

### The sandbox

| What an app can do | How |
|---|---|
| Build and change its UI | the element ABI below; every handle, pointer and length is checked |
| Hear what the user does | `uc_listen` + its `uc_event` export (click, text change, Enter, toggle) |
| Log | `uc_log` (stderr in this version) |
| Clocks, random numbers, stdout/stderr | WASI preview 1 |

What it cannot do: open files (WASI has no preopened directories), read the
environment or arguments, reach the network (an app `fetch` is the next
step), or touch an element it did not create. Limits per app: **2 s per call**
into it (a call that runs longer is interrupted and the app stopped),
**256 MB** of memory (a `memory.grow` past it fails), **20,000 elements**,
**1 MB** per text property. An app that traps, or is stopped, is torn down
and its error shown in its place; it cannot crash or freeze the browser.

## Writing an app

The contract is one C header, [`UltraWeb/guest/ultraweb.h`](../../UltraWeb/guest/ultraweb.h):
element kinds, property and event ids, error codes, and the imports (module
`ultracanvas`). The host includes the same header, so the numbers cannot
disagree. The model:

- The host owns the elements; the app holds `uint32_t` handles.
  `UC_ROOT_HANDLE` (1) is the app's area of the window, a flex column.
- The app exports `uc_main()`, which builds the UI and returns, and — if it
  listens to anything — `uc_event(handle, event, detail)`. It is a WASI
  *reactor*: no `main`, its state lives between calls.
- Strings cross as (pointer, length) in the app's own memory, UTF-8.
- Every import returns `UC_OK` or a negative `UC_ERR_*`; nothing an app
  passes can crash the host.
- Changes the app makes do not raise events back into it.

A counter in C:

```c
#include "ultraweb.h"
#include <stdio.h>
#include <string.h>

static uint32_t button, label;
static int clicks;

static uint32_t Add(uint32_t parent, const char* kind) {
    uint32_t h = uc_create(kind, strlen(kind));
    uc_insert(parent, h, UC_NO_HANDLE);
    return h;
}
static void SetText(uint32_t h, const char* text) { uc_set_text(h, UC_PROP_TEXT, text, strlen(text)); }

UC_EXPORT(uc_main) void uc_main(void) {
    uc_set_number(UC_ROOT_HANDLE, UC_PROP_PADDING, 16);
    uint32_t row = Add(UC_ROOT_HANDLE, UC_KIND_CONTAINER);
    uc_set_number(row, UC_PROP_DIRECTION, UC_DIRECTION_ROW);
    uc_set_number(row, UC_PROP_GAP, 12);
    button = Add(row, UC_KIND_BUTTON);
    SetText(button, "Count");
    uc_listen(button, UC_EVENT_CLICK);
    label = Add(row, UC_KIND_LABEL);
    SetText(label, "Clicked 0 times");
}

UC_EXPORT(uc_event) void uc_event(uint32_t handle, uint32_t event, int32_t detail) {
    if (handle == button && event == UC_EVENT_CLICK) {
        char text[64];
        snprintf(text, sizeof text, "Clicked %d times", ++clicks);
        SetText(label, text);
    }
}
```

Build it with [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) (version 34
was used to verify everything here):

```bash
<wasi-sdk>/bin/clang --target=wasm32-wasip1 -O2 -mexec-model=reactor \
    -I UltraWeb/guest counter.c -o counter.wasm
UltraWeb --check counter.wasm && UltraWeb counter.wasm
```

C++ with exceptions needs the standard exception encoding, which is what
wasmtime runs:

```bash
SYSROOT=<wasi-sdk>/share/wasi-sysroot
<wasi-sdk>/bin/clang++ --target=wasm32-wasip1 --sysroot=$SYSROOT -O2 -mexec-model=reactor \
    -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false -L$SYSROOT/lib/wasm32-wasip1/eh -lunwind \
    -I UltraWeb/guest app.cpp -o app.wasm
```

An app may also be WebAssembly text: [`about:demo`](../../Apps/UltraWeb/host/generate_demo_app.py)
is one, generated so its string offsets cannot drift. An app built for a
newer ABI can export `uc_abi_version()`, returning the `UC_ABI_VERSION` it
needs; an older UltraWeb then refuses it with a message instead of failing
in the middle.

### Element ABI v1 at a glance

| Kind (`uc_create`) | Text properties | Number properties | Events |
|---|---|---|---|
| `Container` (flex column; row with `UC_PROP_DIRECTION`) | — | `DIRECTION`, `GAP`, `PADDING`, `BACKGROUND` | — |
| `Label` (wraps at its width) | `TEXT` | `FONT_SIZE`, `TEXT_COLOR`, `BACKGROUND` | — |
| `Button` | `TEXT` | `FONT_SIZE` | `CLICK` |
| `TextInput` (240 × 28 unless sized) | `TEXT`, `PLACEHOLDER` | `FONT_SIZE` | `CHANGE`, `SUBMIT` |
| `Checkbox` | `TEXT` | `CHECKED` | `TOGGLE` (detail 1 / 0) |
| every kind | | `ENABLED`, `VISIBLE`, `WIDTH`, `HEIGHT` (px, 0 = automatic), `GROW` | |

The root takes the container properties too. `uc_get_number` of `WIDTH` /
`HEIGHT` answers the laid-out size, and `uc_bounds` the box in the app area
— both as of the last layout pass, so not yet right after a change (a
synchronous layout flush is Phase 4 of the plan). Colours are `0xRRGGBBAA`.

## How it is built

```
Apps/UltraWeb/
├── main.cpp                  window, --check, --version
├── ui/UltraWebWindow.*       address bar, app area (UC_ROOT_HANDLE), error view, status bar
└── host/
    ├── UltraWebGuest.*       the element bridge: handle table, the imports, events → uc_event
    ├── UltraWebLoader.*      address → module bytes (about:, file, file://, http(s) via UltraNet)
    ├── UltraWebDemoApp.h     about:demo (generated by generate_demo_app.py)
    └── generate_demo_app.py
UltraWeb/guest/ultraweb.h     the ABI, shared by host and guests
UltraCanvas/{include,core}/WasmHost/   the WebAssembly runner (Masterfile_modules.md §17)
```

- **WasmHost** wraps wasmtime 49.0.2 (its prebuilt C API, downloaded and
  hash-checked by `cmake/UltraCanvasWasmtime.cmake`, or
  `-DULTRACANVAS_WASMTIME_DIR=<unpacked c-api>` offline). On by default on
  Linux; on macOS and Windows it is opt-in
  (`-DULTRACANVAS_ENABLE_WASM_HOST=ON`) and not yet built in CI, and without an
  engine UltraWeb starts and says it cannot run apps. The binary is about
  38 MB, most of it the statically linked engine.
- A guest runs on the UI thread. A failure after start is reported on a
  later turn, never from inside the element callback that ran the guest, and
  an element a guest releases while its own callback runs (a button removing
  itself) is kept alive until that call has unwound.
- Tests: `Tests/WasmHostTest.cpp` (the runner) and
  `Tests/UltraWebGuestTest.cpp` (the bridge and loader, against inline guests
  and `about:demo`); neither needs a display or a wasm toolchain.

## Not yet

In the order of the plan (Phase 1 of `UltraWebProposal.md` §10, then the
later phases): `uc_fetch`, timers and storage for apps; manifests,
`<link rel="ultraweb-app">` and `.ucpkg` packages; a compiled-module cache;
per-app permissions; C++ element proxies generated from the framework
headers; the macOS, Windows and packaging builds; then the HTML reader, the
live DOM, and JavaScript.
