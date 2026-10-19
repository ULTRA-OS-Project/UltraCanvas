# UltraDesktop — user manual

How to use UltraDesktop day to day, make it your Windows desktop beside
Explorer or instead of it, get the Windows desktop back, and what to do when
something goes wrong.

Day to day means starting programs, switching between windows and virtual
desktops, the clipboard history, screenshots, notes, the Sticker board and
the info panel.

This manual describes UltraDesktop 0.4. Companion documents:
[`README.md`](README.md) (the reference: every button, the settings file, how
it works), [`CHANGELOG.md`](CHANGELOG.md), and the developer notes in
[`Apps/UltraDesktop/README.md`](../../Apps/UltraDesktop/README.md).

## 1. What UltraDesktop is

UltraDesktop is a desktop: one window the size of the screen that lies under
every other window. It holds the wallpaper, a **taskbar** with your open
windows, and a **right bar** with your virtual desktops, the tools (notes,
Sticker board, clipboard history, screenshot) and the **info panel**, which
shows what your devices and connections are doing.

| Where | What you get |
|---|---|
| ULTRA OS | The desktop of the session: it starts when you sign in. |
| Linux and the BSDs (X11) | Everything. Run `UltraDesktop` from a terminal or your session's autostart; it needs a window manager, which keeps the window list and the virtual desktops. |
| Windows 10 and 11 | Everything, plus the choice to use it as your Windows desktop — beside Explorer or instead of it (section 6). |
| macOS | The Applications window, the notes, the Sticker board and the settings. The taskbar's list of open windows stays empty, and the Task Manager says so. |

## 2. Getting started

### 2.1 On Windows: install

1. Run the installer, `UltraCanvas-Setup-Windows-<version>-<arch>.exe`. It
   installs for your account only, into
   `%LOCALAPPDATA%\Programs\UltraCanvas`, and needs no administrator rights.
2. On the last page, **Use UltraDesktop as my Windows desktop** is not ticked.
   Tick it if you want UltraDesktop at every sign-in, beside Explorer
   (section 6.2). You can change your mind at any time in UOS-Settings.
3. The installer adds Start-menu entries for the applications. UltraDesktop is
   among them.

### 2.2 Trying it without changing anything

Start **UltraDesktop** from the Start menu. It covers your main screen under
all your windows. Explorer keeps running: its taskbar stays along the bottom,
over the foot of UltraDesktop's right bar, and nothing changes at the next
sign-in.

To end the trial, right-click the **ULTRA OS settings** button (the top
button of the taskbar) and choose **Close UltraDesktop**.

### 2.3 On Linux

Start `UltraDesktop` from a terminal, or add it to your session's autostart.
On ULTRA OS the session starts it for you. To try the taskbar along another
edge for one run, start it with `UltraDesktop --edge top` or `--edge bottom`.

## 3. The screen at a glance

```
┌────┬──────────────────────────────────────────────────────────────┬────┐
│ ⚙  │                                                              │ 1  │  virtual desktops
│ ⋮⋮ │                                                              │ 2  │
│╲   │                                                              │ 3  │
│ ╲  │                                                              │ 📌 │  Sticker board
│ ▣  │                                                              │ 🗒 │  notes
│ ▣  │                     wallpaper                                │ 📋 │  clipboard history
│ ▣  │              (your windows float above it)                   │ 📷 │  screenshot
│ ▣  │                                                              │╱   │
│  ╲ │                                                              │ ✉14│  info panel
│   ╲│                                                              │ ⇧⇩ │
│ ▤  │                                                              │ ⌨ ●│
│ 📁 │                                                              │ 🔋 │
└────┴──────────────────────────────────────────────────────────────┴────┘
 taskbar                                                               right bar
```

**The taskbar** (left by default; top or bottom in the settings) has three
groups:

1. **ULTRA OS settings** (⚙) opens UOS-Settings on its *Desktop* page, and
   **Applications** (⋮⋮) opens the list of installed programs.
