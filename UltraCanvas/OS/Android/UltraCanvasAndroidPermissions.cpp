// OS/Android/UltraCanvasAndroidPermissions.cpp
// Android runtime permissions over JNI. See the header.
//
// Version: 1.1.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (same pattern as Linux).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidPermissions.h"
#include "UltraCanvasAndroidApplication.h"
#include "UltraCanvasAndroidJni.h"
#include "UltraCanvasDebug.h"

#include <jni.h>

#include <atomic>
#include <map>
#include <mutex>
#include <utility>

namespace UltraCanvas {
namespace AndroidPermissions {

    namespace {
        // Requests waiting for onRequestPermissionsResult, by request id.
        std::mutex g_mutex;
        std::map<int, std::function<void(bool)>> g_pending;
        int g_nextRequestId = 1;

        std::atomic<int> g_displayRotation{0};

        int Normalise(int degrees) {
            return ((degrees % 360) + 360) % 360 / 90 * 90;
        }

        // Display.getRotation() through Activity.getWindowManager()
        // .getDefaultDisplay(), for a plain NativeActivity, which has no
        // getDisplayRotationDegrees(). -1 when any step fails.
        int QueryRotationGeneric(JNIEnv* env, jobject activity) {
            int degrees = -1;
            jclass activityClass = env->GetObjectClass(activity);
            jmethodID getWm = env->GetMethodID(activityClass, "getWindowManager",
                                               "()Landroid/view/WindowManager;");
            env->DeleteLocalRef(activityClass);
            if (AndroidJni::ClearException(env, "getWindowManager lookup") || !getWm) return -1;
            jobject wm = env->CallObjectMethod(activity, getWm);
            if (AndroidJni::ClearException(env, "getWindowManager") || !wm) return -1;
            jclass wmClass = env->GetObjectClass(wm);
            jmethodID getDisplay = env->GetMethodID(wmClass, "getDefaultDisplay",
                                                    "()Landroid/view/Display;");
            env->DeleteLocalRef(wmClass);
            jobject display = nullptr;
            if (!AndroidJni::ClearException(env, "getDefaultDisplay lookup") && getDisplay) {
                display = env->CallObjectMethod(wm, getDisplay);
                if (AndroidJni::ClearException(env, "getDefaultDisplay")) display = nullptr;
            }
            if (display) {
                jclass displayClass = env->GetObjectClass(display);
                jmethodID getRotation = env->GetMethodID(displayClass, "getRotation", "()I");
                env->DeleteLocalRef(displayClass);
                if (!AndroidJni::ClearException(env, "getRotation lookup") && getRotation) {
                    const jint surfaceRotation = env->CallIntMethod(display, getRotation);
                    if (!AndroidJni::ClearException(env, "getRotation")) {
                        degrees = static_cast<int>(surfaceRotation) * 90;   // Surface.ROTATION_*
                    }
                }
                env->DeleteLocalRef(display);
            }
            env->DeleteLocalRef(wm);
            return degrees;
        }

        // Answer on the UI thread, which is where every other framework
        // callback runs; the Java answer arrives on the Java UI thread.
        void Deliver(std::function<void(bool)> callback, bool granted) {
            if (!callback) return;
            if (auto* app = UltraCanvasAndroidApplication::GetInstance()) {
                app->PostToUIThread([callback = std::move(callback), granted]() {
                    callback(granted);
                });
            } else {
                callback(granted);
            }
        }
    }

    bool IsGranted(const char* permission) {
        JNIEnv* env = AndroidJni::GetEnv();
        jobject activity = AndroidJni::GetActivity();
        if (!env || !activity || !permission) return false;

        jclass cls = env->GetObjectClass(activity);
        jmethodID mid = env->GetMethodID(cls, "checkSelfPermission", "(Ljava/lang/String;)I");
        env->DeleteLocalRef(cls);
        if (AndroidJni::ClearException(env, "checkSelfPermission lookup") || !mid) return false;

        jstring name = AndroidJni::ToJString(env, permission);
        const jint result = env->CallIntMethod(activity, mid, name);
        env->DeleteLocalRef(name);
        if (AndroidJni::ClearException(env, "checkSelfPermission")) return false;
        return result == 0;   // PackageManager.PERMISSION_GRANTED
    }

    void Request(const char* permission, std::function<void(bool)> callback) {
        if (IsGranted(permission)) {
            Deliver(std::move(callback), true);
            return;
        }
        JNIEnv* env = AndroidJni::GetEnv();
        jobject activity = AndroidJni::GetActivity();
        if (!env || !activity || !permission) {
            Deliver(std::move(callback), false);
            return;
        }

        jclass cls = env->GetObjectClass(activity);
        jmethodID mid = env->GetMethodID(cls, "requestPermissionAsync", "(Ljava/lang/String;I)V");
        env->DeleteLocalRef(cls);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();   // a plain NativeActivity: nobody can answer
            debugOutput << "UltraCanvas Android: cannot ask for " << permission
                        << " without UltraCanvasActivity" << std::endl;
            Deliver(std::move(callback), false);
            return;
        }

        int requestId;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            requestId = g_nextRequestId++;
            g_pending[requestId] = std::move(callback);
        }
        jstring name = AndroidJni::ToJString(env, permission);
        env->CallVoidMethod(activity, mid, name, static_cast<jint>(requestId));
        env->DeleteLocalRef(name);
        if (AndroidJni::ClearException(env, "requestPermissionAsync")) {
            std::function<void(bool)> pending;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                pending = std::move(g_pending[requestId]);
                g_pending.erase(requestId);
            }
            Deliver(std::move(pending), false);
        }
    }

    int DisplayRotationDegrees() {
        JNIEnv* env = AndroidJni::GetEnv();
        jobject activity = AndroidJni::GetActivity();
        if (!env || !activity) return g_displayRotation.load();
        jclass cls = env->GetObjectClass(activity);
        jmethodID mid = env->GetMethodID(cls, "getDisplayRotationDegrees", "()I");
        env->DeleteLocalRef(cls);
        int degrees = -1;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();   // plain NativeActivity
            degrees = QueryRotationGeneric(env, activity);
        } else {
            const jint value = env->CallIntMethod(activity, mid);
            if (!AndroidJni::ClearException(env, "getDisplayRotationDegrees")) {
                degrees = static_cast<int>(value);
            }
        }
        if (degrees < 0) return g_displayRotation.load();
        degrees = Normalise(degrees);
        g_displayRotation.store(degrees);
        return degrees;
    }

    int CurrentDisplayRotationDegrees() {
        return g_displayRotation.load();
    }

} // namespace AndroidPermissions
} // namespace UltraCanvas

// UltraCanvasActivity's display listener: the display turned. Runs on the
// Java UI thread.
extern "C" JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnDisplayRotationChanged(
        JNIEnv*, jclass, jint degrees) {
    using namespace UltraCanvas::AndroidPermissions;
    g_displayRotation.store(Normalise(static_cast<int>(degrees)));
}

// The system's answer to requestPermissionAsync. Runs on the Java UI thread.
extern "C" JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnPermissionResult(
        JNIEnv*, jclass, jint requestId, jboolean granted) {
    using namespace UltraCanvas::AndroidPermissions;
    std::function<void(bool)> callback;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_pending.find(static_cast<int>(requestId));
        if (it == g_pending.end()) return;
        callback = std::move(it->second);
        g_pending.erase(it);
    }
    Deliver(std::move(callback), granted == JNI_TRUE);
}
