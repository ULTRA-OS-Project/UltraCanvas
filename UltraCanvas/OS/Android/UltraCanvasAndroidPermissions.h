// OS/Android/UltraCanvasAndroidPermissions.h
// Android runtime permissions (CAMERA, RECORD_AUDIO, ...), access to every
// file on the shared storage, and the display rotation, for the parts of the
// framework that need a device: the video backend's camera capture, a file
// manager (MobilePC's UltraFiler).
//
// Version: 1.2.0 - HasAllFilesAccess, RequestAllFilesAccess, SharedStoragePath
// Version: 1.1.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_PERMISSIONS_H
#define ULTRACANVAS_ANDROID_PERMISSIONS_H

#include <functional>
#include <string>

namespace UltraCanvas {
namespace AndroidPermissions {

    // Full permission names, as the manifest spells them.
    constexpr const char* Camera = "android.permission.CAMERA";
    constexpr const char* RecordAudio = "android.permission.RECORD_AUDIO";

    // Context.checkSelfPermission. Works with a plain NativeActivity too.
    // Glue thread only (it uses the glue thread's JNIEnv).
    bool IsGranted(const char* permission);

    // Show the system's permission prompt and report the answer through
    // `callback`, on the UI (glue) thread via PostToUIThread. Answers true at
    // once when the permission is already held. Needs UltraCanvasActivity,
    // whose onRequestPermissionsResult carries the answer back; with a plain
    // NativeActivity there is no way to hear it, so the callback gets false.
    // The permission must also be declared in the app's manifest, or Android
    // denies it without asking. Glue thread only.
    void Request(const char* permission, std::function<void(bool granted)> callback);

    // ----- every file on the shared storage -----
    // What a file manager needs to read and write the phone's files by path
    // (/storage/emulated/0/...): "All files access" from Android 11
    // (MANAGE_EXTERNAL_STORAGE, switched on by the user in Settings), the
    // storage runtime permission before. The app's manifest must declare
    // MANAGE_EXTERNAL_STORAGE, and WRITE_EXTERNAL_STORAGE with
    // android:maxSdkVersion="29" (Apps/MobilePC/android/AndroidManifest.xml).
    // Every call: glue thread only; false / "" without UltraCanvasActivity.
    bool HasAllFilesAccess();

    enum class FilesAccessRequest {
        Granted,          // held already: nothing was shown
        Prompted,         // before Android 11: the permission prompt, answered through the callback
        SettingsOpened,   // Android 11 on: Settings' switch, on the phone's screen; no answer comes -
                          // HasAllFilesAccess says what the user chose once they are back
        Unavailable       // no UltraCanvasActivity, or no way to ask on this device
    };
    // Asks for it as the Android version wants. `onPromptAnswer` is called
    // (on the UI thread) only for Prompted.
    FilesAccessRequest RequestAllFilesAccess(std::function<void(bool granted)> onPromptAnswer = nullptr);

    // The shared storage's top folder - /storage/emulated/0 on most devices -
    // or "" when it cannot be told.
    std::string SharedStoragePath();

    // Clockwise rotation of the display from the device's natural
    // orientation, in degrees (0, 90, 180, 270): what a camera frame must be
    // turned by, together with the sensor's own orientation. Asks the display
    // now (JNI) and updates the value CurrentDisplayRotationDegrees returns.
    // 0 when it cannot be read. Glue thread only.
    int DisplayRotationDegrees();

    // The display rotation as last seen, from any thread - what a camera
    // frame arriving on the camera's own thread is turned by. Kept current
    // by UltraCanvasActivity's display listener (every turn, including the
    // 180-degree flips that raise no configuration change) and by the
    // application re-reading it on each configuration change and resize.
    int CurrentDisplayRotationDegrees();

} // namespace AndroidPermissions
} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_PERMISSIONS_H
