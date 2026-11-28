# UltraCanvasLaunchAtLogin

Starting a program when the user signs in to the computer: what a settings
page offers as *Start: manually / when the computer starts*. One call puts the
program in the computer's own list of programs that start at sign-in, or
takes it out, on Linux and the BSDs, Windows and macOS.

```cpp
#include "UltraCanvasLaunchAtLogin.h"

LaunchAtLoginEntry entry;
entry.id = "org.ultraos.ChatCube";       // the entry's name in the list
entry.displayName = "ChatCube";
entry.iconName = "ChatCube";             // the Linux entry's Icon=
entry.arguments = {"--minimized"};       // started in the background
// entry.executable empty: this program

std::string error;
if (!SetLaunchAtLogin(entry, true, error)) {
    // error: a short, user-readable reason; the list is as it was
}
bool startsWithComputer = IsLaunchAtLoginEnabled(entry.id);
```

ChatCube's *Settings > General > Startup* is the first user: *Manually* or
*When the computer starts*, the latter with `--minimized`, so messages
arrive from sign-in on and the window waits.

## The computer's list is the record

Nothing is kept in the program's own settings. Ask `IsLaunchAtLoginEnabled`
each time the choice is shown: an entry the user switched off where the
computer keeps the list - the Task Manager's *Startup* tab, macOS's *Login
Items*, the desktop's autostart settings - reads as off, and the page shows
what will really happen at the next sign-in.

## Calls

| Call | Meaning |
|---|---|
| `LaunchAtLoginAvailable()` | Whether this platform, and this kind of install, can start a program at sign-in. False on Android, iOS and WebAssembly, in a Flatpak or a Snap, and in an MSIX package - leave the choice out, or show it disabled with a word on why. |
| `IsLaunchAtLoginEnabled(id)` | Whether an entry of that name is in the list and switched on. |
| `SetLaunchAtLogin(entry, enabled, error)` | Adds the entry (replacing one of the same `id`) or takes it out. False with a reason in `error` when it could not; the list is then unchanged. Taking out an entry that is not there succeeds. |
| `LaunchAtLoginCommand(entry)` | The command line the entry starts, for a settings page to show ("Started as: ..."). Empty where nothing is available. |

`LaunchAtLoginEntry`:

| Field | Meaning |
|---|---|
| `id` | The entry's name: the `.desktop` file's, the registry value's, the LaunchAgent's label. Reverse-DNS by convention; letters, digits, `.`, `-` and `_` only (anything else is refused). |
| `displayName` | The program's name, where the desktop lists the entries. |
| `comment`, `iconName` | The Linux entry's `Comment=` and `Icon=` (optional). |
| `executable` | The program to start (UTF-8). Empty: this program - inside an AppImage the AppImage (`$APPIMAGE`, since the mounted copy is gone once it ends), inside a macOS `.app` bundle the bundle. |
| `arguments` | Its arguments, each passed as one argument whatever it holds (spaces, quotes, `%`). |

## Backends

| Platform | Where the entry goes |
|---|---|
| Linux, BSD | An XDG autostart entry, `$XDG_CONFIG_HOME/autostart/<id>.desktop` (`~/.config/autostart`), which GNOME, KDE, Xfce, LXQt, Cinnamon, MATE and UltraDesktop's session start. `Hidden=true` or `X-GNOME-Autostart-enabled=false` reads as off. An entry of the same name the system installs (`$XDG_CONFIG_DIRS/autostart`, `/etc/xdg/autostart`) is switched off with a `Hidden=true` entry in the user's folder. |
| Windows | A value named `<id>` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, the program's path quoted. The Task Manager's *Startup* tab keeps its own on / off under `Explorer\StartupApproved\Run`: an entry switched off there reads as off, and switching it on here clears that. |
| macOS | A LaunchAgent, `~/Library/LaunchAgents/<id>.plist`, with `RunAtLoad`. It is not loaded at once (that would start a second copy now), only at the next sign-in. A program inside an `.app` bundle starts through `open -a <bundle>`, so it starts as an application. |
| Android, iOS, WebAssembly | None: `LaunchAtLoginAvailable()` is false and `SetLaunchAtLogin` fails (the fallback in `core/UltraCanvasLaunchAtLogin.cpp`). |

Not available, and so reported: a Flatpak or a Snap, whose config folder is
their own (the Background portal is what those would need), and an MSIX
package, whose `HKCU` writes stay inside the package (a `StartupTask` in its
manifest is what that would need).

## A program started at sign-in

Start it unseen - minimized, or to the tray - with an argument of its own,
as ChatCube's `--minimized`: a window opening in the middle of signing in is
in the way. A single-instance program (`ultracanvas_single_instance`) that
the user then opens by hand only brings the running copy forward.

The entry names the program where it is now. A program that moves (an
AppImage downloaded again to another folder) is started from the old place
until the choice is made again; turning it off and on writes the new path.
