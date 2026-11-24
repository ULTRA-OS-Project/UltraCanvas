# MobilePC

UltraDesktop on a TV or monitor, run by an Android phone. Reference:
[`Docs/MobilePC/README.md`](../../Docs/MobilePC/README.md). Manual:
[`Docs/MobilePC/Manual.md`](../../Docs/MobilePC/Manual.md). Changelog and
version: [`Docs/MobilePC/CHANGELOG.md`](../../Docs/MobilePC/CHANGELOG.md).

An Android app only: `libMobilePC.so`, loaded by `UltraCanvasActivity`.

## Layout

| Path | What is in it |
|---|---|
| `main.cpp` | `ultracanvas_app_main()`: the application, the catalogue sets of the desktop and the hosted applications, `UltraCanvasAndroidDesktop::Enable()`, the control pad, UltraDesktop's window, `--launch` |
| `MobilePCControlPad.*` | The phone's window while the desktop is on the TV (`CreatePhoneWindow`): status line, Keyboard and Apps buttons, the touchpad (`MobilePCTouchpad`, a container passing the fingers to `UltraCanvasAndroidDesktop::TouchpadInput`), Left and Right click |
| `MobilePCHostedApps.*` | UltraViewer, UltraPaint and ArtCreator registered with `UltraCanvasHostedApps`, each started through the entry its own `main()` uses |
| `CMakeLists.txt` | The library: these files, UltraDesktop's `ui/` sources and the hosted applications' window sources, each application only when the core has its widgets |
| `android/AndroidManifest.xml`, `android/assets.txt` | What `scripts/android-package-apk.sh` packages: the manifest (`org.ultraos.mobilepc`, `UltraCanvasActivity`, `android.app.lib_name` MobilePC) and the media the app opens by path |

Nothing of UltraDesktop or the hosted applications is copied or changed for
MobilePC: their sources are compiled in from their own folders.

## Adding an application

1. Its window sources (not its `main.cpp`) go into `CMakeLists.txt`, under the
   widgets it needs, with `MOBILEPC_WITH_<APP>=1` and its `<APP>_VERSION`.
2. A `UltraCanvasHostedApps::Register` call in `MobilePCHostedApps.cpp`,
   whose `launch` does what its `main()` does after `app.Initialize` - without
   the single-instance lock, signal handlers or `app.Run()`.
3. Its catalogue set in `main.cpp`'s `AddSet` list, if it has one.
4. A row in `Docs/MobilePC/README.md`.

## Checking it

- `scripts/android-syntax-check.sh` type-checks the framework's Android code
  (desktop mode included) against the NDK's headers.
- CI (`.github/workflows/android-build.yml`) links `libMobilePC.so` against the
  sysroot, checks its entry points, packages `MobilePC-<abi>.apk`, and runs
  `scripts/android-mobilepc-smoke.sh` on an emulator with a simulated second
  display: the desktop must appear there, UltraPaint must start on it, and the
  desktop must come back to the phone when the display goes.
