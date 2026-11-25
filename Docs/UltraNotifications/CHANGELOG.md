#### 2026-11-25 *0.1.2*
- **Started again from a terminal, UltraNotifications answers in your
  language.** "UltraNotifications is already running: brought it forward." -
  and the plain "... is already running." of a `--background` start - were
  English whatever the language. They come from the framework's standard set
  now (`UltraCanvasSingleInstance::HandOffMessage`).

#### 2026-11-14 *0.1.1*
- **Dark look**: the window, the Quit footer and the message centre (list,
  sources, detail, the unread and urgent marks) take the system look instead
  of fixed light colours, and follow a change of look while it runs.

#### 2026-11-06 *0.1.0*
- **UltraNotifications, the notifications of ULTRA OS after their pop-up
  has gone.** Every notification, chat message and mail that reached the
  UltraMessage feed, in one window: *All*, *Chats*, *Mail* and *System*,
  the sources they came from, a search, *Unread* and one filter per
  service, and the selected one in full with *Open*, *Mark unread*,
  *Dismiss* and the notification's own buttons. *Open* on a mail opens
  UltraMail, on a ChatCube chat ChatCube, on a notification what it
  offers. Dismissing a notification here also closes its pop-up.
- **It can run without a window.** `UltraNotifications --background`
  (UltraDesktop's *Start with the desktop*) connects and counts the
  unread, which the desktop's bell shows; the next start shows the window
  at once. Closing the window then only hides it; *Quit* or Ctrl+Q ends
  the program. One copy runs per user: a later start brings it forward.
