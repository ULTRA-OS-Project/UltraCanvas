// core/UltraCanvasSessionLock.cpp
// The listener list of UltraCanvasSessionLock.h, and the backend of a platform
// that has none (ULTRACANVAS_HAS_NATIVE_SESSION_LOCK unset): it never starts,
// so nothing is reported there.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "UltraCanvasSessionLock.h"

#include <map>
#include <utility>

namespace UltraCanvas {

    namespace {
        // Everything here is touched on the UI thread only: listeners are
        // added and removed by elements, and backends report there.
        struct State {
            std::map<SessionLockListenerId, std::function<void(bool)>> listeners;
            SessionLockListenerId nextId = 1;
            bool watching = false;
            bool known = false;    // a state has been reported
            bool locked = false;   // ... and this was it
        };

        State& Shared() {
            static State state;
            return state;
        }
    }

    SessionLockListenerId AddSessionLockListener(std::function<void(bool locked)> onChange) {
        if (!onChange) return 0;
        State& s = Shared();
        const SessionLockListenerId id = s.nextId++;
        s.listeners.emplace(id, std::move(onChange));
        if (s.listeners.size() == 1) s.watching = SessionLockBackend::Start();
        return id;
    }

    void RemoveSessionLockListener(SessionLockListenerId id) {
        State& s = Shared();
        if (s.listeners.erase(id) == 0 || !s.listeners.empty()) return;
        if (s.watching) SessionLockBackend::Stop();
        s.watching = false;
        s.known = false;
    }

    bool IsWatchingSessionLock() {
        return Shared().watching;
    }

    void NotifySessionLockChanged(bool locked) {
        State& s = Shared();
        if (s.known && s.locked == locked) return;
        s.known = true;
        s.locked = locked;
        // A listener may remove itself (or another) while it is called.
        const auto listeners = s.listeners;
        for (const auto& [id, onChange] : listeners) {
            if (Shared().listeners.count(id) && onChange) onChange(locked);
        }
    }

#ifndef ULTRACANVAS_HAS_NATIVE_SESSION_LOCK
    namespace SessionLockBackend {
        bool Start() { return false; }
        void Stop() {}
    }
#endif

} // namespace UltraCanvas
