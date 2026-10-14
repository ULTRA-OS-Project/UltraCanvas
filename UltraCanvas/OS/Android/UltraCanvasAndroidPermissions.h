// OS/Android/UltraCanvasAndroidPermissions.h
// Android runtime permissions (CAMERA, RECORD_AUDIO, ...) and the display
// rotation, for the parts of the framework that need a device: the video
// backend's camera capture today.
//
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_PERMISSIONS_H
#define ULTRACANVAS_ANDROID_PERMISSIONS_H

#include <functional>

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

    // Clockwise rotation of the display from the device's natural
    // orientation, in degrees (0, 90, 180, 270): what a camera frame must be
    // turned by, together with the sensor's own orientation. 0 when it cannot
    // be read (plain NativeActivity). Glue thread only.
    int DisplayRotationDegrees();

} // namespace AndroidPermissions
} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_PERMISSIONS_H
