// OS/Android/UltraCanvasAndroidClipboard.cpp
// Android clipboard backend: android.content.ClipboardManager over JNI.
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (same pattern as Linux).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidClipboard.h"
#include "UltraCanvasAndroidJni.h"
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    UltraCanvasAndroidClipboard::~UltraCanvasAndroidClipboard() {
        Shutdown();
    }

    bool UltraCanvasAndroidClipboard::Initialize() {
        JNIEnv* env = AndroidJni::GetEnv();
        jobject activity = AndroidJni::GetActivity();
        if (!env || !activity) {
            debugOutput << "UltraCanvas Android clipboard: no JNI environment" << std::endl;
            return false;
        }

        // clipboardManager = activity.getSystemService("clipboard")
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID midGetSystemService = env->GetMethodID(
                activityClass, "getSystemService",
                "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring serviceName = env->NewStringUTF("clipboard");   // Context.CLIPBOARD_SERVICE
        jobject manager = env->CallObjectMethod(activity, midGetSystemService, serviceName);
        env->DeleteLocalRef(serviceName);
        env->DeleteLocalRef(activityClass);
        if (AndroidJni::ClearException(env, "getSystemService(clipboard)") || !manager) {
            return false;
        }
        clipboardManager = env->NewGlobalRef(manager);
        env->DeleteLocalRef(manager);

        jclass managerClass = env->GetObjectClass(clipboardManager);
        midSetPrimaryClip = env->GetMethodID(managerClass, "setPrimaryClip",
                "(Landroid/content/ClipData;)V");
        midGetPrimaryClip = env->GetMethodID(managerClass, "getPrimaryClip",
                "()Landroid/content/ClipData;");
        midHasPrimaryClip = env->GetMethodID(managerClass, "hasPrimaryClip", "()Z");
        midGetPrimaryClipDescription = env->GetMethodID(managerClass,
                "getPrimaryClipDescription", "()Landroid/content/ClipDescription;");
        env->DeleteLocalRef(managerClass);

        jclass localClipData = env->FindClass("android/content/ClipData");
        clipDataClass = static_cast<jclass>(env->NewGlobalRef(localClipData));
        env->DeleteLocalRef(localClipData);
        midNewPlainText = env->GetStaticMethodID(clipDataClass, "newPlainText",
                "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;");
        midGetItemCount = env->GetMethodID(clipDataClass, "getItemCount", "()I");
        midGetItemAt = env->GetMethodID(clipDataClass, "getItemAt",
                "(I)Landroid/content/ClipData$Item;");

        jclass itemClass = env->FindClass("android/content/ClipData$Item");
        midCoerceToText = env->GetMethodID(itemClass, "coerceToText",
                "(Landroid/content/Context;)Ljava/lang/CharSequence;");
        env->DeleteLocalRef(itemClass);

        jclass charSeqClass = env->FindClass("java/lang/CharSequence");
        midCharSeqToString = env->GetMethodID(charSeqClass, "toString",
                "()Ljava/lang/String;");
        env->DeleteLocalRef(charSeqClass);

        jclass descClass = env->FindClass("android/content/ClipDescription");
        midGetTimestamp = env->GetMethodID(descClass, "getTimestamp", "()J");
        env->DeleteLocalRef(descClass);

        if (AndroidJni::ClearException(env, "clipboard method lookup")) {
            Shutdown();
            return false;
        }

        lastSeenTimestamp = QueryClipTimestamp();
        return true;
    }

    void UltraCanvasAndroidClipboard::Shutdown() {
        JNIEnv* env = AndroidJni::GetEnv();
        if (env) {
            if (clipboardManager) env->DeleteGlobalRef(clipboardManager);
            if (clipDataClass) env->DeleteGlobalRef(clipDataClass);
        }
        clipboardManager = nullptr;
        clipDataClass = nullptr;
    }

    bool UltraCanvasAndroidClipboard::SetClipboardText(const std::string& text) {
        JNIEnv* env = AndroidJni::GetEnv();
        if (!env || !clipboardManager) return false;

        jstring label = env->NewStringUTF("UltraCanvas");
        jstring value = env->NewStringUTF(text.c_str());
        jobject clip = env->CallStaticObjectMethod(clipDataClass, midNewPlainText,
                                                   label, value);
        env->DeleteLocalRef(label);
        env->DeleteLocalRef(value);
        if (AndroidJni::ClearException(env, "ClipData.newPlainText") || !clip) {
            return false;
        }

        env->CallVoidMethod(clipboardManager, midSetPrimaryClip, clip);
        env->DeleteLocalRef(clip);
        return !AndroidJni::ClearException(env, "setPrimaryClip");
    }

    bool UltraCanvasAndroidClipboard::GetClipboardText(std::string& text) {
        JNIEnv* env = AndroidJni::GetEnv();
        jobject activity = AndroidJni::GetActivity();
        if (!env || !clipboardManager || !activity) return false;

        jobject clip = env->CallObjectMethod(clipboardManager, midGetPrimaryClip);
        if (AndroidJni::ClearException(env, "getPrimaryClip") || !clip) {
            return false;   // empty clipboard, or read denied while unfocused
        }

        bool ok = false;
        if (env->CallIntMethod(clip, midGetItemCount) > 0) {
            jobject item = env->CallObjectMethod(clip, midGetItemAt, 0);
            if (!AndroidJni::ClearException(env, "getItemAt") && item) {
                jobject charSeq = env->CallObjectMethod(item, midCoerceToText, activity);
                if (!AndroidJni::ClearException(env, "coerceToText") && charSeq) {
                    auto str = static_cast<jstring>(
                            env->CallObjectMethod(charSeq, midCharSeqToString));
                    if (!AndroidJni::ClearException(env, "CharSequence.toString")) {
                        text = AndroidJni::ToStdString(env, str);
                        ok = true;
                    }
                    if (str) env->DeleteLocalRef(str);
                    env->DeleteLocalRef(charSeq);
                }
                env->DeleteLocalRef(item);
            }
        }
        env->DeleteLocalRef(clip);
        return ok;
    }

    int64_t UltraCanvasAndroidClipboard::QueryClipTimestamp() {
        JNIEnv* env = AndroidJni::GetEnv();
        if (!env || !clipboardManager) return 0;

        jobject desc = env->CallObjectMethod(clipboardManager,
                                             midGetPrimaryClipDescription);
        if (AndroidJni::ClearException(env, "getPrimaryClipDescription") || !desc) {
            return 0;
        }
        const int64_t ts = env->CallLongMethod(desc, midGetTimestamp);
        env->DeleteLocalRef(desc);
        if (AndroidJni::ClearException(env, "ClipDescription.getTimestamp")) {
            return 0;
        }
        return ts;
    }

    bool UltraCanvasAndroidClipboard::HasClipboardChanged() {
        return QueryClipTimestamp() != lastSeenTimestamp;
    }

    void UltraCanvasAndroidClipboard::ResetChangeState() {
        lastSeenTimestamp = QueryClipTimestamp();
    }

    std::vector<std::string> UltraCanvasAndroidClipboard::GetAvailableFormats() {
        std::vector<std::string> formats;
        JNIEnv* env = AndroidJni::GetEnv();
        if (!env || !clipboardManager) return formats;
        if (env->CallBooleanMethod(clipboardManager, midHasPrimaryClip)) {
            formats.push_back("text/plain");
        }
        AndroidJni::ClearException(env, "hasPrimaryClip");
        return formats;
    }

    bool UltraCanvasAndroidClipboard::IsFormatAvailable(const std::string& format) {
        if (format != "text/plain") return false;
        JNIEnv* env = AndroidJni::GetEnv();
        if (!env || !clipboardManager) return false;
        const bool has = env->CallBooleanMethod(clipboardManager, midHasPrimaryClip);
        return !AndroidJni::ClearException(env, "hasPrimaryClip") && has;
    }

} // namespace UltraCanvas
