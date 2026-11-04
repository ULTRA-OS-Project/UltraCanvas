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

**From the window.** Start UltraSnip from the applications: until there is
a snip its window is just its bar. Choose the shape and the kind (icons;
each names itself in its tooltip) and the delay, and click *Start*
(Ctrl+N):

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
`Pictures/Screenshots/Screenshot <date> <time>.<type>` the moment it is
taken, in the type chosen in the settings (PNG unless changed); either can
be switched off there. The window opens out below its bar to show it, and
the bar gains four icon buttons after the delay, with a line saying what
was done (the whole path in its tooltip):

| Button | Key | |
|---|---|---|
| Save as | Ctrl+S | under another name, in another folder, or as another picture type - every type this build writes, through the framework's Save dialog; the type's *Settings* there give the quality, and a size other than the picture's own |
| Copy | Ctrl+C | onto the clipboard, again |
| Open folder | | the saved file's folder in the file manager |
| Edit in UltraPaint | | the saved picture in UltraPaint (saved first when it was not) |

## Videos

With *Video* chosen, the area you pick is recorded instead:
`Videos/Screen Recordings/Screen recording <date> <time>.mp4` (H.264; or
Matroska, or WebM with VP9, as the settings say), at the frame rate set
there (5 to 60 a second; more is smoother and larger). A small bar
appears beside the area - above it, else below, else at the top of the
screen - with the elapsed time, *Pause* / *Resume* and *Stop*. The pause is
left out of the recording. After *Stop* the recording plays in the window,
with *Save as* (a copy under another name) and *Open folder*. The file is
written as it goes, so a recording cut short by a crash or a power cut still
plays up to its last seconds.

Not yet: sound (the microphone, the loudspeaker), the pointer in the
recording, and a recording on a build whose VideoFX has no FFmpeg - *Start*
says so then, and pictures still work.

## Settings

The *Settings* button at the right end of the bar (the cog) opens a window laid out like
UltraFiler's: the sections on the left, the page on the right, the page's
*Restore defaults* and *Close* at the foot. Every change applies at once and
is saved.

| Page | What it sets |
|---|---|
| Pictures > File type | The type a picture is written as. The useful ones come first - PNG, JPEG XL, AVIF, HEIC, WebP, JPEG - each greyed when this build cannot write it, and *Another type* lists every other picture type this build writes. *Settings for .type* opens the type's own quality and compression settings, the same ones the framework's Save dialog keeps for every application. The clipboard always gets a PNG. A type the build cannot write falls back to PNG and says so. |
| Pictures > Saving | Whether every picture is written as it is taken, whether it goes to the clipboard, and the folder it is written to (*Choose folder*, or back to Pictures/Screenshots). |
| Videos > Recording | The recording's container - MP4 with H.264, Matroska with H.264, WebM with VP9, each greyed when the build's FFmpeg lacks the encoder - the frame rate (5 to 60 a second) and the folder. |
| Behaviour > After a snip | Whether the UltraSnip window appears with the result after a snip started from the desktop's button, the shortcut or the command line, or stays out of the way: the picture is saved and copied and nothing appears (a recording still shows its bar). A snip taken from the window always returns to it. |

## Languages

UltraSnip shows its texts in the language UltraCanvas runs in, from its own
set (`media/locale/en/UltraSnip.json` in English) over the *Image* and
*Video* base sets and the standard set. *Translate UltraSnip...* on the
settings window's start page opens the localisation editor on that set; a
change is kept in the settings folder and shows at the next start.

## Where things are kept

| What | Linux | Windows |
|---|---|---|
| Pictures | `~/Pictures/Screenshots/` | `Pictures\Screenshots\` in your user folder |
| Recordings | `~/Videos/Screen Recordings/` | `Videos\Screen Recordings\` in your user folder |
| Settings (shape, kind, delay, the types, folders and options, the Save-as folder) | `~/.config/UltraCanvas/UltraSnip/settings.json` | `%APPDATA%\UltraCanvas\UltraSnip\settings.json` |

## How it is built

| Path | What is in it |
|---|---|
| `ui/UltraSnipTypes.h` | The request: shape, kind, delay, as JSON between starts |
| `ui/UltraSnipCapture.*` | No UI: the rectangle from a drag, the crop of a capture into RGBA, the topmost window under a point, where the recording bar goes, files written and copied |
| `ui/UltraSnipSettings.*` | The settings file, through `UltraCanvasAppSettings`: the picture and video types and folders, the frame rate, the after-snip behaviour; the paths of a new snip |
| `ui/UltraSnipSettingsDialog.*` | The settings window: `UltraCanvasTreeView` of pages on the left, a page of `UltraCanvasRadio` / `UltraCanvasCheckbox` / `UltraCanvasSlider` / `UltraCanvasDropdown` controls on the right, the notes block at the foot - UltraFiler's settings window's skeleton |
| `ui/UltraSnipOverlay.*` | The selection: a borderless window the size of the screen showing the frozen capture (`UltraCanvasImageElement`) under the marquee element (the dimming, the rectangle and its size, the crosshair, the outlined window, the hint) |
| `ui/UltraSnipRecorder.*` | The recording thread: `CaptureScreenArea` at the frame rate into `VideoFXFrameRecorder`; the clock that leaves pauses out |
| `ui/UltraSnipRecordingBar.*` | The floating bar: a notification-type window (above everything, never taking the keyboard) with the time, Pause / Resume and Stop |
| `ui/UltraSnipWindow.*` | The window: the bar (`UltraCanvasButton`, two `UltraCanvasSegmentedControl`s, an `UltraCanvasDropdown`), the result (`UltraCanvasImageElement` / `UltraCanvasMediaViewer`), the actions and the Settings button, and the capture step by step |
| `main.cpp` | The command line and the single instance |
