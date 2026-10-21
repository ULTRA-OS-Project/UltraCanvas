- **The notification registrations can be removed without the uninstaller.**
  Windows keeps, for each application whose notifications were shown, its
  name, icon and the program a click starts. The installer's uninstaller
  removes them; a folder unpacked from the ZIP package had no way to.
  - `ListNotificationRegistrations()` (`UltraCanvasNotificationActivation.h`)
    lists them; `RemoveNotificationRegistrations()` now returns how many it
    removed.
  - UOS-Settings 0.7.0: *Notifications > Registrations* lists them with a
    *Remove all* button, and `UOS-Settings --remove-notification-registrations`
    removes them with no window.
  - `package-win.sh` puts `uc-remove-registrations.cmd` in the ZIP beside the
    programs: it runs that command, to run before deleting the folder.
- **Every application that posts a notification registers its activator.**
  New `scripts/check_notification_activators.py`, run by CI
  (`notification-activators.yml`), reports an application under `Apps/` that
  posts a `system.notification` and never calls
  `RegisterNotificationActivator` - its Windows toast would do nothing once the
  broker host quit. UltraMail is the only application whose notifications
  reach the presenter today, and registers; the DemoApp's message-centre
  samples are posted Silent and say so (`notification-activator-exempt`). The
  DemoApp's toast page and the UltraMessage README (§3.9) show the call with
  the notification.
