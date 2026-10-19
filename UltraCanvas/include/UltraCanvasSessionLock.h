// include/UltraCanvasSessionLock.h
// The desktop session's lock: a call when the screen locks - the user locked
// it, or stepped away and the screensaver did - and when it unlocks again.
// What a program holding something it should not keep past the user's
// absence (a password, a decrypted document on screen) listens to:
//
//   auto id = AddSessionLockListener([this](bool locked) {
//       if (locked) ForgetPasswords();
//   });
//   ...
//   RemoveSessionLockListener(id);
//
// Listeners are called on the UI thread, once per change (a lock reported by
// two sources is one call). The platform is watched only while a listener is
// registered. Backends:
//   Linux, the BSDs  logind's Lock / Unlock signals and LockedHint for this
//                    session (system bus) and the screensaver's ActiveChanged
//                    (org.freedesktop.ScreenSaver, org.gnome.ScreenSaver;
//                    session bus), through GDBus on the application's loop
//   Windows          WTS session notifications (WTS_SESSION_LOCK / UNLOCK)
//                    on a hidden window
//   macOS            the com.apple.screenIsLocked / screenIsUnlocked
//                    distributed notifications
// Where none of these answers - another platform, no bus, no session - nothing
// is reported and IsWatchingSessionLock() is false: a caller that must not
// depend on it keeps an idle timeout of its own.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <functional>

namespace UltraCanvas {

    using SessionLockListenerId = std::uint64_t;

    // `onChange(true)` when the session locks, `onChange(false)` when it
    // unlocks. Returns the id RemoveSessionLockListener takes; 0 for no
    // callback.
    SessionLockListenerId AddSessionLockListener(std::function<void(bool locked)> onChange);
    void RemoveSessionLockListener(SessionLockListenerId id);

    // True while a backend is watching the session (a listener is registered
    // and the platform answered).
    bool IsWatchingSessionLock();

    // Tells every listener the session locked or unlocked - what a backend
    // calls, on the UI thread. A host with a lock source of its own (a kiosk's
    // own screen lock) calls it too, and so do tests. A repeat of the state
    // already reported calls nobody.
    void NotifySessionLockChanged(bool locked);

    // ===== PLATFORM BACKEND (OS/<Platform>/UltraCanvas*SessionLock.*) =====
    // Started with the first listener and stopped with the last. Start()
    // returns false where nothing can be watched; a platform without a
    // backend gets the one in core/UltraCanvasSessionLock.cpp, which never
    // starts.
    namespace SessionLockBackend {
        bool Start();
        void Stop();
    }

} // namespace UltraCanvas
