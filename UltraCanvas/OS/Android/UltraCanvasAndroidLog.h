// OS/Android/UltraCanvasAndroidLog.h
// Routes the process's stdout and stderr into logcat.
//
// A native Android app has no console: stdout and stderr point at /dev/null
// (and on older releases are closed outright), so everything written to them
// is discarded. That silence covers three sources that matter during
// bring-up:
//
//   - the ~50 call sites in shared framework code that still write straight to
//     std::cout / std::cerr rather than through debugOutput,
//   - the platform backend's own EGL diagnostics, kept a near-copy of the
//     Linux context manager on purpose,
//   - and every warning the dependency stack emits - fontconfig complaining
//     about a missing cache directory, cairo about a surface it cannot
//     create, Pango about a font it cannot find. These are exactly the
//     failures a first run on a device hits, and none of them are ours to
//     rewrite.
//
// So the first thing android_main does is replace both file descriptors with
// a pipe and pump it into logcat. debugOutput does not come through here - it
// writes to logcat directly (UltraCanvasDebug.h) - which keeps framework
// diagnostics timestamped and controllable while still capturing everything
// else.
//
// Version: 1.0.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_LOG_H
#define ULTRACANVAS_ANDROID_LOG_H

namespace UltraCanvas {

    // Redirect stdout/stderr into logcat under the tag "UltraCanvas-stdio".
    //
    // Idempotent: the second and later calls do nothing and report the result
    // of the first. Returns false only when the pipe could not be created, in
    // which case the descriptors are left exactly as they were - a failure
    // here must never cost the app its output on a platform where it does
    // work.
    //
    // The reader thread is detached and lives for the process's lifetime;
    // there is no un-redirect, deliberately, since the descriptors it owns
    // outlive any caller that could ask for one.
    bool RedirectStdioToLogcat();

} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_LOG_H
