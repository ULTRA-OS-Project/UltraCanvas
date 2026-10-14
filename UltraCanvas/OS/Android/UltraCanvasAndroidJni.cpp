// OS/Android/UltraCanvasAndroidJni.cpp
// Minimal JNI plumbing shared by the Android backend's Java bridges.
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidJni.h"

#include <cstdint>
#include <vector>
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
        // Not GetStringUTFChars: that is JNI's *modified* UTF-8, which writes
        // a character outside the BMP (every emoji) as two 3-byte surrogate
        // halves, so text widgets received six bytes no font could draw and
        // byte offsets computed by Java's real UTF-8 no longer lined up.
        const jsize length = env->GetStringLength(str);
        std::vector<jchar> units(static_cast<size_t>(length));
        if (length > 0) env->GetStringRegion(str, 0, length, units.data());
        if (ClearException(env, "GetStringRegion")) return {};

        std::string result;
        result.reserve(units.size());
        for (size_t i = 0; i < units.size(); ++i) {
            uint32_t cp = units[i];
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units.size() &&
                units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (units[i + 1] - 0xDC00);
                ++i;
            } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                cp = 0xFFFD;                  // unpaired surrogate
            }
            if (cp < 0x80) {
                result += static_cast<char>(cp);
            } else if (cp < 0x800) {
                result += static_cast<char>(0xC0 | (cp >> 6));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                result += static_cast<char>(0xE0 | (cp >> 12));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                result += static_cast<char>(0xF0 | (cp >> 18));
                result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
        return result;
    }

    std::vector<std::string> SplitLines(const std::string& text) {
        std::vector<std::string> lines;
        std::string::size_type start = 0;
        while (start <= text.size()) {
            const std::string::size_type end = text.find('\n', start);
            if (end == std::string::npos) {
                if (start < text.size()) lines.push_back(text.substr(start));
                break;
            }
            if (end > start) lines.push_back(text.substr(start, end - start));
            start = end + 1;
        }
        return lines;
    }

} // namespace AndroidJni
} // namespace UltraCanvas
