// OS/Android/UltraCanvasAndroidMain.cpp
// android_native_app_glue entry point for UltraCanvas applications.
//
// NativeActivity keeps the whole application in C++: the glue spawns a thread,
// calls android_main() on it, and that thread hosts the framework's blocking
// Run() loop. An application targeting Android replaces its main() with
//
//     extern "C" int ultracanvas_app_main(int argc, char** argv);
//
// (a thin #ifdef __ANDROID__ shim around the existing main body). By the time
// it runs, the activity surface exists and HOME/TMPDIR/XDG_CACHE_HOME point
// into the app sandbox, so path-based resource/config/font code works
// unchanged. App assets must be extracted to $HOME/share/ on first launch
// (see SetResourcesDir's Android arm); the packaging layer owns that step.
// Version: 1.0.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasAndroidApplication.h"

#include <android/looper.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include <cstdlib>
#include <string>
#include <sys/stat.h>

// Provided by the application (its main() under a different name).
extern "C" int ultracanvas_app_main(int argc, char** argv);

namespace {

    void ExportSandboxEnvironment(android_app* app) {
        const char* dataPath = app->activity->internalDataPath;
        if (!dataPath || !*dataPath) return;

        // Don't overwrite: a wrapper (test harness, custom activity) may have
        // set these already.
        setenv("HOME", dataPath, 0);

        const std::string tmpDir = std::string(dataPath) + "/tmp";
        mkdir(tmpDir.c_str(), 0700);
        setenv("TMPDIR", tmpDir.c_str(), 0);

        const std::string cacheDir = std::string(dataPath) + "/cache";
        mkdir(cacheDir.c_str(), 0700);
        setenv("XDG_CACHE_HOME", cacheDir.c_str(), 0);
    }

    // Pump the glue's looper until the activity surface exists, so the app's
    // window Create() finds an ANativeWindow to attach to.
    bool WaitForNativeWindow(android_app* app) {
        while (!app->window && !app->destroyRequested) {
            int events = 0;
            android_poll_source* source = nullptr;
            if (ALooper_pollOnce(-1, nullptr, &events,
                                 reinterpret_cast<void**>(&source)) >= 0 && source) {
                source->process(app, source);
            }
        }
        return app->window != nullptr;
    }

} // namespace

void android_main(android_app* app) {
    UltraCanvas::UltraCanvasAndroidApplication::SetAndroidApp(app);
    ExportSandboxEnvironment(app);

    if (!WaitForNativeWindow(app)) {
        return;   // activity destroyed before a surface ever appeared
    }

    static char argv0[] = "ultracanvas-app";
    char* argv[] = { argv0, nullptr };
    ultracanvas_app_main(1, argv);

    // The app's main loop is done; ask the activity to finish, then keep
    // draining commands until the system confirms destruction so the glue
    // thread shuts down cleanly (detached input queues, freed saved state).
    ANativeActivity_finish(app->activity);
    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, &events,
                             reinterpret_cast<void**>(&source)) >= 0 && source) {
            source->process(app, source);
        }
    }
}