2. **Your open windows** on the current desktop, one button each.
3. **Pinned**: the **RAM disc** (on Linux, `/dev/shm`; on Windows only if you
   set a folder) and **UltraFiler**.

**The right bar** has the virtual desktops and the tools at the top and the
info panel at the bottom.

Maximised windows stop beside the bars instead of covering them.

Every button has a tooltip: rest the pointer on it to see what it is, or what
it is showing.

## 4. Everyday use

### 4.1 Starting a program

- Click **Applications** (⋮⋮) on the taskbar. Every installed program is a
  tile; type in **Find an application…** to narrow the list, then click the
  tile. The window closes once the program starts; if it cannot start, the
  window stays open and says why.
- When UltraDesktop runs instead of Explorer, a tap of the **Win key** on its
  own opens the same window, where the Start menu would open.
- **UltraFiler** and the **RAM disc** are one click away at the foot of the
  taskbar.

On Windows the list holds the Start menu's programs, named as the Start menu
names them, and the Microsoft Store apps (Calculator, Photos, Settings …).
On Linux it holds every program with a desktop entry, which is the same list
any other desktop's application menu shows.

### 4.2 Switching between windows

The middle group of the taskbar has a button for each open window on the
current desktop, with the program's icon. The active window's button is
highlighted.

- **Click** a button to bring its window to the front; click the active
  window's button to minimise it.
- **Right-click** for **Activate**, **Minimize**, **Move to** (a desktop, or
  **Every desktop**) and **Close window**.
- **Drag** a button to put it somewhere else in the row. A new window joins
  its program's other windows.
- When the row is full, scroll it with the mouse wheel.

### 4.3 Virtual desktops

The numbered buttons at the top of the right bar are your virtual desktops.
Click one to switch to it. The taskbar then shows that desktop's windows.

- To change how many there are, use UOS-Settings → Desktop → **Virtual
  desktops** (section 5). The window manager has the last word and may offer
  a different number.
- To put a window on another desktop, right-click its taskbar button →
  **Move to**.
- On Windows, the buttons are Windows' own virtual desktops, which Explorer
  provides: they switch while Explorer runs (beside Explorer). You add and
  remove desktops in Task View (Win+Tab), and **Move to** moves only
  UltraDesktop's own windows.

### 4.4 Clipboard history

UltraDesktop remembers what you copy, so you can paste something you copied
earlier. Copies that a password manager marks as secret are not kept. The
history is stored encrypted on your disk.

**To open it**, click the clipboard button (📋) on the right bar, or press
**Super+V** (Win+V) in any window. The panel shows a search field, your
pinned entries and the ten latest copies. Each entry has a thumbnail and
**Copy**, **Edit** and **Delete**. Typing searches the whole history.

Entries that suit the program you are pasting into come first, under
*For <program>*: the newest images for a paint program, files for a file
manager, code and text for an editor.

| Key | In the clipboard panel |
|---|---|
| Up / Down | choose an entry |
| Enter | copy it, ready to paste |
| Delete | delete it (Undo brings it back for a few seconds) |
| F2 | edit it in UltraClipboard |
| Escape | close the panel |

- The **Recording** switch in the panel pauses the history. While it is
  paused, the clipboard button is crossed out.
- **Open UltraClipboard** shows the whole history.
- **Right-click** the clipboard button for **Pause recording**, **Open
  UltraClipboard** and **Clear history**.
- When a program you copied from quits, UltraDesktop puts that copy back on
  the clipboard, so it can still be pasted.

On Windows, while Explorer runs, Win+V belongs to Windows' own clipboard
history. Use the clipboard button instead. When UltraDesktop runs instead of
Explorer, Win+V opens UltraDesktop's panel.

### 4.5 Screenshots

Click the screenshot button (📷). The whole screen is saved to
`Pictures/Screenshots/Screenshot <date> <time>.png` in your home folder. The
button shows a green dot for three seconds, and its tooltip names the file.

