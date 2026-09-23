- **The dependency tables now list libudev.** IODeviceManager's Linux
  hot-plug watcher links libudev when the configure step finds it, and without
  it `StartMonitoring()` returns `BackendUnavailable`. Nothing said so outside
  `UltraCanvas/CMakeLists.txt`. `Docs/Dependencies.md` and the DemoApp's
  in-app copy (`UltraCanvasDependenciesExamples.cpp`) gain a *Hot-plug
  watching* row: libudev (optional) on Linux, no watcher yet on macOS or
  Windows. libudev is also added to the library-links table (LGPL 2.1, part of
  systemd). Its effect on DeviceExplorer is documented in that app's own docs.
