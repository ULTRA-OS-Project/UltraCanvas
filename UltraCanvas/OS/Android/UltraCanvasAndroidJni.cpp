// OS/Android/UltraCanvasAndroidJni.cpp
// Minimal JNI plumbing shared by the Android backend's Java bridges.
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidJni.h"
#include "UltraCanvasAndroidApplication.h"
#include "UltraCanvasDebug.h"

#include <android/native_activity.h>
#include <android_native_app_glue.h>

namespace UltraCanvas {
namespace AndroidJni {

    // The framework is single-threaded on the glue thread, so one flag is
    // enough - no thread-local bookkeeping needed.
    static bool attachedHere = false;

    JNIEnv* GetEnv() {
        android_app* app = UltraCanvasAndroidApplication::GetAndroidApp();
        if (!app || !app->activity || !app->activity->vm) return nullptr;
        JavaVM* vm = app->activity->vm;

        JNIEnv* env = nullptr;
        jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (state == JNI_OK) return env;
        if (state != JNI_EDETACHED) return nullptr;

#ifdef _JAVASOFT_JNI_H_
        // Host smoke builds compile against the JDK's jni.h, whose C++
        // AttachCurrentThread takes void** where the NDK's takes JNIEnv**.
        if (vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
#else
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
#endif
            debugOutput << "UltraCanvas Android JNI: AttachCurrentThread failed" << std::endl;
            return nullptr;
        }
        attachedHere = true;
        return env;
    }

    void DetachCurrentThread() {
        if (!attachedHere) return;
        android_app* app = UltraCanvasAndroidApplication::GetAndroidApp();
        if (app && app->activity && app->activity->vm) {
            app->activity->vm->DetachCurrentThread();
        }
        attachedHere = false;
    }

    jobject GetActivity() {
        android_app* app = UltraCanvasAndroidApplication::GetAndroidApp();
        return (app && app->activity) ? app->activity->clazz : nullptr;
    }

    bool ClearException(JNIEnv* env, const char* where) {
        if (!env->ExceptionCheck()) return false;
        debugOutput << "UltraCanvas Android JNI: Java exception in "
                    << where << std::endl;
        env->ExceptionDescribe();   // goes to logcat
        env->ExceptionClear();
        return true;
    }

    std::string ToStdString(JNIEnv* env, jstring str) {
        if (!str) return {};
        const char* utf = env->GetStringUTFChars(str, nullptr);
        if (!utf) return {};
        std::string result(utf);
        env->ReleaseStringUTFChars(str, utf);
        return result;
    }

} // namespace AndroidJni
} // namespace UltraCanvas
