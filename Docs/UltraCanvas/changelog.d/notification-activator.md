- **A click on a Windows toast reaches its application after the broker host
  quit.** The windows-presenter shows an application's notification as a
  toast under the application's own AppUserModelID, and its click came back
  over the bus only while the program hosting the broker ran. After that the
  toast waited in the Action Center, and a click on it did nothing.
  - New `UltraCanvasNotificationActivation.h` (doc
    `UltraCanvasNotificationActivation.md`): `RegisterNotificationActivator(appId,
    handler)` makes the program the COM activator of its AppUserModelID
    (`Software\Classes\CLSID\<clsid>\LocalServer32` names it with
    `-ToastActivated`, the AppUserModelID's `CustomActivator` names the class,
    a CLSID derived from the ID). Windows hands it the click, starting the
    program when it is not running; the handler runs on the UI thread.
    Windows backend `OS/MSWindows/UltraCanvasWindowsNotificationActivation.cpp`;
    a no-op elsewhere.
  - `SystemNotification` has an optional `launch`, the application's own note
    of what the notification is about (in the `system.notification` schema).
    The toast's activation arguments now carry the action, the notification's
    id and its `launch` (`FormatNotificationArguments`), so the activator
    needs nothing from the bus. UltraMail uses it: its toast opens the mail
    it announced (UltraMail 0.10.65).
  - Every identity is listed under `HKCU\Software\UltraCanvas\Notifications`.
    The installer's uninstaller walks the list and removes each
    AppUserModelID key and activator class, then the list;
    `RemoveNotificationRegistrations()` does the same from code.
  - Windows CI runs the UltraMessage suite (`UltraMessageTests`), which it
    built and never ran: the Windows presenter and listener against the
    runner's desktop.
  - Tested: the arguments, the CLSID and the `launch` round trip in
    `test_adapters.cpp`, UltraMail's launch string in `test_newmail.cpp`.
