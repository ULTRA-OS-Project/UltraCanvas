- **macOS: UltraMessage puts applications' notifications in Notification
  Center.** The new `macos-presenter` adapter
  (`OS/MacOS/UltraMessage/UltraMessageMacOSPresenter.mm`, in every macOS build)
  hands a `system.notification` an application posts on the bus to
  Notification Center through the UserNotifications framework, so UltraMail's
  new-mail notification now shows on macOS as it does on Linux and Windows.
  Until now there was no presenter on macOS and such a notification reached
  the message feed only.
  - A banner that stays in the Notification Center list. The notification's
    other actions become buttons (up to three). On macOS 12 and later
    `critical` is time-sensitive and `low` is passive and silent. An icon
    that names a PNG, JPEG or GIF file becomes the notification's picture,
    and one application's notifications are grouped. An update
    (`UltraMsgFlag_Replace`) changes the notification in place.
  - What the user does comes back on the bus naming the notification: a
    click as the `default` action where the notification has one, a button
    as its action, a dismissal as `system.notification.dismissed`. A
    dismissal or action posted on the bus removes it from Notification
    Center. Each notification carries its bus id, so a click on one shown
    before the broker last started is still reported.
  - macOS names the notification after the application bundle that hosts the
    broker and asks the user once to allow it. Another application's
    notification names that application in its subtitle. Where the user did
    not allow notifications, the adapter's state is `needs-permission` and
    names the place in System Settings.
  - **Outside an application bundle** (an executable started from the build
    tree, `ultramsgd`, a test), or where macOS refuses the bundle, the
    adapter shows notifications through `osascript`'s
    `display notification` (mode `script`): under Script Editor's name,
    without buttons, and a click on one opens nothing. The texts are passed
    as script arguments, never inside the script.
  - **The presenters' shared half** moves into `UltraMessageAdapter.h` and is
    tested on every platform: `ReadPresentedContent` (what a notification
    says, its buttons, its icon file), `ButtonSetKey`, `PresentedNotifications`
    (which update shows where) and `PublishPresenterResponse` (what a click,
    a button or a dismissal publishes).
  - `UltraMessage` links the Foundation and UserNotifications frameworks on
    macOS and exports `ULTRAMESSAGE_HAVE_MACOS_PRESENTER`. Tests: five cases
    for the shared half on every platform (41 in the suite on Linux), and on
    macOS one that lists the adapter and switches it off and on.