From a prompt, `UltraDesktop --screenshot` does the same, and
`UltraDesktop --screenshot <file>` saves to the file you name.

### 4.6 Notes on the wallpaper

The notes button (🗒) shows and hides your sticky notes.

- **+** in the corner adds a note. Type straight into it.
- Drag a note by its top bar; **◐** gives it the next of six paper colours;
  **×** removes it.
- Notes stay where you left them, through restarts.

### 4.7 The Sticker board

The pinboard button (📌) opens the Sticker board: your files and notes sorted
into categories, on a board over the wallpaper. It starts with **Docs**,
**Work** and **Home**.

- **Add a file:** drag it from the file manager and drop it on a category. A
  file that is already on the board moves to the category you drop it on.
- **Open a file:** double-click its card. It opens with its usual program.
- **Move a card:** drag it to another category.
- **Write a note:** **+ Add card**.
- **Add a category:** **+ Add category**, then type its name. To rename a
  category later, double-click its title.
- **Change the look:** the picker in the board's bar offers **Sticky notes**,
  **Poster**, **Schematic** and **Dark**.
- **Move or resize the board:** drag it by its title; resize it by the grip
  (◢) in its bottom-right corner.

The board, its look, its place and its size are kept.

### 4.8 The info panel

The icons at the foot of the right bar show your devices and connections:
Email, Upload, Download, Internet/LAN, VPN, Bluetooth, Wi-Fi, USB, Keyboard,
Webcam, Microphone, Loudspeaker, Battery and Task Manager.

| Marker | Means | For example |
|---|---|---|
| red dot | a device is on, or a link is down | the webcam is in use, the microphone is recording, Bluetooth is on; no network link; Wi-Fi present but not joined |
| yellow dot | activity | something is downloading or uploading, the loudspeaker is playing, the VPN is up |
| grey pill | a count or a value | unread mail, attached USB devices, the battery level (green while charging, red at 15 % and below), the keyboard layout |

The tooltips say it in words, such as "Wi-Fi: HomeNet", "Download: 1.2 MB/s"
or "Battery: 84%, charging".

A click opens the program for that icon:

| Icon | Opens |
|---|---|
| Email | UltraMail |
| Upload, Download, Internet/LAN, VPN, Wi-Fi | UltraNetMonitor |
| USB, Keyboard, Webcam | DeviceExplorer |
| Bluetooth, Microphone, Loudspeaker, Battery, Task Manager | the Task Manager |

When there are many virtual desktops on a small screen, the panel scrolls: a
chevron marks where more icons are, and a click on it or the mouse wheel
scrolls.

When UltraDesktop runs instead of Explorer on Windows, the programs' **tray
icons** (OneDrive, a VPN client, a chat program) appear here too, above the
services. A click opens the program, and a right-click opens its menu.

### 4.9 Notifications

On ULTRA OS, notifications from every program appear in the top-right
corner, beside the right bar. At most four show at a time, newest on top.

- Click the text to do what the notification offers. For example, UltraMail
  opens the mail.
- Click one of its buttons to do what the button says, or **×** to dismiss it.
- A notification goes by itself after 8 seconds, or 5 for a minor one, but not
  while the pointer rests on it. A critical one, such as a low battery, stays
  until you close it.

On a Linux desktop that has its own notification server (GNOME, Plasma,
dunst …), that server shows them instead. On Windows beside Explorer, Windows
shows them as before. Instead of Explorer, UltraDesktop shows the tray icons'
balloons.

### 4.10 The Task Manager

The Task Manager icon at the foot of the info panel opens it.

- **Windows** lists the open windows, each with **Activate** and **Close**.
- **Hardware** shows the machine: CPU load and temperature, memory, storage
  and the network interfaces.

## 5. Settings

UltraDesktop's settings are the **Desktop** page of **UOS-Settings**. Click
the **ULTRA OS settings** button (⚙), or right-click it and choose **ULTRA OS
settings**.

