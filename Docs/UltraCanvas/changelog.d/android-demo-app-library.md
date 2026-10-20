- **DemoApp builds for Android, as the library an APK carries.** On Android
  the demo is `libUltraCanvasDemo.so` (`add_library(... SHARED)` where the
  desktop has an executable), and its `main()` is `ultracanvas_app_main()`,
  which the framework's `android_main()` calls once the sandbox, the unpacked
  assets and the first surface are ready. The X11 and signal setup in
  `Apps/DemoApp/main.cpp` now say `!defined(__ANDROID__)` too, since bionic
  defines `__linux__`. The packaging template's manifest names the library
  and its Gradle file builds that target alone. No APK has been built yet, so
  it has still not run on a device.
- **Android: an app's library now carries the entry points called into it by
  name.** The core is a static archive, and `ANativeActivity_onCreate` and the
  `Java_*` functions behind `UltraCanvasActivity`'s native methods are
  referenced by nothing in an app, so a plain link left them out. Without
  `ANativeActivity_onCreate` the activity cannot start the app, and
  `UltraCanvasAndroidTextInput.cpp` holds only JNI functions, so the first
  keystroke from the IME would have thrown `UnsatisfiedLinkError`. The core's
  link interface now passes `-u` for each, read from `OS/Android/*.cpp` at
  configure time, so a new native method needs nothing added.
- **The Android build workflow links the demo and checks what it exports.**
  `android-build.yml` builds `UltraCanvasDemo` after the core (the NDK links a
  shared library with `--no-undefined`, so a dependency missing from the
  sysroot's link lists shows here and not in the static core), then fails if
  `libUltraCanvasDemo.so` does not export `ANativeActivity_onCreate` and every
  `Java_*` function. It also runs when `Apps/DemoApp/` changes now.
