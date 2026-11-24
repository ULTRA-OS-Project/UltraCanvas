- **iOS: a clear stop instead of a missing file, and the port investigated.**
  An iOS compile reached `#include "../OS/iOS/..."` in `UltraCanvasWindow.h`
  and `UltraCanvasApplication.h`, a directory that does not exist; both
  branches are now `#error "iOS backend not implemented yet"`. An iOS
  configure (`-DCMAKE_SYSTEM_NAME=iOS`, likewise tvOS, visionOS and watchOS)
  satisfied `APPLE` and quietly picked the AppKit backend;
  `UltraCanvas/CMakeLists.txt` now tests those four first and stops with a
  message. The headers' last-resort `#error` no longer lists iOS as
  supported. The two stops name the new
  `Docs/UltraCanvas/iOSPortInvestigation.md`: the backend's parts list, the
  run loop under `UIApplicationMain`, building the dependencies with vcpkg's
  iOS triplets, the GL / Metal decision (GL off first, ANGLE on Metal later)
  and the CMake branch a port needs.
