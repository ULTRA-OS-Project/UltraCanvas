// OS/Android/UltraCanvasAndroidAssets.cpp
// Unpacks the APK's assets/ tree into the app sandbox on first launch.
// Version: 1.0.0
// Last Modified: 2026-09-01
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (same pattern as Linux).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidAssets.h"
#include "UltraCanvasAndroidJni.h"
#include "UltraCanvasDebug.h"

#include <android/asset_manager.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace UltraCanvas {

    namespace {

        constexpr const char* kStampFile = ".ultracanvas-assets";

        // mkdir -p. Returns false only if a path component exists as a
        // non-directory or cannot be created.
        bool MakeDirectories(const std::string& path) {
            if (path.empty()) return false;
            std::string partial;
            partial.reserve(path.size());
            for (std::size_t i = 0; i < path.size(); ++i) {
                partial += path[i];
                const bool last = (i + 1 == path.size());
                if (path[i] != '/' && !last) continue;
                if (partial == "/") continue;
                if (mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST) {
                    return false;
                }
            }
            struct stat st{};
            return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }

        // The installed APK's path, via Context.getPackageCodePath(). Its
        // mtime is what tells an update apart from an ordinary launch.
        std::string ApkPath(JNIEnv* env, jobject activity) {
            jclass activityClass = env->GetObjectClass(activity);
            jmethodID mid = env->GetMethodID(activityClass, "getPackageCodePath",
                                             "()Ljava/lang/String;");
            env->DeleteLocalRef(activityClass);
            if (AndroidJni::ClearException(env, "getPackageCodePath lookup")) return {};

            auto path = static_cast<jstring>(env->CallObjectMethod(activity, mid));
            if (AndroidJni::ClearException(env, "getPackageCodePath") || !path) return {};
            std::string result = AndroidJni::ToStdString(env, path);
            env->DeleteLocalRef(path);
            return result;
        }

        std::string CurrentStamp(JNIEnv* env, jobject activity) {
            const std::string apk = ApkPath(env, activity);
            struct stat st{};
            if (apk.empty() || stat(apk.c_str(), &st) != 0) return {};
            return std::to_string(static_cast<long long>(st.st_mtime)) + ":" +
                   std::to_string(static_cast<long long>(st.st_size));
        }

        std::string ReadStamp(const std::string& path) {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) return {};
            char buffer[128] = {};
            const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, f);
            std::fclose(f);
            return std::string(buffer, read);
        }

        bool WriteStamp(const std::string& path, const std::string& stamp) {
            FILE* f = std::fopen(path.c_str(), "wb");
            if (!f) return false;
            const bool ok = std::fwrite(stamp.data(), 1, stamp.size(), f) == stamp.size();
            return std::fclose(f) == 0 && ok;
        }

        bool CopyAssetToFile(AAssetManager* manager, const std::string& assetPath,
                             const std::string& destPath) {
            AAsset* asset = AAssetManager_open(manager, assetPath.c_str(),
                                               AASSET_MODE_STREAMING);
            if (!asset) return false;

            FILE* out = std::fopen(destPath.c_str(), "wb");
            if (!out) {
                AAsset_close(asset);
                return false;
            }

            bool ok = true;
            char buffer[64 * 1024];
            int read;
            while ((read = AAsset_read(asset, buffer, sizeof(buffer))) > 0) {
                if (std::fwrite(buffer, 1, static_cast<std::size_t>(read), out)
                        != static_cast<std::size_t>(read)) {
                    ok = false;
                    break;
                }
            }
            if (read < 0) ok = false;   // read error partway through

            if (std::fclose(out) != 0) ok = false;
            AAsset_close(asset);
            if (!ok) std::remove(destPath.c_str());   // never leave a half file
            return ok;
        }

        // Java AssetManager.list(), which - unlike the NDK's AAssetDir - also
        // reports subdirectories. That is the whole reason this walk needs
        // JNI at all; file *contents* still come from the native manager.
        std::vector<std::string> ListAssets(JNIEnv* env, jobject assetManager,
                                            jmethodID midList,
                                            const std::string& path) {
            std::vector<std::string> names;
            jstring jPath = env->NewStringUTF(path.c_str());
            auto array = static_cast<jobjectArray>(
                    env->CallObjectMethod(assetManager, midList, jPath));
            env->DeleteLocalRef(jPath);
            if (AndroidJni::ClearException(env, "AssetManager.list") || !array) {
                return names;
            }

            const jsize count = env->GetArrayLength(array);
            names.reserve(static_cast<std::size_t>(count));
            for (jsize i = 0; i < count; ++i) {
                auto entry = static_cast<jstring>(env->GetObjectArrayElement(array, i));
                if (entry) {
                    names.push_back(AndroidJni::ToStdString(env, entry));
                    env->DeleteLocalRef(entry);
                }
            }
            env->DeleteLocalRef(array);
            return names;
        }

        // Returns the number of files written, or -1 on a hard failure.
        int ExtractDirectory(JNIEnv* env, jobject assetManager, jmethodID midList,
                             AAssetManager* nativeManager,
                             const std::string& assetPath,
                             const std::string& destRoot) {
            int written = 0;
            for (const std::string& name : ListAssets(env, assetManager, midList, assetPath)) {
                if (name.empty() || name == "." || name == "..") continue;
                // An asset name containing '/' or '\' would let a crafted APK
                // write outside the destination; there is no legitimate one.
                if (name.find('/') != std::string::npos ||
                    name.find('\\') != std::string::npos) {
                    continue;
                }

                const std::string child =
                        assetPath.empty() ? name : assetPath + "/" + name;
                const std::string dest = destRoot + "/" + child;

                // Whether an entry is a file or a directory is not something
                // list() reports: try to open it, and recurse if it will not
                // open. (An empty directory opens as neither and is skipped,
                // which is correct - there is nothing to extract.)
                if (CopyAssetToFile(nativeManager, child, dest)) {
                    ++written;
                    continue;
                }
                if (!MakeDirectories(dest)) continue;
                const int sub = ExtractDirectory(env, assetManager, midList,
                                                 nativeManager, child, destRoot);
                if (sub < 0) return -1;
                written += sub;
            }
            return written;
        }

    } // namespace

    bool ExtractBundledAssets(android_app* app) {
        if (!app || !app->activity) return false;

        AAssetManager* nativeManager = app->activity->assetManager;
        if (!nativeManager) return false;

        const char* home = std::getenv("HOME");
        if (!home || !*home) {
            debugOutput << "UltraCanvas Android: no HOME; assets not extracted"
                        << std::endl;
            return false;
        }
        const std::string destRoot = std::string(home) + "/share";
        if (!MakeDirectories(destRoot)) {
            debugOutput << "UltraCanvas Android: cannot create " << destRoot
                        << std::endl;
            return false;
        }

        JNIEnv* env = AndroidJni::GetEnv();
        jobject activity = AndroidJni::GetActivity();
        if (!env || !activity) return false;

        // Skip the whole walk when this exact APK was already unpacked. A
        // reinstall or app update changes the stamp and re-extracts.
        const std::string stampPath = destRoot + "/" + kStampFile;
        const std::string stamp = CurrentStamp(env, activity);
        if (!stamp.empty() && ReadStamp(stampPath) == stamp) {
            return true;
        }

        jclass activityClass = env->GetObjectClass(activity);
        jmethodID midGetAssets = env->GetMethodID(
                activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
        env->DeleteLocalRef(activityClass);
        if (AndroidJni::ClearException(env, "getAssets lookup")) return false;

        jobject assetManager = env->CallObjectMethod(activity, midGetAssets);
        if (AndroidJni::ClearException(env, "getAssets") || !assetManager) return false;

        jclass managerClass = env->GetObjectClass(assetManager);
        jmethodID midList = env->GetMethodID(managerClass, "list",
                "(Ljava/lang/String;)[Ljava/lang/String;");
        env->DeleteLocalRef(managerClass);
        if (AndroidJni::ClearException(env, "AssetManager.list lookup")) {
            env->DeleteLocalRef(assetManager);
            return false;
        }

        const int written = ExtractDirectory(env, assetManager, midList,
                                             nativeManager, "", destRoot);
        env->DeleteLocalRef(assetManager);
        if (written < 0) return false;

        // Only stamp a complete extraction: a run interrupted partway must
        // start over rather than leave the app with half its resources.
        if (!stamp.empty() && !WriteStamp(stampPath, stamp)) {
            debugOutput << "UltraCanvas Android: assets extracted but stamping "
                           "failed; they will be extracted again next launch"
                        << std::endl;
        }
        debugOutput << "UltraCanvas Android: extracted " << written
                    << " asset file(s) to " << destRoot << std::endl;
        return true;
    }

} // namespace UltraCanvas
