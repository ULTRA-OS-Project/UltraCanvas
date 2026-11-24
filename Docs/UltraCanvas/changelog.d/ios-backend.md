- **iOS phase 2: a UIKit backend, and DemoApp as an app for the simulator.**
  Nothing in it has run yet: `.github/workflows/ios-build.yml` is its first
  build. `UltraCanvas/OS/iOS/` holds:
  - the application and the window. The window is a Cairo image surface
    presented as the root view layer's contents, at
    `traitCollection.displayScale`, and it follows rotation;
  - `main()`. It calls `UIApplicationMain`, and the application's renamed
    `ultracanvas_app_main()` runs once the app has launched;
  - the run loop. The framework's `Run()` pumps the main run loop one turn at
    a time, with a `CFRunLoopSource` wakeup;
  - touches, as touch events and first-finger mouse events with double tap;
  - the clipboard (`UIPasteboard`);
  - message and text-input dialogs (`UIAlertController`, answered
    synchronously);
  - the system languages.

  `UltraCanvas/CMakeLists.txt` selects it for `CMAKE_SYSTEM_NAME=iOS`, with a
  static core and GL, audio, video and UltraNet off. tvOS, visionOS and
  watchOS still stop. It defines `ULTRACANVAS_IOS`, which the core now tests
  where `__APPLE__` alone meant macOS:
  - fontconfig is used on iOS;
  - iOS takes the no-op file-association, host-icon and alias fallbacks;
  - there is no hardware-info or network-monitor backend;
  - `OpenURL` goes through `UIApplication`, since `system()` is unavailable.

  The root `CMakeLists.txt` builds DemoApp alone, as `UltraCanvasDemo.app`
  (Info.plist from `UltraCanvas/OS/iOS/packaging/Info.plist.in`), with its
  resources in the bundle's `share/`. The CI job builds the core and the app
  against the simulator sysroot and launches the app on a simulator.
