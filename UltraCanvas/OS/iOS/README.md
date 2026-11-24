# OS/iOS — iOS platform backend

The investigation and the phased plan are in
[`Docs/UltraCanvas/iOSPortInvestigation.md`](../../../Docs/UltraCanvas/iOSPortInvestigation.md).
This directory holds phase 1, the dependency packaging (`packaging/`), phase
2, the UIKit backend, and the first part of phase 3: the keyboards, host
appearance and the document picker for opening files.

**Status: written, but not yet seen working.** Nothing here has been built
on a Mac by hand. `.github/workflows/ios-build.yml` is the first place
anything here meets Xcode. It builds the sysroot, compiles the core library
and DemoApp for the simulator, installs `UltraCanvasDemo.app` on a booted
simulator, launches it, and keeps its screenshot and output. Until that run
is green, read this as a design that compiles only in review.

## What is here

| File | Role |
|---|---|
| `UltraCanvasiOSMain.mm` | `main()`: `UIApplicationMain` with the backend's app delegate. On launch it sets `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_CACHE_HOME` under the container's `Library/`, puts the root view controller in the key window, and schedules the application's `extern "C" int ultracanvas_app_main(int, char**)` with `performSelector:afterDelay:0` — a run-loop timer, so the launch callback returns as iOS requires. The application's existing `main()` is renamed to that, as on Android (`Apps/DemoApp/main.cpp`). The view delivers touches (stable small finger ids) and size changes, the delegate the lifecycle. The view is also the software keyboard's editor (`UIKeyInput`): it is first responder only while a widget holds the caret (`SetiOSKeyboardWanted`). Typed text arrives as committed text, Return and backspace as key presses, and autocorrection is off. A hardware keyboard's presses (`UIKey`, HID usage → `UCKeys`) are all taken as key events with their text, so nothing types twice. |
| `UltraCanvasiOSApplication.{h,mm}` | The application base's pure virtuals. `CollectAndProcessNativeEvents` runs the main run loop one turn (`CFRunLoopRunInMode`, `returnAfterSourceHandled`) with the next timer as its timeout. UIKit's callbacks fire inside that turn and queue `UCEvent`s. `WakeUpEventLoop` is a `CFRunLoopSource`, the macOS backend's. `RunInEventLoop` flushes the CoreAnimation transaction. Touches become touch events for every finger, and mouse events for the first one, with double-tap synthesis. A second finger ends the mouse interaction for the rest of the gesture: the Android rule. Fonts are fontconfig + FreeType with the bundled families (Ubuntu / Ubuntu Mono). Host appearance comes from the trait collection: dark mode, contrast or Darken Colors, Reduce Motion, the tint as the accent, and Dynamic Type as the text scale. A trait change refreshes it. The caret drives the software keyboard, and a hide waits a loop turn. Also `OpenURLiOS` (`UIApplication openURL`). |
| `UltraCanvasiOSWindow.{h,mm}` | The window base's pure virtuals. A Cairo **image** surface at physical pixels, presented as the root view layer's `contents` (`CGBitmapContextCreateImage`, which copies). This is not cairo's Quartz surface, whose header needs ApplicationServices. Points are the logical units, and the scale is `traitCollection.displayScale`. The window always covers the view. Rotation and split view arrive as `HandleViewResized`, and backgrounding hides the window so nothing renders unseen. |
| `UltraCanvasiOSClipboard.{h,mm}` | `UIPasteboard`: text, and images as PNG. Secret text is local-only and expires after two minutes. Changes are seen through `changeCount`, which reads nothing, so iOS shows no "pasted from" notice for it. |
| `UltraCanvasiOSNativeDialogs.mm` | Messages, questions and text input are `UIAlertController`s, answered synchronously by pumping the run loop, as on Android. Opening one or several files is the document picker. Documents are copied into the sandbox (`asCopy`), so the caller gets plain paths, and the filters become content types. Saving is `SaveContent`: the bytes go into a temporary file of the suggested name, which the picker's export mode copies to where the user chooses. The path-returning `SaveFile` stays a stub, since nothing outside the sandbox has a path the app could write to later. `SelectFolder` opens a folder with security-scoped access, kept for the run. |
| `UltraCanvasiOSSystemLanguage.mm` | `NSLocale.preferredLanguages`. |
| `UltraCanvasiOSFileLoader.mm` | `NotifyRecentFile`: a no-op. |
| `UltraCanvasSpellCheckSupport.cpp` | No native spell checker yet (`UITextChecker` later). |
| `packaging/` | Phase 1: the vcpkg manifest and triplets for `scripts/ios-bootstrap-sysroot.sh`, and `Info.plist.in`, the template an app bundle's `MACOSX_BUNDLE_INFO_PLIST` names. Its `UILaunchScreen` is what makes iOS run the app at the device's own size. |

## Building

On a Mac with Xcode:

```bash
prefix=$(scripts/ios-bootstrap-sysroot.sh simulator | tail -n 1)
```

```bash
PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" cmake -G Ninja -B build-ios -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 -DCMAKE_FIND_ROOT_PATH="$prefix;$(xcrun --sdk iphonesimulator --show-sdk-path)" -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DPKG_CONFIG_ARGN=--static -DCMAKE_BUILD_TYPE=Release
```

```bash
PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig" cmake --build build-ios --target UltraCanvasDemo
```

The iOS branch of `UltraCanvas/CMakeLists.txt` sets these:

- a static core;
- `ULTRACANVAS_IOS`, a public compile definition: what a source tests where
  `__APPLE__` alone would mean macOS;
- GL, audio, video and UltraNet off.

The root `CMakeLists.txt` builds DemoApp only, as `UltraCanvasDemo.app`. Its
`share/` holds the fonts, icons, images and catalogues the pages read.

## Not yet

- **The rest of phase 3:** composition, `UITextInput`'s marked text into
  `TextComposition`, which CJK input methods and autocorrection need.
- **Phase 4:** UltraNet (OpenSSL with the system roots, c-ares), audio, video
  (AVFoundation).
- **Phase 5:** GL surfaces through ANGLE on Metal.
- **Phase 6:** a signed device build.
- **The safe area:** the window covers the whole screen, with the status bar
  and home indicator hidden.
