// Apps/UltraWeb/browser/site/ultraweb-loader.js
// Runs an UltraWeb app in an ordinary browser page. A site serves this
// script, its app.wasm, and - unless it relies on the extension alone - the
// runtime:
//
//   <script src="ultraweb-loader.js" data-app="hello.wasm" data-runtime="runtime/"></script>
//
// data-app      the app, relative to the page
// data-runtime  where the site keeps ultraweb-runtime.{js,wasm,data}; left
//               out, the page needs the UltraCanvas extension
// data-status   the id of an element that shows progress (default
//               "ultraweb-status")
//
// With the extension installed its runtime is used, and the site's copy is
// never downloaded. Either way the runtime is loaded into this page and runs
// here, under the page's origin: the app's fetches, storage and clipboard
// are the page's. The page's <html> element tells a test what happened:
// data-ultraweb-runtime-source ("extension" or "site") and
// data-ultraweb-state ("running" or "failed").
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
(function () {
    "use strict";
    const script = document.currentScript;
    const root = document.documentElement;
    const statusId = script.dataset.status || "ultraweb-status";
    const status = (text) => {
        const element = document.getElementById(statusId);
        if (element) element.textContent = text;
        console.log("[UltraWeb loader] " + text);
    };
    const fail = (text) => {
        root.dataset.ultrawebState = "failed";
        status(text);
    };

    if (!script.dataset.app) {
        fail("ultraweb-loader.js needs a data-app attribute naming the app");
        return;
    }
    const app = new URL(script.dataset.app, document.baseURI).href;
    const installed = root.dataset.ultrawebRuntime || "";
    const hosted = script.dataset.runtime ? new URL(script.dataset.runtime, document.baseURI).href : "";
    const base = installed || hosted;
    if (!base) {
        fail("This page runs an UltraWeb app, which needs the UltraCanvas browser extension. Install it and reload the page.");
        return;
    }
    root.dataset.ultrawebRuntimeSource = installed ? "extension" : "site";
    status("Loading " + app + " with the runtime from the " + root.dataset.ultrawebRuntimeSource);

    // The app downloads while the runtime starts.
    const appBytes = fetch(app).then((response) => {
        if (!response.ok) throw new Error(app + " answered " + response.status);
        return response.arrayBuffer();
    });

    window.Module = {
        locateFile: (path) => base + path,
        onUltraWebReady: () => {
            appBytes.then((buffer) => {
                const bytes = new Uint8Array(buffer);
                const data = Module._malloc(bytes.length);
                Module.HEAPU8.set(bytes, data);
                const address = Module.stringToNewUTF8(app);
                Module._uw_runtime_start(data, bytes.length, address);
                Module._free(data);
                Module._free(address);
            }).catch((e) => fail("Cannot load the app: " + e.message));
        },
        onUltraWebStarted: () => {
            root.dataset.ultrawebState = "running";
            status("Running " + app);
        },
    };

    const tag = document.createElement("script");
    tag.src = base + "ultraweb-runtime.js";
    tag.onerror = () => fail("Cannot load the UltraWeb runtime from " + base);
    document.head.appendChild(tag);
})();
