- **Notifications are drawn where nothing else draws them.** Where
  UltraMessage itself serves `org.freedesktop.Notifications` - on ULTRA OS,
  and on any Linux session with no notification daemon installed - every
  application's notification arrived on the bus and reached the message feed
  only; nothing put it on screen. The new `UltraCanvasNotificationToastHost`
  (`include/Plugins/UltraMessage/UltraCanvasNotificationToast.h`, target
  `UltraMessageCenter`) is the screen of last resort: a client of the bus that
  draws each live `system.notification` nothing else shows as a toast,
  stacked in a corner of the screen clear of the desktop's bars, and sends
  what the user does back - a click or an action button as
  `system.notification.action`, the close button as
  `system.notification.dismissed`, which the freedesktop adapter turns into
  `ActionInvoked` / `NotificationClosed` for the application. A toast goes
  after 8 s (5 s for low urgency; a critical one stays until closed; the
  pointer resting on it holds it), when it is dismissed or acted on anywhere
  on the bus, and a replacement updates it. The ULTRA OS desktop hosts it
  (UltraDesktop 0.2.0).
  - **For the desktop shell, not applications.** An application posts a
    `system.notification` and the message system decides who shows it; the
    host is the desktop's, one per bus - a second `Connect` is refused so
    nothing is drawn twice.
  - **`UltraCanvasNotificationToast`**, the toast as an element: the
    application's icon and name, summary, body, the notification's action
    buttons and a close button, from catalogue elements. In the element
    catalogue and documented in `Docs/UltraCanvas/UltraCanvasNotificationToast.md`.
  - **`WindowType::Notification`**: undecorated, above other windows, on
    every virtual desktop, out of taskbars and pagers, and never given the
    keyboard focus (`_NET_WM_WINDOW_TYPE_NOTIFICATION` with the input hint
    off on X11; `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, shown
    without activating, on Windows; borderless on macOS).
  - **Every notification says whether it is on screen.** The broker hands an
    application's notification to the presenters before it journals and
    delivers it and writes the presenter that took it into the body's
    `displayed` (an application's own value is removed); the
    `freedesktop-notifications` adapter in monitor mode and the
    `windows-notification-listener` write it on what the platform drew. A
    toast host draws only what carries none, so on GNOME, Plasma, dunst or
    Windows nothing is shown twice.
  - DemoApp: a *Notification Toast* page next to *Alert / Message Box* - the
    element on the page, real toasts at the screen corner (mail, a chat with
    Reply, a critical one, a download updating in place, six at once against
    the limit of four), what the clicks report, and when to use a toast
    rather than an Alert; the Alert page points at it.
  - The `freedesktop-presenter`'s state names the toast host for the case no
    notification server runs.
  - The Alert's documentation pointed transient messages at a "Toast
    (`UltraCanvasToast`)" that did not exist; it now names this element, and
    a critical toast takes the Error alert's red. A toast is not built on the
    Alert: that one is modal, centred, focused and answered by a button.
  - `core/UltraCanvasToast.cpp`, an unfinished in-app toast draft that never
    compiled (its header was never committed, and it called a rendering API
    that no longer exists), is removed: the element above replaces it.
  - Tests: six toast-host cases (which notifications it draws, update and
    replace, the visible limit, expiry and the pointer's hold, the controls,
    and a round trip on the private bus); `displayed` in the adapter and
    presenter tests. Checked by hand under Xvfb with no notification daemon:
    UltraDesktop drawing a native application's `Notify`, an UltraMail
    notification and a critical one, the clicks answering on both buses, and
    nothing drawn twice where dunst runs.
