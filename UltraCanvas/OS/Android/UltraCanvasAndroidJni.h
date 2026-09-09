// OS/Android/UltraCanvasAndroidJni.h
// Minimal JNI plumbing shared by the Android backend's Java bridges
// (clipboard, soft keyboard). The framework runs on android_native_app_glue's
// dedicated native thread, which is NOT attached to the JVM by default: every
// bridge goes through GetEnv(), which attaches lazily and stays attached for
// the thread's lifetime (detached once in DetachCurrentThread() at shutdown).
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_JNI_H
#define ULTRACANVAS_ANDROID_JNI_H

#include <jni.h>
#include <string>

namespace UltraCanvas {
namespace AndroidJni {

    // JNIEnv for the calling (glue) thread, attaching it to the JVM on first
    // use. Returns null when no android_app/VM exists (never inside a running
    // activity).
    JNIEnv* GetEnv();

    // Detach the glue thread from the JVM if GetEnv() attached it. Called
    // from the application's ShutdownNative().
    void DetachCurrentThread();

    // The NativeActivity instance (android_app->activity->clazz). Null before
    // the glue delivered it.
    jobject GetActivity();

    // If a Java exception is pending: log + clear it and return true (the
    // JNI call chain that raised it must bail out). Android 10+ throws
    // SecurityException for background clipboard reads, so this is a normal
    // runtime path, not just a programming-error backstop.
    bool ClearException(JNIEnv* env, const char* where);

    // UTF-8 std::string from a jstring (empty for null).
    std::string ToStdString(JNIEnv* env, jstring str);

} // namespace AndroidJni
} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_JNI_H
