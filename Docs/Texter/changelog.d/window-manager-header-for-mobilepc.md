- **UltraTexter runs inside MobilePC.** Its window manager - every editor
  window, New Window, tabs dragged out, the files a second start hands over -
  moved unchanged out of `main.cpp` into `TexterWindowManager.h`, which
  MobilePC starts Texter with on Android. Built for Android, the file
  statistics show a file's creation time as *Not available* (Android has no
  `statx` before API 30).
