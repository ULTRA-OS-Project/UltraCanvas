- **UltraMessage puts applications' notifications on screen.** A
  `system.notification` an application posts on the bus is now handed by the
  broker to a *presenter* adapter, which shows it with the desktop's own
  notification service - so it looks, sounds and obeys do-not-disturb like
  every other program's - and reports what the user does with it back on the
  bus: a click as `system.notification.action` (`actionId` `"default"` for
  the body), a close as `system.notification.dismissed`, both naming the
  notification. A dismissal posted on the bus withdraws it from the screen,
  and `UltraMsgFlag_Replace` updates it; `UltraMsgFlag_Silent` keeps it off
  screen, and what adapters publish (it came from the screen) is never
  presented. Until now the feed was the only place such a notification
  appeared. New hook: `Internal::IAdapter::Present`.
  - **Linux: `freedesktop-presenter`** calls `Notify` on whatever owns
    `org.freedesktop.Notifications` (GNOME Shell, Plasma, XFCE, dunst, mako),
    with the hints `category`, `urgency`, `desktop-entry` (the body's new
    optional `desktopEntry`), `sender-pid` and `x-ultramessage-id`, and turns
    `ActionInvoked` and a close by the user into the bus notices above. Where
    UltraMessage itself serves the name nothing draws notifications; the
    presenter then declines and its state says so, with the remedy.
  - **Windows: `windows-presenter`** shows a notification-area balloon
    (`Shell_NotifyIconW`), which Windows 10 and 11 present as a toast and keep
    in the Action Center - no package identity, shortcut or registration
    needed, so every desktop build has it. Focus assist is respected. The
    `windows-notification-listener` no longer reads those toasts back as a
    second notification.
  - **The `freedesktop-notifications` adapter no longer takes the name from
    an installed notification server.** dunst, mako and xfce4-notifyd start
    by D-Bus activation on the first `Notify`; the adapter used to claim the
    free name first, and from then on no application's notification was drawn
    on those desktops. It now starts such a server and watches it in monitor
    mode. It also skips the presenter's own `Notify` calls.
  - `UltraCanvasMessageCenter` lists only what the journal holds: a notice
    sent with `UltraMsgFlag_NoJournal` - a passing alert such as UltraMail's
    new-mail notification, whose messages are rows already - is no row.
  - The DemoApp's *Ultra Message* page sends its sample notifications Silent,
    so they stay on the page instead of popping up on the desktop.
  - `UltraMessage` links `shell32` and `user32` on Windows. Tests: two
    presenter cases against a fake desktop notification server on the private
    D-Bus session (36 in the suite); the message centre's NoJournal rule.
    UltraMail uses all this for its new-mail notification (UltraMail 0.10.34).
