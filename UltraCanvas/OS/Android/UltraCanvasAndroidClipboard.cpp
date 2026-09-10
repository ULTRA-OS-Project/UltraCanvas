// OS/Android/UltraCanvasAndroidClipboard.cpp
// Android clipboard backend: android.content.ClipboardManager over JNI.
// Version: 1.1.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (same pattern as Linux).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidClipboard.h"
#include "UltraCanvasAndroidJni.h"
#include "UltraCanvasDebug.h"

#include <algorithm>
#include <cstdio>

namespace UltraCanvas {

    namespace {

        // Call a no-argument String method on UltraCanvasActivity. Returns
        // false when the method is not there at all - i.e. the app runs a
        // plain NativeActivity - which is not an error, just a fallback.
        bool CallActivityStringMethod(const char* name, std::string& out) {
            JNIEnv* env = AndroidJni::GetEnv();
            jobject activity = AndroidJni::GetActivity();
            if (!env || !activity) return false;

            jclass activityClass = env->GetObjectClass(activity);
            jmethodID mid = env->GetMethodID(activityClass, name,
                                             "()Ljava/lang/String;");
            env->DeleteLocalRef(activityClass);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();   // NoSuchMethodError: expected
                return false;
            }

            auto result = static_cast<jstring>(env->CallObjectMethod(activity, mid));
            if (AndroidJni::ClearException(env, name)) {
                // The glue thread stays attached for the process's life, so
                // its local refs are never popped by a returning frame: drop
                // this one explicitly rather than leak it once per poll.
                if (result) env->DeleteLocalRef(result);
                return false;
            }
            if (!result) return false;   // Java returned null: nothing to read

            out = AndroidJni::ToStdString(env, result);
            env->DeleteLocalRef(result);
            return !out.empty();
        }

        bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
            FILE* file = std::fopen(path.c_str(), "rb");
            if (!file) return false;
            uint8_t buffer[64 * 1024];
            std::size_t read;
            while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
                out.insert(out.end(), buffer, buffer + read);
            }
            std::fclose(file);
            return !out.empty();
        }

    } // namespace

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

    bool UltraCanvasAndroidClipboard::GetClipboardFiles(
            std::vector<std::string>& filePaths) {
        // Every content:// item on the clip, copied into the app cache by the
        // Java side because no POSIX call can open a content:// URI and this
        // API hands back paths. Callers therefore read a snapshot - the same
        // bargain the SAF file picker makes.
        std::string joined;
        if (!CallActivityStringMethod("getClipboardUriPaths", joined)) return false;

        filePaths = AndroidJni::SplitLines(joined);
        return !filePaths.empty();
    }

    bool UltraCanvasAndroidClipboard::GetClipboardImage(
            std::vector<uint8_t>& imageData, std::string& format) {
        // Ask what the clip claims to be before paying to copy it: an image
        // and an arbitrary file arrive through the same URI machinery, and
        // only the MIME type separates them.
        std::string mime;
        if (!CallActivityStringMethod("getClipboardMimeType", mime)) return false;
        if (mime.rfind("image/", 0) != 0) return false;

        std::vector<std::string> paths;
        if (!GetClipboardFiles(paths)) return false;

        if (!ReadWholeFile(paths.front(), imageData)) {
            debugOutput << "UltraCanvas Android clipboard: image URI copied to '"
                        << paths.front() << "' but could not be read" << std::endl;
            return false;
        }
        // "image/png" -> "png". A wildcard ("image/*", which some sources
        // advertise) names no decoder, so hand back nothing rather than "*"
        // and let the caller sniff the bytes it now has.
        format = mime.substr(6);
        if (format == "*") format.clear();
        return true;
    }

    std::vector<std::string> UltraCanvasAndroidClipboard::GetAvailableFormats() {
        std::vector<std::string> formats;
        JNIEnv* env = AndroidJni::GetEnv();
        if (!env || !clipboardManager) return formats;

        const bool has = env->CallBooleanMethod(clipboardManager, midHasPrimaryClip);
        if (AndroidJni::ClearException(env, "hasPrimaryClip") || !has) return formats;

        // What the clip actually advertises, in the same MIME spelling the
        // Linux backend reports its TARGETS atoms in. ClipboardManager alone
        // cannot say - that needs the ClipDescription, hence the activity -
        // and answering "text/plain" for a copied image would make
        // IsFormatAvailable lie about an image the caller could in fact read.
        std::string mime;
        if (CallActivityStringMethod("getClipboardMimeType", mime)) {
            formats.push_back(mime);
        } else {
            formats.push_back("text/plain");   // plain NativeActivity: text is all this backend does
        }
        return formats;
    }

    bool UltraCanvasAndroidClipboard::IsFormatAvailable(const std::string& format) {
        const std::vector<std::string> formats = GetAvailableFormats();
        return std::find(formats.begin(), formats.end(), format) != formats.end();
    }

} // namespace UltraCanvas
