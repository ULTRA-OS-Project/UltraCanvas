- **Android: Java could not call back into C++. Fixed before it ever ran.**
  `UltraCanvasActivity` sends dialog answers, typed text, permission answers
  and display turns to C++ through `native` methods. `NativeActivity` loads
  the app's library with a plain `dlopen`, and Android's runtime only finds
  `native` methods in libraries loaded through `System.loadLibrary`. So every
  such call would have thrown `UnsatisfiedLinkError` on the UI thread,
  crashing the app on the first keystroke, dialog answer or permission
  answer. The fixes:
  - `UltraCanvasActivity` now loads the same library (named by
    `android.app.lib_name`, as `NativeActivity` finds it) through
    `System.loadLibrary` before `NativeActivity` starts.
  - At startup the native side checks the bridge and writes one logcat line,
    `Java->native bridge OK` or `BROKEN`, whatever the build type.
  - `scripts/android-java-check.sh` (run in CI) now fails when a Java
    `native` method has no C++ definition.