| Setting | What it does |
|---|---|
| **Taskbar** | **Left**, **Top** or **Bottom** edge. |
| **Wallpaper** | A picture file (**Browse...**). Empty: the framework's landscape picture. |
| **RAM disc** | The folder the RAM disc button opens. Empty: no button. On Linux it is `/dev/shm` unless you change it; on Windows it is empty unless you set one. |
| **File manager** | The program the folder buttons start (UltraFiler unless you change it). |
| **Virtual desktops** | 1 to 9. The window manager may offer a different number. |
| **At sign-in** (Windows only) | **The Windows desktop**, **UltraDesktop beside Explorer** or **UltraDesktop instead of Explorer**. See section 6. |

Changes are saved at once: a choice when you make it, a text field when you
leave it or press Return. A running UltraDesktop takes them over within a
second; there is no need to restart it. The line under the settings names the
file they were saved to, or says why they were not saved.

## 6. UltraDesktop as your Windows desktop

On Windows there are three ways to start a session. The switch is for your
account only and needs no administrator rights.

| | **The Windows desktop** | **UltraDesktop beside Explorer** | **UltraDesktop instead of Explorer** |
|---|---|---|---|
| At sign-in | Explorer, as Windows has it | Explorer, then UltraDesktop | UltraDesktop only |
| Takes effect | at once | at once | at the next sign-in |
| Explorer's taskbar | as you had it | hidden; slides in at the bottom edge | not there |
| Start menu, quick settings, notification centre | yes | yes | no |
| Win key alone | Start menu | Start menu | UltraDesktop's Applications |
| Win+V | Windows' clipboard | Windows' clipboard | UltraDesktop's clipboard |
| Tray icons | Explorer's taskbar | Explorer's taskbar | UltraDesktop's info panel |
| Other monitors | Explorer's desktop | Explorer's desktop | UltraDesktop's wallpaper (the bars stay on the main monitor) |
| Sign out, restart, shut down | Start menu | Start menu | UltraDesktop's ⚙ menu |

**If you are not sure, choose beside Explorer.** Everything in Windows keeps
working, and UltraDesktop is your desktop at once.

### 6.1 Where to switch

You can switch in any of three places, and each can undo the others:

- **UOS-Settings → Desktop → At sign-in**;
- the **right-click menu of the ULTRA OS settings button** (⚙) on
  UltraDesktop's taskbar;
- a command at a prompt: `UltraDesktop --use-as-desktop`,
  `UltraDesktop --use-instead-of-explorer` or `UltraDesktop --restore-desktop`.

### 6.2 Beside Explorer

Choose **UltraDesktop beside Explorer** in UOS-Settings, or **Use as the
Windows desktop (beside Explorer)** on the ⚙ menu. Ticking the installer's
last page does the same.

- UltraDesktop starts now, if it is not already running, and at every
  sign-in. It is listed as **UltraCanvas Desktop** among Task Manager's
  startup apps.
- Explorer's taskbar hides itself. Move the pointer to the bottom edge of the
  screen and it slides in, with the Start menu, the tray icons and the clock.
- Explorer keeps running, so its notifications, its Start menu and the
  programs that rely on it all work as before.

### 6.3 Instead of Explorer

Choose **UltraDesktop instead of Explorer** in UOS-Settings, or **Use instead
of Explorer (from the next sign-in)** on the ⚙ menu. Then **sign out and sign
in again**.

From then on, UltraDesktop is the whole desktop:

- the programs' **tray icons** are on the info panel, and their balloons
  appear as notifications;
- a tap of the **Win key** opens **Applications**;
- **Win+V** opens UltraDesktop's clipboard history;
- the **wallpaper** covers every monitor, and a monitor you plug in is covered
  within a few seconds;
- the **⚙ right-click menu** offers **Sign out...**, **Restart...** and **Shut
  down...**. Each asks first, and open programs are asked to close.

