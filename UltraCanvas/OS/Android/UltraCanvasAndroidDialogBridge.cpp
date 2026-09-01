// OS/Android/UltraCanvasAndroidDialogBridge.cpp
// Sync-over-async bridge to the Java dialogs in UltraCanvasActivity.
// Version: 1.0.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (same pattern as Linux).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidDialogBridge.h"
#include "UltraCanvasAndroidApplication.h"
#include "UltraCanvasAndroidJni.h"
#include "UltraCanvasDebug.h"

#include <atomic>
#include <mutex>

namespace UltraCanvas {
namespace AndroidDialogs {

    namespace {

        // Dialogs are modal and the requesting thread blocks inside
        // ShowMessage(), so at most one can ever be outstanding: a single slot
        // is enough. Written by the Java UI thread, read by the glue thread.
        struct PendingDialog {
            std::mutex mutex;
            std::atomic<bool> resolved{true};
            int requestId = 0;
            JavaResult result = JavaResult::Cancel;
            std::string value;
        };
        PendingDialog g_pending;

        // 0 is never used, so a result for request 0 (or any stale id) is
        // recognisably not the one being waited on.
        int g_nextRequestId = 1;

        // Local ref to the activity's class, or null when this app runs a
        // plain NativeActivity. Looked up once.
        jmethodID FindShowMessageMethod(JNIEnv* env, jobject activity) {
            jclass activityClass = env->GetObjectClass(activity);
            jmethodID mid = env->GetMethodID(activityClass, "showMessageDialog",
                    "(ILjava/lang/String;Ljava/lang/String;I"
                    "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
            env->DeleteLocalRef(activityClass);
            if (env->ExceptionCheck()) {
                // NoSuchMethodError: a plain NativeActivity. Expected, not a
                // failure - clear it quietly and let the caller fall back.
                env->ExceptionClear();
                return nullptr;
            }
            return mid;
        }

        // Null label -> Java null (button omitted).
        jstring MakeLabel(JNIEnv* env, const char* label) {
            return label ? env->NewStringUTF(label) : nullptr;
        }

    } // namespace

    JavaDialogOutcome ShowMessage(const std::string& title,
                                  const std::string& message,
                                  JavaIcon icon,
                                  const char* positiveLabel,
                                  const char* negativeLabel,
                                  const char* neutralLabel) {
        JavaDialogOutcome outcome;

        auto* app = UltraCanvasAndroidApplication::GetInstance();
        JNIEnv* env = AndroidJni::GetEnv();
        jobject activity = AndroidJni::GetActivity();
        if (!app || !env || !activity) return outcome;

        jmethodID midShow = FindShowMessageMethod(env, activity);
        if (!midShow) return outcome;   // plain NativeActivity: caller falls back

        int requestId;
        {
            std::lock_guard<std::mutex> lock(g_pending.mutex);
            requestId = g_nextRequestId++;
            g_pending.requestId = requestId;
            g_pending.result = JavaResult::Cancel;
            g_pending.value.clear();
            g_pending.resolved.store(false, std::memory_order_release);
        }

        jstring jTitle = env->NewStringUTF(title.c_str());
        jstring jMessage = env->NewStringUTF(message.c_str());
        jstring jPositive = MakeLabel(env, positiveLabel);
        jstring jNegative = MakeLabel(env, negativeLabel);
        jstring jNeutral = MakeLabel(env, neutralLabel);

        env->CallVoidMethod(activity, midShow, static_cast<jint>(requestId),
                            jTitle, jMessage, static_cast<jint>(icon),
                            jPositive, jNegative, jNeutral);

        env->DeleteLocalRef(jTitle);
        env->DeleteLocalRef(jMessage);
        if (jPositive) env->DeleteLocalRef(jPositive);
        if (jNegative) env->DeleteLocalRef(jNegative);
        if (jNeutral) env->DeleteLocalRef(jNeutral);

        if (AndroidJni::ClearException(env, "showMessageDialog")) {
            g_pending.resolved.store(true, std::memory_order_release);
            return outcome;
        }

        const bool answered = app->PumpWhileModal([] {
            return g_pending.resolved.load(std::memory_order_acquire);
        });
        if (!answered) {
            // Activity being destroyed: stop waiting for a result that will
            // never come and let the caller take its cancel path.
            g_pending.resolved.store(true, std::memory_order_release);
            return outcome;
        }

        std::lock_guard<std::mutex> lock(g_pending.mutex);
        outcome.bridged = true;
        outcome.result = g_pending.result;
        outcome.value = g_pending.value;
        return outcome;
    }

} // namespace AndroidDialogs
} // namespace UltraCanvas

// ===== JNI ENTRY POINT =====
// Resolved by name against the native library NativeActivity already loaded,
// so no RegisterNatives call is needed. Runs on the Java UI thread.
extern "C" JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnDialogResult(
        JNIEnv* env, jclass, jint requestId, jint result, jstring value) {
    using namespace UltraCanvas;
    using namespace UltraCanvas::AndroidDialogs;

    std::lock_guard<std::mutex> lock(g_pending.mutex);
    if (g_pending.requestId != static_cast<int>(requestId)) {
        // A result for a dialog nobody is waiting on any more (the activity
        // was torn down while it was up). Dropping it is the whole point of
        // carrying the request id.
        return;
    }
    g_pending.result = static_cast<JavaResult>(result);
    g_pending.value = value ? AndroidJni::ToStdString(env, value) : std::string();
    g_pending.resolved.store(true, std::memory_order_release);
}
