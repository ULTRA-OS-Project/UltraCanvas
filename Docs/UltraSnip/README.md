# UltraSnip

The screenshot and screen-recording tool of ULTRA OS: a picture or a video
of an area you drag out, of a window you click, or of the whole screen -
what the Snipping Tool is on Windows. A picture goes to the clipboard and to
your Pictures folder as it is taken; a video goes to your Videos folder
while it is recorded.

Source: [`Apps/UltraSnip/`](../../Apps/UltraSnip/). Changelog:
[`CHANGELOG.md`](CHANGELOG.md). The screen comes through
[UltraCanvasDesktopShell](../UltraCanvas/UltraCanvasDesktopShell.md)
(`CaptureScreenImage`, `CaptureScreenArea`), the video through
[VideoFX](../Modules/VideoFX/README.md)'s `VideoFXFrameRecorder`.

## Taking a snip

**From the desktop.** Click the screenshot button on UltraDesktop's right
bar, or press `Super+Shift+S` in any window: the screen freezes and you drag
out the area. The button's right-click menu has the other shapes and the
recordings.

**From the window.** Start UltraSnip from the applications; choose the
shape, the kind and the delay in its bar and click *New* (Ctrl+N):

| Shape | What happens |
|---|---|
| *Rectangle* | The screen freezes, dimmed; drag out the area. Its size in pixels is shown beside it. Enter takes the whole screen, Escape or a right click cancels. |
| *Window* | The window under the pointer is outlined with its title; click it. A drag takes an area instead. Enter takes the outlined window. |
| *Full screen* | Everything, at once. |

A *delay* of 3, 5 or 10 seconds lets you open a menu or a tooltip first.
UltraSnip's own window hides itself while the snip is taken, so it is never
in the picture, and comes back with the result.

**From a prompt.** `UltraSnip --rectangle`, `--window` or `--screen` takes
that snip at once; add `--video` for a recording and `--delay 3` (5, 10) for
a wait. When UltraSnip is already running the request goes to it.

## Pictures

A picture is put on the clipboard and written to
`Pictures/Screenshots/Screenshot <date> <time>.png` the moment it is taken;
either can be switched off under the picture (*Copy pictures to the
clipboard*, *Save pictures to Pictures/Screenshots*). The window shows it,
with:

| Button | Key | |
|---|---|---|
| Save as | Ctrl+S | under another name, in another folder, or as another picture type - every type this build writes, through the framework's Save dialog; the type's *Settings* there give the quality, and a size other than the picture's own |
| Copy | Ctrl+C | onto the clipboard, again |
| Open folder | | the saved file's folder in the file manager |
| Edit in UltraPaint | | the saved picture in UltraPaint (saved first when it was not) |

## Videos

With *Video* chosen, the area you pick is recorded instead:
`Videos/Screen Recordings/Screen recording <date> <time>.mp4`, H.264 where
this build's FFmpeg has it, at the frame rate chosen under the picture (10,
15, 20 or 30 frames a second; more is smoother and larger). A small bar
appears beside the area - above it, else below, else at the top of the
screen - with the elapsed time, *Pause* / *Resume* and *Stop*. The pause is
left out of the recording. After *Stop* the recording plays in the window,
with *Save as* (a copy under another name) and *Open folder*. The file is
written as it goes, so a recording cut short by a crash or a power cut still
plays up to its last seconds.

Not yet: sound (the microphone, the loudspeaker), the pointer in the
recording, and a recording on a build whose VideoFX has no FFmpeg - *New*
says so then, and pictures still work.

## Where things are kept

| What | Linux | Windows |
|---|---|---|
| Pictures | `~/Pictures/Screenshots/` | `Pictures\Screenshots\` in your user folder |
| Recordings | `~/Videos/Screen Recordings/` | `Videos\Screen Recordings\` in your user folder |
| Settings (shape, kind, delay, options, the Save-as folder) | `~/.config/UltraSnip/settings.json` | `%APPDATA%\UltraSnip\settings.json` |

## How it is built

| Path | What is in it |
|---|---|
| `ui/UltraSnipTypes.h` | The request: shape, kind, delay, as JSON between starts |
| `ui/UltraSnipCapture.*` | No UI: the rectangle from a drag, the crop of a capture into RGBA, the topmost window under a point, where the recording bar goes, files written and copied |
| `ui/UltraSnipSettings.*` | The settings file, through `UltraCanvasAppSettings` |
| `ui/UltraSnipOverlay.*` | The selection: a borderless window the size of the screen showing the frozen capture (`UltraCanvasImageElement`) under the marquee element (the dimming, the rectangle and its size, the crosshair, the outlined window, the hint) |
| `ui/UltraSnipRecorder.*` | The recording thread: `CaptureScreenArea` at the frame rate into `VideoFXFrameRecorder`; the clock that leaves pauses out |
| `ui/UltraSnipRecordingBar.*` | The floating bar: a notification-type window (above everything, never taking the keyboard) with the time, Pause / Resume and Stop |
| `ui/UltraSnipWindow.*` | The window: the bar (`UltraCanvasButton`, two `UltraCanvasSegmentedControl`s, an `UltraCanvasDropdown`), the result (`UltraCanvasImageElement` / `UltraCanvasMediaViewer`), the actions and options (`UltraCanvasCheckbox`), and the capture step by step |
| `main.cpp` | The command line and the single instance |
