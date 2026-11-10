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
// it runs, the activity surface exists, HOME/TMPDIR/XDG_CACHE_HOME point into
// the app sandbox, and the APK's assets have been unpacked to $HOME/share
// (where SetResourcesDir's Android arm looks), so path-based
// resource/config/font code works unchanged.
// Version: 1.3.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasAndroidApplication.h"
#include "UltraCanvasAndroidAssets.h"
#include "UltraCanvasAndroidJni.h"
#include "UltraCanvasAndroidLog.h"

#include <android/looper.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include <cstdlib>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

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

    // The launching intent's "ultracanvas.args" string extra, split on
    // whitespace: the command line an app's main() would have had. Lets a
    // test start the app the way a desktop run would be started
    // (`am start -e ultracanvas.args "--no-about" ...`). Empty when the
    // activity was started without it - the launcher never sets it.
    std::vector<std::string> ReadIntentArguments() {
        std::vector<std::string> args;
        JNIEnv* env = UltraCanvas::AndroidJni::GetEnv();
        jobject activity = UltraCanvas::AndroidJni::GetActivity();
        if (!env || !activity) return args;

        jclass activityClass = env->GetObjectClass(activity);
        jmethodID getIntent = env->GetMethodID(activityClass, "getIntent",
                                               "()Landroid/content/Intent;");
        jobject intent = getIntent ? env->CallObjectMethod(activity, getIntent) : nullptr;
        if (UltraCanvas::AndroidJni::ClearException(env, "Activity.getIntent")) intent = nullptr;
        if (intent) {
            jclass intentClass = env->GetObjectClass(intent);
            jmethodID getStringExtra = env->GetMethodID(intentClass, "getStringExtra",
                                           "(Ljava/lang/String;)Ljava/lang/String;");
            jstring key = UltraCanvas::AndroidJni::ToJString(env, "ultracanvas.args");
            auto value = static_cast<jstring>(getStringExtra && key
                ? env->CallObjectMethod(intent, getStringExtra, key) : nullptr);
            if (UltraCanvas::AndroidJni::ClearException(env, "Intent.getStringExtra")) value = nullptr;
            std::istringstream words(UltraCanvas::AndroidJni::ToStdString(env, value));
            for (std::string word; words >> word;) args.push_back(word);
            if (value) env->DeleteLocalRef(value);
            if (key) env->DeleteLocalRef(key);
            env->DeleteLocalRef(intentClass);
            env->DeleteLocalRef(intent);
        }
        env->DeleteLocalRef(activityClass);
        return args;
    }

} // namespace

void android_main(android_app* app) {
    // First, before anything can produce a diagnostic: on Android stdout and
    // stderr go to /dev/null, so without this every warning from the
    // dependency stack (fontconfig, cairo, Pango) and every std::cout/cerr
    // call site in shared framework code is lost. debugOutput reaches logcat
    // on its own (UltraCanvasDebug.h); this covers everything that does not.
    UltraCanvas::RedirectStdioToLogcat();

    UltraCanvas::UltraCanvasAndroidApplication::SetAndroidApp(app);
    ExportSandboxEnvironment(app);

    // One logcat line saying whether Java can call back into this library -
    // every dialog answer, keystroke and permission answer depends on it.
    UltraCanvas::AndroidJni::CheckJavaToNativeBridge();

    // Before anything can load a font, icon or media file by path: inside an
    // APK those are not on the filesystem. Only the first launch after an
    // install or update pays for this.
    UltraCanvas::ExtractBundledAssets(app);

    if (!WaitForNativeWindow(app)) {
        return;   // activity destroyed before a surface ever appeared
    }

    static char argv0[] = "ultracanvas-app";
    std::vector<std::string> arguments = ReadIntentArguments();
    std::vector<char*> argv = { argv0 };
    for (std::string& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    ultracanvas_app_main(static_cast<int>(argv.size() - 1), argv.data());

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
