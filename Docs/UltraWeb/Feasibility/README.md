# UltraWeb feasibility measurements

**Last Modified:** 2026-10-06

The measurements behind §7 and §13 of
[`UltraWebProposal.md`](../UltraWebProposal.md): can unmodified React and
Angular production builds run on QuickJS, natively and compiled to
WebAssembly inside an embeddable runtime, and how fast; which browser APIs
they actually call; and what a guest-to-host call costs, which prices the
UltraCanvas element ABI a WASM application talks to. The numbers themselves
are in [`results.md`](results.md).

Nothing here is built by the repository's CMake and nothing is vendored:
`setup.sh` downloads and builds every tool at a pinned version into a
scratch directory.

## Running it

```bash
WORK=/tmp/ultraweb-feasibility ./setup.sh    # ~10 min, ~2 GB; Node >= 22.22.3
WORK=/tmp/ultraweb-feasibility ./run.sh      # ~25 min, one core
python3 summarise.py /tmp/ultraweb-feasibility/runs > results.md
```

| Pinned | Version |
|---|---|
| QuickJS-ng | 0.17.0 (`140b26d`), native and `wasm32-wasip1` |
| wasi-sdk | 34 (clang 23) |
| wasmtime | 49.0.2 (Cranelift) |
| WAMR | 2.4.5, fast JIT and fast interpreter |
| React / react-dom | 19.3.0, production build |
| Angular | 22.2.1 CLI production builds, zoneless (the v22 default) and zone.js 0.16.3 |
| linkedom | 0.18.13 — the stand-in DOM, see *Caveats* |
| Node (V8 reference) | 22.22.0 for the runs, 24 for the Angular CLI |

## What it does

- **The workload** is the [js-framework-benchmark](https://github.com/krausest/js-framework-benchmark)
  keyed table (`harness/react-app.jsx`, `angular/app.ts`): create 1,000 rows,
  replace them, update every 10th, select, swap, remove, clear, create
  10,000, append 1,000. `harness/driver.js` clicks the app's own buttons by
  dispatching DOM `click` events, flushes the framework
  (`flushSync` for React, `ApplicationRef.tick()` for zoneless Angular,
  nothing for zone.js, which must run change detection by itself), then
  checks the resulting DOM — row counts, labels, the selected class, the
  swapped ids. A run that renders the wrong thing fails; every published
  run passed every check.
- **The environment** (`harness/env.js`) is a JavaScript DOM (linkedom) plus
  host timers and a clock from the runner. The same two bundles run under
  every engine: `runner-qjs.mjs` for QuickJS (native, and the WASI build
  under wasmtime and WAMR), `runner-node.mjs` for V8 with and without its
  JIT.
- **Recording mode** wraps every DOM prototype member and records the ones
  framework code calls (not the ones linkedom calls internally), how often,
  and the time spent inside the DOM — which separates framework time from
  DOM time, and lists the API surface UltraWeb's DOM has to provide.
  `dispatchEvent` is recorded but not timed: the listeners it runs are
  framework code (with zone.js, all of Angular's change detection). The
  driver's own lookups appear in the list too and are named as such in
  the proposal's §13.3.
- **`exceptions/eh.cpp`** throws and catches two C++ exceptions; it is
  built in both WebAssembly exception encodings and run on wasmtime and on
  every WAMR mode.
- **`hostcall/`** is a guest module that calls two host imports ten
  million times each — an empty call, and a call shaped like a property set
  (handle, key, a string copied out of guest memory) — with the host side
  written twice: a WAMR native library (`host.c`) and a wasmtime embedding
  (`host-wasmtime.c`, checked and unchecked host-function APIs).

## Caveats

- **The DOM is JavaScript here and native in UltraWeb.** linkedom runs inside
  the measured engine, so its cost is slowed by the engine too; recording
  mode shows how much of each operation it takes. In UltraWeb each DOM call
  becomes a call into C++ (priced by `hostcall/`), so the time outside the
  DOM is the better predictor. React's 10,000-row case grows faster
  than linearly on every engine, V8 included, while Angular's does not;
  how much of that is linkedom is still to be profiled.
- **No layout or paint is measured.** The numbers are script time only;
  UltraWeb adds its own style, layout and paint on top, which is the native
  work described in §7.3 of the proposal.
- **One machine, one core:** a 4-core x86-64 cloud container. Absolute
  numbers will differ elsewhere; the ratios between engines are what the
  proposal uses.
- **WAMR AOT and LLVM JIT were not measured** (they need an LLVM build).
  They would be expected to land near wasmtime; the WAMR fast JIT and
  interpreter figures are the lower bounds.
