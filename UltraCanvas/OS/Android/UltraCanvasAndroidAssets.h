// OS/Android/UltraCanvasAndroidAssets.h
// Unpacks the APK's assets/ tree into the app sandbox on first launch.
//
// Everything the framework loads at runtime - bundled fonts, icons, media,
// per-app resources - is opened by path. Inside an APK those files are not on
// the filesystem at all: they live compressed in the archive, reachable only
// through AAssetManager, so every fopen() would fail. Extracting once to
// $HOME/share (exactly where SetResourcesDir's Android arm points) keeps all
// that path-based code working unchanged.
//
// Version: 1.0.0
// Last Modified: 2026-09-01
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_ASSETS_H
#define ULTRACANVAS_ANDROID_ASSETS_H

struct android_app;   // android_native_app_glue

namespace UltraCanvas {

    // Extract assets/ into $HOME/share unless an earlier run of this same APK
    // already did. Returns false if nothing could be extracted (no HOME, no
    // asset manager, unwritable destination); an app with no assets is a
    // success, not a failure.
    //
    // Safe to call when the APK has no assets at all, and cheap on every
    // launch after the first: it compares a stamp against the APK's
    // modification time, so a reinstall or an app update re-extracts while a
    // normal launch does not.
    bool ExtractBundledAssets(android_app* app);

} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_ASSETS_H
