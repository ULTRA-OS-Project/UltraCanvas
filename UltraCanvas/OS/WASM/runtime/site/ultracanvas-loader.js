// OS/WASM/runtime/site/ultracanvas-loader.js
// Runs an UltraCanvas app - a WebAssembly side module built with
// ultracanvas_wasm_app() - in an ordinary browser page. A site serves this
// script and its app, and - unless it relies on the extension alone - the
// runtime:
//
//   <script src="ultracanvas-loader.js" data-app="hello-app.wasm" data-runtime="runtime/"
//           data-runtime-version="0.9.523"></script>
//
// data-app              the app, relative to the page
// data-runtime          where the site keeps ultracanvas-runtime.{js,wasm,data};
//                       left out, the page needs the UltraCanvas extension
// data-runtime-version  the runtime version the app was built against. An
//                       installed extension of another version is passed
//                       over for the site's copy (an app is bound to the
//                       runtime it was built with); without the attribute
//                       any installed version is taken
// data-status           the id of an element that shows progress (default
//                       "ultracanvas-status")
//
// The runtime runs in this page, under its origin. The page's <html>
// element tells a test what happened: data-ultracanvas-runtime-source
// ("extension" or "site") and data-ultracanvas-state ("running" or "failed").
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
(function () {
    "use strict";
    const script = document.currentScript;
    const root = document.documentElement;
    const statusId = script.dataset.status || "ultracanvas-status";
    const status = (text) => {
        const element = document.getElementById(statusId);
        if (element) element.textContent = text;
        console.log("[UltraCanvas loader] " + text);
    };
    const fail = (text) => {
        root.dataset.ultracanvasState = "failed";
        status(text);
    };

    if (!script.dataset.app) {
        fail("ultracanvas-loader.js needs a data-app attribute naming the app");
        return;
    }
    const app = new URL(script.dataset.app, document.baseURI).href;
    const wanted = script.dataset.runtimeVersion || "";
    const installedVersion = root.dataset.ultracanvasRuntimeVersion || "";
    const installed = root.dataset.ultracanvasRuntime && (!wanted || wanted === installedVersion)
        ? root.dataset.ultracanvasRuntime : "";
    const hosted = script.dataset.runtime ? new URL(script.dataset.runtime, document.baseURI).href : "";
    const base = installed || hosted;
    if (!base) {
        fail(root.dataset.ultracanvasRuntime
            ? "This app needs UltraCanvas " + wanted + "; the extension has " + installedVersion + "."
            : "This page runs an UltraCanvas app, which needs the UltraCanvas browser extension. Install it and reload the page.");
        return;
    }
    root.dataset.ultracanvasRuntimeSource = installed ? "extension" : "site";
    status("Loading " + app + " with the UltraCanvas runtime from the " + root.dataset.ultracanvasRuntimeSource);

    // The app downloads while the runtime starts.
    const appBytes = fetch(app).then((response) => {
        if (!response.ok) throw new Error(app + " answered " + response.status);
        return response.arrayBuffer();
    });

    window.Module = {
        locateFile: (path) => base + path,
        onUltraCanvasRuntimeReady: () => {
            appBytes.then((buffer) => {
                try { Module.FS.mkdir("/app"); } catch (e) { /* there already */ }
                Module.FS.writeFile("/app/app.wasm", new Uint8Array(buffer));
                const path = Module.stringToNewUTF8("/app/app.wasm");
                try {
                    Module._uc_runtime_start_app(path);
                } catch (e) {
                    // The app's Run() started the browser's main loop: Emscripten
                    // leaves the call this way, and the app keeps running.
                    if (e !== "unwind") throw e;
                }
            }).catch((e) => fail("Cannot start the app: " + (e && e.message ? e.message : e)));
        },
        onUltraCanvasAppStarting: () => {
            root.dataset.ultracanvasState = "running";
            status("Running " + app);
        },
        onUltraCanvasAppFailed: (message) => fail(message),
    };

    const tag = document.createElement("script");
    tag.src = base + "ultracanvas-runtime.js";
    tag.onerror = () => fail("Cannot load the UltraCanvas runtime from " + base);
    document.head.appendChild(tag);
})();