**What you give up:** without Explorer there is no Start menu, no quick
settings (Wi-Fi, volume, brightness), no notification centre, and no
Explorer shortcuts such as Win+E, Win+R and Win+X. In their place:

| You want | Do this |
|---|---|
| a program from the Start menu | Win key, or Applications on the taskbar |
| Windows Settings (Wi-Fi, sound, display) | Applications → **Settings** |
| a File Explorer window | Applications → **File Explorer** (it opens *This PC*), or UltraFiler |
| Task Manager or Run | Ctrl+Shift+Esc; **Run new task** there replaces Win+R |
| to lock the screen | Win+L, or Ctrl+Alt+Del |

**The safety net.** At sign-in Windows starts *UltraCanvasShellGuard*, which
then starts UltraDesktop. It makes sure your screen is never left empty:

- If UltraDesktop crashes, the guard starts it again.
- After the third crash within a minute, or if UltraDesktop cannot start at
  all, the guard starts the Windows desktop for that session and tells you
  why. UltraDesktop is tried again at the next sign-in.
- The guard starts the programs that start with Windows, as Explorer would.
  The ones you switched off in Task Manager stay off.
- It writes what it did to `%LOCALAPPDATA%\UltraCanvas\ShellGuard.log`.

To keep Explorer after all, choose **Keep Explorer running beside
UltraDesktop** on the ⚙ menu, or **UltraDesktop beside Explorer** in
UOS-Settings.

### 6.4 Getting the Windows desktop back

Any of these works, whichever mode you are in:

- UOS-Settings → Desktop → At sign-in → **The Windows desktop**;
- ⚙ right-click menu → **Switch back to the Windows desktop**;
- `UltraDesktop --restore-desktop` at a prompt;
- **when nothing responds:** press **Ctrl+Shift+Esc**, choose **Run new
  task**, type `UltraDesktop --restore-desktop` and press Enter. The
  installer registered the name, so no folder is needed.

The start at sign-in is removed, and Explorer's taskbar comes back as you had
it, auto-hide or not.

- **Beside Explorer:** UltraDesktop closes within a second.
- **Instead of Explorer:** Explorer starts in its place at once. If
  UltraDesktop no longer responds, the guard ends it after five seconds and
  starts Explorer all the same.

### 6.5 Uninstalling

Uninstall **UltraCanvas** from **Settings → Apps → Installed apps** (*Apps &
features* on Windows 10). The uninstaller gives you the Windows desktop back
first, then removes exactly the files it installed. Nothing is left starting
at sign-in.

## 7. Keyboard and mouse

| Where | Action | Does |
|---|---|---|
| Any window | Super+V (Win+V) | opens the clipboard history (on Windows only instead of Explorer) |
| Any window, Windows instead of Explorer | Win key alone | opens Applications |
| Taskbar window button | click / right-click / drag / wheel | activate or minimise / window menu / reorder / scroll the row |
| ⚙ button | click / right-click | UOS-Settings / desktop menu (switching, sign out …) |
| Clipboard button | click / right-click | the panel / pause, open UltraClipboard, clear |
| Clipboard panel | Up, Down, Enter, Delete, F2, Escape | choose, copy, delete, edit, close |
| Sticker board | drop files / double-click a card / double-click a category title | add / open / rename |
| A notification | click / × | its default action / dismiss |

## 8. Troubleshooting

**The screen stays empty after signing in (Windows).** Press
**Ctrl+Shift+Esc**, choose **Run new task**, and run
`UltraDesktop --restore-desktop`. If even that does not help, run
`explorer.exe` there to get the Windows desktop for this session. Then look at
`%LOCALAPPDATA%\UltraCanvas\ShellGuard.log`.

