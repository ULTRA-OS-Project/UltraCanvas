#### 2026-10-06 *0.2.0*
- **Apps get timers, fetch, storage and the clipboard (element ABI v2).**
  Beside its elements an app may now start timers (`uc_timer_start`), fetch
  over http(s) (`uc_fetch` and its status, body, headers and close), keep
  key/value data across runs (`uc_storage_*`, 5 MB per origin, on disk under
  the settings folder) and copy text to the clipboard (`uc_clipboard_write`,
  only while it handles a click, a toggle or an edit). Timers and finished
  fetches arrive through `uc_event` with handle 0. Fetch follows a browser's
  rules without cookies: no http from an https app, any request to the app's
  own origin, to another origin only simple ones, and their answers only
  when the server allows the app's origin (CORS), with only the headers it
  exposes. A refused fetch or clipboard write says why on the console. A v1
  app runs unchanged; a v2 app exports `uc_abi_version()` returning 2, so
  UltraWeb 0.1.0 refuses it with a message. `about:demo` counts into storage,
  copies what was typed and shows a timer.
- **Nothing is stretched unless the app asks.** A container stretched every
  child across it, so a button given a width in a column, or a text input's
  240 px, went full width. Children now sit at their own size, at the start;
  the new `UC_PROP_ALIGN` (start, center, end, stretch) is how an app asks
  for something else, and even stretch leaves a child with a set width at
  that width. This is the framework's layout engine's rule now, for every
  app (see the framework changelog).
- **An app no longer inherits the last one's root.** Padding, gap,
  direction, alignment and background an app set on its root stayed for the
  next app opened in the window; the root goes back to the window's setup
  when an app goes.
- **An app cut off while downloading is an error.** A transfer that broke
  after the server's first line (a timeout, a lost connection) used to hand
  over the part that had arrived as if it were the app; UltraWeb now says
  it could not load it.
- `--check` runs `uc_main` as a first visit would: storage empty and not
  kept, timers that never fire, no fetch or clipboard.

#### 2026-10-06 *0.1.0*
- **UltraWeb runs WebAssembly apps.** The first version of the ULTRA OS
  browser loads a `.wasm` (or WebAssembly text) app from `about:demo`, a file
  path, a `file://` URL or an `https://` URL, and runs it in a sandbox: the
  app builds its UI out of real UltraCanvas elements - containers, labels,
  buttons, text inputs, checkboxes - through the element ABI in
  `UltraWeb/guest/ultraweb.h`, and holds only handles to them. An app that
  traps or runs one call for longer than two seconds is stopped and its error
  shown, and its memory cannot grow past 256 MB, so it can neither crash nor
  freeze the browser. `about:demo` is
  built in, and `UltraWeb --check <address>` starts an app without a window.
  HTML pages and JavaScript come in later phases
  (`Docs/UltraWeb/UltraWebProposal.md`). Runs on Linux in this version; the
  framework's WasmHost module, on wasmtime 49, is what it is built on (see the
  framework changelog).
