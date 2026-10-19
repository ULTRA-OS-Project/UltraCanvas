# UltraCanvasSessionLock

<!-- doc-check: void ForgetPasswords(); void ShowLockedAgain(); -->

A call when the desktop session locks — the user locked the screen, or
stepped away and the screensaver did — and when it unlocks again. It is what
a program holding something it should not keep past the user's absence
listens to: a password typed earlier, a decrypted document on screen.

```cpp
#include "UltraCanvasSessionLock.h"

SessionLockListenerId listener = AddSessionLockListener([this](bool locked) {
    if (locked) {
        ForgetPasswords();
        ShowLockedAgain();
    }
});
// ... and before `this` goes:
RemoveSessionLockListener(listener);
```

The [media viewer](UltraCanvasMediaViewer.md) is the first user: while it
keeps a password Unlock was given, or shows a file opened with one, a lock
forgets the passwords and puts the file back behind its Unlock button.
UltraPaint listens while a drop of protected files holds the passwords given
for it, and forgets them, so the files still to come ask again.

A listener is best registered only while there is something to forget, as
both do: the platform is watched only while one is registered.

## Calls

| Call | Meaning |
|---|---|
| `AddSessionLockListener(onChange)` | `onChange(true)` when the session locks, `onChange(false)` when it unlocks, on the UI thread. Returns the id to remove it by; 0 for an empty callback. The first listener starts the platform backend. |
| `RemoveSessionLockListener(id)` | Removes it; the last one stops the backend. Safe from inside a callback (its own or another's) and safe twice. |
| `IsWatchingSessionLock()` | True while a backend is watching: a listener is registered and the platform answered. |
| `NotifySessionLockChanged(locked)` | Tells every listener — what a backend calls. A host with a lock of its own (a kiosk's screen lock) calls it too, and so do tests. |

Each change is reported once. A lock usually arrives from two sources at once
— logind's signal and the screensaver's, or the signal and the property that
follows it — and the second, saying the same thing, calls nobody.

## Where it comes from

| Platform | Source |
|---|---|
| Linux, the BSDs | logind (system bus): the `Lock` / `Unlock` signals of this process's session and its `LockedHint` property, which the screen locker sets while it shows. The screensaver (session bus): the `ActiveChanged` signal (a boolean) of `org.freedesktop.ScreenSaver` (KDE, Xfce and most others) and `org.gnome.ScreenSaver` (GNOME). Either is enough. GDBus runs on a private context the application's loop watches — no thread of its own. |
| Windows | WTS session notifications (`WTS_SESSION_LOCK`, `WTS_SESSION_UNLOCK`) on a message-only window of the UI thread; `wtsapi32` is loaded when the first listener arrives. |
| macOS | The loginwindow's `com.apple.screenIsLocked` / `com.apple.screenIsUnlocked` distributed notifications, on the main queue. |

Everything else — Android, WebAssembly, a Linux session with no bus — reports
nothing, and `IsWatchingSessionLock()` stays false. A caller that must not
depend on a lock being reported keeps an idle timeout of its own as well; the
media viewer forgets after ten minutes without input either way.

On Linux the session is the caller's own (logind's `GetSession` asked for
`"auto"`, which for a program started from a user service is the user's
graphical session), falling back to the one its process is in. The screensaver's signal is taken from any
sender on any path, since each desktop emits it on a path of its own; a forged
one can do no more than make a listener forget.

## Testing

`NotifySessionLockChanged(true)` is a lock as far as every listener can tell,
so a test needs no desktop. The backend itself is tested by
`Tests/SessionLockTest.cpp`, which CTest runs under `dbus-run-session` and
which sends the screensaver's signal with `gdbus`:

```sh
gdbus emit --session --object-path /org/gnome/ScreenSaver \
           --signal org.gnome.ScreenSaver.ActiveChanged true
```

## Building

`ULTRACANVAS_HAS_NATIVE_SESSION_LOCK` (top-level `CMakeLists.txt`) is set for
Windows, macOS and desktop Unix, and is a PUBLIC define of the library. Without
it `core/UltraCanvasSessionLock.cpp` compiles a backend that never starts. The
backends are `OS/Linux/UltraCanvasLinuxSessionLock.cpp` (gio, which GTK
already brings), `OS/MSWindows/UltraCanvasWindowsSessionLock.cpp` and
`OS/MacOS/UltraCanvasMacOSSessionLock.mm`.
