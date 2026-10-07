# UltraCanvasNotificationToast — Notifications on Screen

**Status:** Phase 2 of UltraMessage (the screen of last resort)
**Version:** 0.1.0
**Author:** UltraCanvas Framework / ULTRA OS
**Last Modified:** 2026-10-05

Where UltraMessage itself is the notification server — on ULTRA OS, and on a
Linux session with no notification daemon installed — every application's
notification arrives on the UltraMessage bus as a `system.notification`
(`Docs/Modules/UltraMessage/README.md` §3.6), and nothing else would draw it.
These two classes do. **Applications do not use them:** an application posts
a `system.notification` on UltraMessage, and the desktop's notification
service shows it - on ULTRA OS the toast host the desktop runs. There is one
host per bus (a second `Connect` is refused), so nothing is drawn twice; the
element on its own serves that host and previews such as the DemoApp page.

- **`UltraCanvasNotificationToast`** — one notification as an element: the
  application's icon and name, the summary, the body, the notification's own
  action buttons and a close button, with an accent stripe (red for a
  critical one). Built from catalogue elements only: `UltraCanvasLabel`,
  `UltraCanvasButton`, `UltraCanvasImageElement` in an `UltraCanvasContainer`.
- **`UltraCanvasNotificationToastHost`** — a client of the bus that shows each
  notification nothing else shows as a toast in its own
  `WindowType::Notification` window, stacked in a corner of the screen clear
  of the desktop's bars, and sends what the user does back on the bus.

**File location:** `include/Plugins/UltraMessage/UltraCanvasNotificationToast.h`,
`Plugins/UltraMessage/UltraCanvasNotificationToast.cpp`
**Library target:** `UltraMessageCenter` (with the message centre; links the
UltraCanvas library and `UltraMessage`)
**Used by:** the ULTRA OS desktop (`Apps/UltraDesktop`, top right beside its
right bar)
**Demo:** `Apps/DemoApp/UltraCanvasNotificationToastExamples.cpp` (Notification
Toast, next to Alert / Message Box): the element on the page, real toasts at
the screen corner, a download updating in place, and when to use a toast
rather than an Alert
**Tests:** `Tests/UltraMessage/test_toasts.cpp` (target
`UltraMessageCenterTests`, in-tree, headless)

---

## 1. What a toast looks like

```
┌──────────────────────────────────────────┐
▌ [icon] Telegram                        ×  │   application, close
▌ Ada Lovelace                              │   summary (bold, one line)
▌ Are we still on for the engine review     │   body (wrapped; long ones cut)
▌ at 9?                                     │
▌ [ Reply ]                                 │   the notification's actions
└──────────────────────────────────────────┘
```

340 px wide by default; the window fits its height to the text (up to 240 px).
A click on the summary or the body is the notification's `default` action;
each other action is a button (at most three); × dismisses.

## 2. Which notifications the host draws

`ShouldShow(message)` is the whole rule: a live `system.notification` that is
not `Silent` and has no `displayed` field. The broker writes `displayed` when a
presenter handed the notification to the desktop's own notification service
(`freedesktop-presenter`, `windows-presenter`, `macos-presenter`), and the notification adapters
when they read it from a service that drew it (`freedesktop-notifications` in
monitor mode, `windows-notification-listener`). So on GNOME, Plasma, dunst or
Windows the host draws nothing and nothing is shown twice; where UltraMessage
serves `org.freedesktop.Notifications` it draws everything — the native
applications' notifications and those UltraCanvas applications post (UltraMail's
new mail). A notice sent with `NoJournal` is still drawn: it is an alert that
the feed does not keep.

## 3. What happens to a toast

| Event | The toast | On the bus |
|---|---|---|
| A notification to draw arrives | shown on top of the stack; the oldest beyond the limit (4) goes | — |
| The same id again, or one sent with `UltraMsgFlag_Replace` naming it | updated in place, its time starts again | — |
| Its time is up (low 5 s, normal 8 s) | goes; a critical one stays until closed; the pointer resting on it holds it | — (the feed keeps the notification) |
| Click on the summary or body | goes | `system.notification.action` `{notificationId, actionId: "default"}` where the notification has a default action; otherwise nothing |
| An action button | goes | `system.notification.action` `{notificationId, actionId}` |
| × | goes | `system.notification.dismissed` `{notificationId, reason: "dismissed"}` |
| `system.notification.dismissed` or `.action` from anyone, `feed.dismissed` | goes | — |

The freedesktop adapter turns those notices into `ActionInvoked` and
`NotificationClosed` for the native application that sent the notification,
and UltraMail opens the mail its notification announced.

## 4. Using the host

```cpp
#include "Plugins/UltraMessage/UltraCanvasNotificationToast.h"
using namespace UltraCanvas;

UltraCanvasNotificationToastHost toasts;          // owned by the shell window
toasts.SetCorner(NotificationToastCorner::TopRight);
toasts.SetScreenMargins(0, 0, rightBarWidth, 0);  // logical px kept clear
if (!toasts.Connect())                            // hosts the broker when none runs
    debugOutput << "no notifications: " << toasts.LastError() << std::endl;
```

| Call | Purpose |
|---|---|
| `DefaultConnectOptions()`, `Connect(options)`, `Disconnect()`, `IsConnected()`, `LastError()` | The bus: app id `org.ultraos.notifications`, UI-thread delivery. |
| `ShouldShow(message)` | The rule of §2. |
| `Ingest(message)` | What the subscription calls; a host or a test may feed messages itself. |
| `Withdraw(id)`, `Dismiss(id)`, `InvokeAction(id, action)`, `ActivateBody(id)` | Take a toast away silently, or as its own controls do. |
| `Tick(nowMs)`, `Hold(id, nowMs)`, `NowMs()` | Expiry and the pointer's hold; the host runs a timer, tests pass their own clock. |
| `SetCorner`, `SetScreenMargins`, `SetMaxVisible`, `SetTimeouts(lowMs, normalMs)`, `SetStyle` | Placement and looks. |
| `SetWindowsEnabled(false)` | No windows: tests, or a host that draws the toasts itself from `onShown`. |
| `onShown`, `onAction`, `onDismissed` | Callbacks besides the bus posts. |
| `GetToasts()`, `GetToastCount()`, `GetToastWindow(id)` | What is on screen, newest first. |

`UltraCanvasNotificationToast` on its own: `SetContent(NotificationToastContent)`
(`NotificationToastContent::FromMessage` reads one from a message), `SetStyle`,
and the `onAction(actionId)`, `onClose`, `onHover` callbacks.

## 5. The window

Each toast is a `WindowType::Notification` window: undecorated, above the
other windows, on every virtual desktop, out of taskbars and pagers, and never
given the keyboard focus, so a toast never interrupts typing. On X11 that is
`_NET_WM_WINDOW_TYPE_NOTIFICATION` with `_NET_WM_STATE_ABOVE`, `STICKY`,
`SKIP_TASKBAR`, `SKIP_PAGER` and the input hint off; on Windows
`WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, shown without
activating; elsewhere a borderless window. Positions are computed in the
window's native geometry, as `CenterOnScreen` does, so they hold on HiDPI
screens.

## 6. Notes

- An icon is drawn when the notification names a file (a path or `file://`
  URI); an icon-theme name is not resolved yet.
- Earlier, `core/UltraCanvasToast.cpp` was an unfinished in-app toast draft
  that never compiled (its header was never committed); it was removed when
  this element replaced it.