**I chose "instead of Explorer" but got the Windows desktop, with a message.**
UltraDesktop crashed three times within a minute, or could not start. The
message and `%LOCALAPPDATA%\UltraCanvas\ShellGuard.log` say which. The next
sign-in tries UltraDesktop again. Reinstalling UltraCanvas usually helps; to
stop trying, switch back to the Windows desktop (section 6.4).

**"UltraDesktop instead of Explorer" is refused.** The switch is made only
when `UltraCanvasShellGuard.exe` is installed beside `UltraDesktop.exe`,
because it is what brings Explorer back if UltraDesktop fails. Reinstall
UltraCanvas.

**Explorer's taskbar has disappeared.** UltraDesktop runs beside Explorer,
which hides Explorer's taskbar. Move the pointer to the bottom edge of the
screen and it slides in. To have it back for good, switch back to the Windows
desktop.

**Win+V opens Windows' clipboard, not UltraDesktop's.** That is expected while
Explorer runs. Use the clipboard button on the right bar.

**The Win key opens the Start menu.** That is expected beside Explorer. Only
instead of Explorer does the Win key open Applications.

**My other monitor shows Explorer's desktop, or the wallpaper is missing
there.** Beside Explorer, the other monitors show Explorer's desktop. Instead
of Explorer, UltraDesktop covers them with its wallpaper; a monitor whose
wallpaper was closed is covered again within a few seconds.

**I can't find the tray icons.** Beside Explorer they are in Explorer's
taskbar; move the pointer to the bottom edge. Instead of Explorer they are on
UltraDesktop's info panel.

**The taskbar shows no windows.** On macOS that is expected. On Linux, a window
manager has to be running.

**A setting does not seem to take effect.** Settings are saved when you leave
the field or press Return. The status line on the Desktop page says where
they were saved, or why not. A running UltraDesktop picks them up within a
second.

**Something else.** `UltraDesktop --windows`, `--apps` and `--devices`
(section 9) print what UltraDesktop sees, which helps tell a desktop problem
from a system one.

## 9. Command line

```
UltraDesktop                       open the desktop
UltraDesktop --edge left|top|bottom  the taskbar on that edge, for this run only
UltraDesktop --settings <file>     use another settings file
UltraDesktop --windows             print the open windows and exit
UltraDesktop --apps                print the installed applications and exit
UltraDesktop --devices             print what the info panel sees and exit
UltraDesktop --screenshot [file]   capture the screen (default: Pictures/Screenshots)
UltraDesktop --use-as-desktop      Windows: beside Explorer from now on, and open it
UltraDesktop --use-instead-of-explorer
                                   Windows: instead of Explorer from the next sign-in
UltraDesktop --restore-desktop     Windows: back to the Windows desktop
UltraDesktop --version | --help
```

`--session` and `--shell` are what the sign-in starts UltraDesktop with; you
do not need them yourself.

On Windows, run these at a command prompt in the UltraCanvas folder
(`%LOCALAPPDATA%\Programs\UltraCanvas`), or with that folder on `PATH`. Typed
without an extension, `UltraDesktop …` runs `UltraDesktop.com`, which waits
for UltraDesktop and prints its output. `UltraDesktop.exe …` starts it
without waiting. Task Manager's **Run new task** finds `UltraDesktop` from
anywhere.

## 10. Where UltraDesktop keeps your data

| What | Linux | Windows |
|---|---|---|
| Settings, notes, Sticker board | `~/.config/ultraos/desktop.json` | `%APPDATA%\ultraos\desktop.json` |
| Clipboard history (encrypted) | `~/.local/share/ultraos/clipboard/` | `%LOCALAPPDATA%\ultraos\clipboard\` |
| Screenshots | `~/Pictures/Screenshots/` | `Pictures\Screenshots\` in your user folder |
| The guard's log | — | `%LOCALAPPDATA%\UltraCanvas\ShellGuard.log` |

On macOS the settings are in `~/Library/Application Support/ultraos/`. Where
`XDG_CONFIG_HOME` or `XDG_DATA_HOME` is set, the settings or the clipboard
history are under it.
