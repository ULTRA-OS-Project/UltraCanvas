// OS/WASM/runtime/UltraCanvasWasmRuntime.cpp
// The UltraCanvas runtime for browsers: UltraCanvas, cairo, pango, glib and
// the bundled fonts linked once into an Emscripten main module that exports
// the whole C++ API. An application is a side module - its own code only,
// compiled against the UltraCanvas headers - that this program loads with
// dlopen and starts by calling its main(). A browser extension installs the
// runtime once; a site can also serve it itself
// (Docs/UltraCanvas/UltraCanvasWasmRuntime.md).
//
// The page's loader (ultracanvas-loader.js) starts this program, waits for
// Module.onUltraCanvasRuntimeReady, writes the app into the virtual file
// system and calls uc_runtime_start_app with its path. The app's main() is
// an ordinary UltraCanvas main(): its Run() starts the browser main loop
// and never returns, as in any Emscripten page.
//
// Version: 0.1.0 - first version
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework / ULTRA OS

#include <dlfcn.h>
#include <emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void Report(const std::string& message) {
    std::printf("[UltraCanvas runtime] %s\n", message.c_str());
    EM_ASM({ if (Module.onUltraCanvasAppFailed) Module.onUltraCanvasAppFailed(UTF8ToString($0)); }, message.c_str());
}

} // namespace

extern "C" {

// glib defines g_strcmp0, which pango uses, in gtestutils.c, and that file
// also refers to g_io_channel_unix_new, which glib's WebAssembly build does
// not have. An ordinary link never notices - the test utilities are never
// called - but a main module exports everything it links, so the reference
// has to resolve. Nothing calls it; if anything ever did, it stops here.
EMSCRIPTEN_KEEPALIVE void* g_io_channel_unix_new(int) {
    Report("g_io_channel_unix_new called: glib has no Unix I/O channels in the browser");
    std::abort();
}

// Loads the app at `path` (a side module in the virtual file system) and
// runs its main(). Returns only when the app could not start, or when its
// main() returns without starting a main loop; once the app's Run() has
// started the loop, the call ends in Emscripten's "unwind", which the
// loader expects.
EMSCRIPTEN_KEEPALIVE int uc_runtime_start_app(const char* path) {
    void* app = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!app) {
        const char* why = dlerror();
        Report(std::string("the app did not load: ") + (why ? why : "unknown error"));
        return 1;
    }
    // A C++ main(int, char**) is __main_argc_argv in WebAssembly.
    using Main = int (*)(int, char**);
    Main entry = reinterpret_cast<Main>(dlsym(app, "__main_argc_argv"));
    if (!entry) entry = reinterpret_cast<Main>(dlsym(app, "main"));
    if (!entry) {
        Report("the app has no main()");
        return 1;
    }
    static char name[] = "app";
    static char* argv[] = {name, nullptr};
    EM_ASM({ if (Module.onUltraCanvasAppStarting) Module.onUltraCanvasAppStarting(); });
    const int status = entry(1, argv);
    Report("the app's main() returned " + std::to_string(status) + " without running");
    return status;
}

} // extern "C"

int main() {
    // Nothing of the app's yet: the loader hands it over (uc_runtime_start_app).
    EM_ASM({ if (Module.onUltraCanvasRuntimeReady) Module.onUltraCanvasRuntimeReady(); });
    return 0;
}
