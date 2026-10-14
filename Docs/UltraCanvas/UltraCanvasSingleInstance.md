# UltraCanvasSingleInstance

Lets an application run **one copy per user**. A second start does not open
another window: it asks the copy already running to come forward, handing it
whatever this start was asked for, and quits.

```cpp
#include "UltraCanvasSingleInstance.h"

int main() {
    UltraCanvasSingleInstance instance("org.ultraos.deviceexplorer", "DeviceExplorer");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return 0;   // the running copy came forward

    UltraCanvasApplication app;
    if (!app.Initialize("DeviceExplorer")) return 1;
    if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
        return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "DeviceExplorer");

    // ... create and show the main window ...
    instance.ListenRaise();   // a later start brings the main window forward
    app.Run();
    instance.Close();         // before the windows go
    return 0;
}
```

And in the application's `CMakeLists.txt`, after its `add_executable`:

```cmake
ultracanvas_single_instance(DeviceExplorer)
```

## How it decides

**Who runs** is decided by an operating-system lock
([UltraCanvasInstanceLock](UltraCanvasInstanceLock.md)) on
`<settings folder>/instances/<appId>.lock` — or in the folder `Claim()` is
given, as UltraMail does with its data folder. The system drops the lock when
the process ends, however it ends, so two starts at the same moment can never
both stay, and a crash never blocks the next start.

**Coming forward** travels over UltraMessage, the per-user message bus: a
request on the topic `<appId>.show` to the running copy's endpoint
`<appId>.instance`, which answers before it acts. The first copy hosts the bus
when nothing else does. A copy started a moment earlier may not listen yet, so
a later start keeps asking for up to ten seconds.

| `Claim()` answers | Meaning |
|---|---|
| `First` | This process holds the lock: carry on. Also the answer when the lock cannot be taken for any other reason (an unwritable folder, a platform without locks) — better two copies than none. |
| `HandedOff` | Another copy runs and took the request: quit. |
| `AlreadyRunning` | Another copy holds the lock but did not answer (busy, or a build without UltraMessage): say so with `RunAlreadyRunningAlert`, then quit. |

## API

| Call | Meaning |
|---|---|
| `UltraCanvasSingleInstance(appId, displayName)` | `appId` is reverse-DNS (`"org.ultraos.deviceexplorer"`); the lock file and the bus topic are named after it. `displayName` is what the alert says. |
| `Claim(request = {}, lockDirectory = "")` | Takes the lock; when another copy holds it, offers it `request` (a JSON object). Call **after** the command line is read — a mode that prints and exits (`--help`, `--list`) must not claim — and **before** `Initialize`, so a second start costs no window system. |
| `Listen(handler)` | A later start's request arrives in `handler(const JSONValue&)` on the UI thread. UltraClipboard reads `search` and `editId` from it. |
| `ListenRaise()` | `Listen` with `BringToFront()` as the handler — what a single-window application needs. |
| `Close()` | Stops listening. The lock stays until the object goes, so no second copy slips in while this one shuts down. |
| `BringToFront()` | Restores and raises the first window that is not a dialog, or the open dialog, which keeps what is typed in it. |
| `RunAlreadyRunningAlert(app, displayName)` | *"<displayName> is already running."*, then runs the event loop until it is dismissed. Returns `EXIT_SUCCESS`. |

## Why it is compiled into the application

The hand-off needs UltraMessage, a separate module the core library does not
link. `ultracanvas_single_instance(<target>)`
(`cmake/UltraCanvasSingleInstance.cmake`) therefore compiles
`core/AppEntry/UltraCanvasSingleInstance.cpp` into the executable and links
UltraMessage where the build has it. Without UltraMessage the lock still stops
a second copy, and `Claim()` answers `AlreadyRunning` rather than `HandedOff`.

## Who uses it

UltraMail (lock in its data folder: one copy per mailbox), UltraClipboard
(hands over the search and the entry to edit), DeviceExplorer, UOS-Settings,
UltraNetMonitor, UltraPassword, UltraAuthenticator, UltraCleaner,
EmailCleaner, UltraClaude, the UltraAI dashboard, UltraSocial, UltraFIBU and
UltraWin Manager. Command-line tools (`ultramsg`, `anchorpoint`, `ultrafibu`)
do not: scripts run several of them at once. Applications that open documents
(Texter, UltraPaint, UltraViewer, UltraFiler, UltraVideo, ArtCreator,
AnchorPoint) are not single-instance yet — they need the running copy to open
the second start's files, which is a hook of their own.
