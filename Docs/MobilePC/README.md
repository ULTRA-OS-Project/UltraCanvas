# MobilePC

## Overview

MobilePC turns an Android phone or tablet into a desktop computer. Connected
to a TV or monitor, it shows [UltraDesktop](../UltraDesktop/README.md) there at
full size - the taskbar, the Apps window, the info panel - and runs the
UltraCanvas desktop applications in windows on it, while the phone becomes the
control pad: a touchpad and a keyboard. A Bluetooth or USB mouse and keyboard
work as on any computer.

It is one Android app carrying everything in one process: UltraDesktop and the
applications it starts. The framework's Android backend provides the desktop
itself - the **desktop mode** of
[`UltraCanvas/OS/Android`](../../UltraCanvas/OS/Android/README.md#desktop-mode-a-tv-or-monitor),
on [UltraCanvasEmbeddedWindowManager](../UltraCanvas/UltraCanvasEmbeddedWindowManager.md)
- and the applications are listed and started through
[UltraCanvasHostedApps](../UltraCanvas/UltraCanvasHostedApps.md), so UltraDesktop
runs unchanged.

- User manual: [`Manual.md`](Manual.md).
- Source: [`Apps/MobilePC`](../../Apps/MobilePC/README.md).
- Design and the road ahead: [`Docs/Research/MobilePCDesignProposal.md`](../Research/MobilePCDesignProposal.md).
- Version: its own, from the first line of [`CHANGELOG.md`](CHANGELOG.md)
  (`MOBILEPC_VERSION`); the control pad's title carries it. UltraDesktop and
  the applications keep their own numbers.
- Build: Android only - `BUILD_MOBILEPC` (on), configured with the NDK's
  toolchain against the dependency sysroot like the demo (see
  [`packaging/`](../../UltraCanvas/OS/Android/packaging/README.md)); the target
  is `libMobilePC.so`, packaged with
  `scripts/android-package-apk.sh --manifest Apps/MobilePC/android/AndroidManifest.xml --assets Apps/MobilePC/android/assets.txt --icon media/appicon/MobilePC.png`.
  CI builds it for `arm64-v8a` and `x86_64` (`android-build.yml`, artifact
  `MobilePC-<abi>`) and runs it on an emulator with a simulated second display.

## Contents

- [Screens](#screens)
- [Connecting a display](#connecting-a-display)
- [Input](#input)
- [The applications](#the-applications)
- [Starting it](#starting-it)
- [Not yet](#not-yet)

## Screens

| Screen | Shows |
|---|---|
| TV or monitor | UltraDesktop and the application windows, with title bars (minimise, maximise, close), a shadow, and a pointer |
| Phone, while the desktop is on the TV | The control pad: where the desktop is, **Keyboard** and **Apps**, the touchpad, **Left click** and **Right click** |
| Phone, no display | The desktop itself, on the phone's screen (a tablet with a keyboard is a small computer that way) |

## Connecting a display

Any display Android offers an app as a second screen: a monitor or TV on a
USB-C cable or dock (the phone must support DisplayPort over USB-C), or a
wireless display the phone maker's own *Smart View*-style feature connects as
a second screen. Plugging in moves the desktop to it, pulling the cable brings
it back to the phone, without closing anything. Mirroring (Google Cast's
*Cast screen*, DisplayLink adapters) shows the phone's screen, so the desktop
appears there too when it is on the phone.

The desktop's scale on a TV or monitor follows its resolution: 100 % up to
1080 lines, 125 % at 1440, 200 % at 4K.

## Input

| With | Does |
|---|---|
| The touchpad | one finger moves the pointer; a tap clicks; a tap with two fingers is the right button; two fingers moving scroll; a tap and then press-and-move drags |
| Left click / Right click | click where the pointer is |
| Keyboard | brings up the phone's keyboard; what it types goes to the window in front on the TV |
| A mouse | captured while the desktop is on the TV, so it moves the TV's pointer |
| A keyboard | types into the window in front |
| The phone's back button | Escape |

## The applications

| Application | Started from |
|---|---|
| UltraFiler | the taskbar's UltraFiler button, the RAM disc button and the Apps window (System) - opens the phone's storage (`/storage/emulated/0`); asks once, at its first start, for *All files access* (Android 11 on, switched on in Settings on the phone; the storage permission before), without which it sees the folders but few of the files; no remote drives and no RAM discs in this build |
| UltraTexter | the Apps window (Office) - its windows kept by `TexterWindowManager.h`, as its own `main()` keeps them; a file's creation time shows as *Not available* (no `statx` before API 30) |
| UltraViewer | the Apps window (Graphics) |
| UltraPaint | the Apps window (Graphics) |
| ArtCreator | the Apps window (Graphics), and UltraFiler's *Edit in ArtCreator* on a drawing - opens and saves SVG, AI, DXF, DWG, EMF, WMF and ODG through the framework's Vector plug-in, built for Android and registered by MobilePC's `main()`; CDR, XAR and EPS stay out (their plug-ins are not built for Android) |

Each is the application's own window code, unchanged, started by the entry
its desktop `main()` uses. Their texts come from their own catalogues
(`UltraLocalisation::AddSet`). Each also answers its program's name
(`HostedAppInfo::programs`): UltraDesktop's taskbar button starts
"UltraFiler" with `UltraCanvasDesktopShell::LaunchProgram`, and UltraFiler
starts "UltraViewer" for a picture that is double-clicked, so both reach the
hosted applications without a change to either.

## Starting it

From the launcher, or with arguments through the intent's `ultracanvas.args`
extra:

```
adb shell am start -n org.ultraos.mobilepc/org.ultraos.ultracanvas.UltraCanvasActivity -e ultracanvas.args "--launch ultrapaint"
```

| Argument | Does |
|---|---|
| `--launch <id>` | starts that application once the desktop is up: `ultrafiler`, `texter`, `ultraviewer`, `ultrapaint`, `artcreator`; give it again for another |
| `--open <id> <file>` | the same, with a file to open in it; a relative path is taken from the app's home (`$HOME`, where the APK's media are unpacked under `share/`). ArtCreator logs whether it read the file and how many objects it has (`MobilePC: artcreator opened <file> (<n> objects)`) |
| `--debug` | the framework's diagnostics to logcat |

## Not yet

- UltraFiler's remote drives (UltraCloud is not built for Android), RAM discs,
  and the SD card and USB sticks as drives of their own (they are reachable
  under `/storage`).
- Keeping the session alive while the phone is locked (a foreground service).
- The wireless stream for phones without a second-screen display, and the TV
  and browser receivers.
- Menus and lists drawn beyond their window's edge are cut at it.
- Nothing of it has run on a phone yet - only on the emulator in CI.
