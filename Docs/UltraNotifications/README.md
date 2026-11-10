# UltraNotifications

The notifications of ULTRA OS after their pop-up has gone. UltraDesktop
draws each notification as it arrives - a toast in the top-right corner -
and the toast goes after a few seconds; UltraNotifications is where it stays:
every notification, chat message and mail that reached the UltraMessage
feed, to read again, search, open and dismiss.

The window is the framework's
[UltraCanvasMessageCenter](../UltraCanvas/UltraCanvasMessageCenter.md)
filling it: the program adds the window, the background start, the unread
count for the desktop and what *Open* does.

## Contents

- [Using it](#using-it)
- [In the background](#in-the-background)
- [The bell on the desktop](#the-bell-on-the-desktop)
- [Command line](#command-line)
- [Building](#building)

## Using it

| Part | What it does |
|---|---|
| *All / Chats / Mail / System* | the feed's three kinds, or all of them |
| Sources (left) | each chat, mail account and application, with its unread count |
| Search, *Unread*, the service chips | narrow the list |
| The list | newest first; unread ones marked, urgent ones in red |
| The detail (right) | the whole text; *Open*, *Mark read / unread*, *Dismiss*, and the notification's own buttons |

Selecting a row marks it read. *Open* (or a double-click) opens a mail in
UltraMail, a ChatCube chat in ChatCube, and does a notification's first
action. *Dismiss* removes the row and, for a notification, closes its
pop-up too.

## In the background

`UltraNotifications --background` starts it with no window: it connects to
the UltraMessage bus, keeps the list and counts the unread. The next start -
the bell, the Applications window, `UltraNotifications` - shows the window at
once. While it runs this way, closing the window only hides it; *Quit* at the
foot of the window, or Ctrl+Q, ends the program.

UltraDesktop starts it so when *Start with the desktop* names it - the
settings button's right-click menu, or UOS-Settings > Desktop. One copy runs
per user (UltraCanvasSingleInstance): a second start brings the running one
forward, and a second background start changes nothing.

## The bell on the desktop

While it runs, UltraNotifications publishes its unread count as a desktop
notice (`UltraCanvasDesktopShell::PublishNotice("UltraNotifications", …)`);
UltraDesktop shows it on the bell on its right bar and opens
UltraNotifications from there. When the program ends the notice goes, and
the bell shows no count.

## Command line

```
UltraNotifications [--background]
UltraNotifications --version | --help
```

## Building

`BUILD_ULTRANOTIFICATIONS` (on by default) builds it wherever the
`UltraMessageCenter` library is built. It is packaged with the other
applications on Linux, Windows and macOS.
