// OS/Android/UltraCanvasAndroidDialogBridge.h
// Sync-over-async bridge to the Java dialogs in UltraCanvasActivity.
//
// The framework's dialog API is synchronous (ShowQuestion returns the answer),
// while every Android dialog is asynchronous and lives on the Java UI thread.
// The bridge closes that gap: it asks the activity to show the dialog, then
// pumps activity commands on the glue thread until the answer arrives. That is
// safe here in a way it would not be on a desktop UI thread - the framework
// runs on android_native_app_glue's own thread, so the Java main thread stays
// free to run the dialog and deliver the result.
//
// Version: 1.0.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_DIALOG_BRIDGE_H
#define ULTRACANVAS_ANDROID_DIALOG_BRIDGE_H

#include <string>

namespace UltraCanvas {
namespace AndroidDialogs {

    // Which button ended the dialog. Keep in sync with the RESULT_* constants
    // in UltraCanvasActivity.java.
    enum class JavaResult {
        Cancel   = 0,   // back button / dismissed / no UltraCanvasActivity
        Positive = 1,
        Negative = 2,
        Neutral  = 3
    };

    // Keep in sync with the ICON_* constants in UltraCanvasActivity.java.
    enum class JavaIcon { None = 0, Info = 1, Alert = 2 };

    struct JavaDialogOutcome {
        // false: this app runs a plain NativeActivity (no UltraCanvasActivity),
        // so nothing was shown and the caller must take its fallback path.
        bool bridged = false;
        JavaResult result = JavaResult::Cancel;
        std::string value;
    };

    // Show a modal AlertDialog and block until the user answers. A null label
    // omits that button. Returns bridged=false immediately when the Java
    // activity has no bridge method, or when the activity is being destroyed
    // while waiting.
    JavaDialogOutcome ShowMessage(const std::string& title,
                                  const std::string& message,
                                  JavaIcon icon,
                                  const char* positiveLabel,
                                  const char* negativeLabel,
                                  const char* neutralLabel);

    // Launch the system document picker (SAF) and block until the user picks
    // or cancels. `mimeTypesCsv` narrows the picker ("image/png,image/jpeg");
    // empty offers everything.
    //
    // On success `value` is the newline-separated list of paths the chosen
    // documents were COPIED to inside the app cache. SAF yields content://
    // URIs, which no POSIX call can open, so copying is what lets the
    // path-based framework API keep working with any provider - including
    // ones streaming from the network with no underlying file. Callers
    // therefore read a snapshot: writes to these paths do not reach the
    // original document.
    JavaDialogOutcome ShowOpenDocument(const std::string& mimeTypesCsv,
                                       bool allowMultiple);

    // Launch the system "create document" picker and, once the user chooses a
    // destination, write `data` to it. Blocks until both have happened.
    //
    // The bytes are handed over up front rather than through a path, because
    // SAF has no path to give: the document is only reachable through its
    // content:// URI and the app's ContentResolver. That is the whole reason
    // UltraCanvasNativeDialogs::SaveContent exists alongside SaveFile.
    JavaDialogOutcome ShowSaveDocument(const std::string& mimeType,
                                       const std::string& suggestedName,
                                       const void* data, std::size_t size);

} // namespace AndroidDialogs
} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_DIALOG_BRIDGE_H
