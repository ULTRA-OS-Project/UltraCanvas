// OS/Android/UltraCanvasAndroidClipboard.h
// Android clipboard backend: android.content.ClipboardManager over JNI.
// Text only - images and file lists report unsupported (Android's clipboard
// carries them as content:// URIs, which need the SAF adapter of a later
// phase). Change detection uses ClipDescription.getTimestamp() (API 26+,
// matching the backend's android-26 floor).
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_CLIPBOARD_H
#define ULTRACANVAS_ANDROID_CLIPBOARD_H

#include "UltraCanvasClipboard.h"

#include <jni.h>
#include <cstdint>

namespace UltraCanvas {

    class UltraCanvasAndroidClipboard : public UltraCanvasClipboardBackend {
    public:
        UltraCanvasAndroidClipboard() = default;
        ~UltraCanvasAndroidClipboard() override;

        bool Initialize() override;
        void Shutdown() override;

        bool GetClipboardText(std::string& text) override;
        bool SetClipboardText(const std::string& text) override;

        // Reading non-text formats needs this app's ContentResolver, so it goes
        // through UltraCanvasActivity (see the .cpp). Without that activity
        // these report "nothing there", exactly as the dialogs fall back.
        bool GetClipboardImage(std::vector<uint8_t>& imageData, std::string& format) override;
        bool GetClipboardFiles(std::vector<std::string>& filePaths) override;

        // Writing them is a different problem and stays unimplemented: handing
        // another app a file means publishing a content:// URI it may read,
        // which requires a ContentProvider declared in the *application's*
        // manifest - framework code cannot supply one on the app's behalf.
        // Reporting false is honest; a plain path put on the clipboard would
        // be unreadable to every other app and look like data loss.
        bool SetClipboardImage(const std::vector<uint8_t>&, const std::string&) override { return false; }
        bool SetClipboardFiles(const std::vector<std::string>&) override { return false; }

        bool HasClipboardChanged() override;
        void ResetChangeState() override;

        std::vector<std::string> GetAvailableFormats() override;
        bool IsFormatAvailable(const std::string& format) override;

    private:
        // Current primary clip's timestamp (ms since epoch), or 0 when empty/
        // unreadable. Android 10+ denies reads while the app lacks input
        // focus - callers just see "unchanged"/"no text" then.
        int64_t QueryClipTimestamp();

        // Cached global refs, created in Initialize(). All java.* / android.*
        // classes resolve through the system class loader, which attached
        // native threads use for FindClass.
        jobject clipboardManager = nullptr;   // android.content.ClipboardManager
        jclass clipDataClass = nullptr;       // android.content.ClipData
        jmethodID midSetPrimaryClip = nullptr;
        jmethodID midGetPrimaryClip = nullptr;
        jmethodID midHasPrimaryClip = nullptr;
        jmethodID midGetPrimaryClipDescription = nullptr;
        jmethodID midNewPlainText = nullptr;  // static ClipData.newPlainText
        jmethodID midGetItemCount = nullptr;
        jmethodID midGetItemAt = nullptr;
        jmethodID midCoerceToText = nullptr;  // ClipData.Item.coerceToText(Context)
        jmethodID midCharSeqToString = nullptr;
        jmethodID midGetTimestamp = nullptr;  // ClipDescription.getTimestamp

        int64_t lastSeenTimestamp = 0;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_CLIPBOARD_H
