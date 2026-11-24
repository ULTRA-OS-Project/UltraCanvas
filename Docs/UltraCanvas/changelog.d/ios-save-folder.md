- **iOS: saving, and choosing a folder, through the document picker.** Neither
  has been compiled or run yet.
  - **`SaveContent`** writes the bytes into a temporary file of the suggested
    name (with the default extension where it has none). The picker's export
    mode then copies it to where the user chooses, and the function returns
    true once it is there.
  - **The shared desktop `SaveContent`** (`core/UltraCanvasFileLoader.cpp`) is
    compiled out for iOS, as it is for Android and WebAssembly. The
    path-returning `SaveFile` stays a stub: nothing outside the sandbox has a
    path the app could write to afterwards.
  - **`SelectFolder`** returns the picked folder's path with its
    security-scoped access started and kept for the run.
  - **The iOS CI job** now fails when the app's screenshot is one colour,
    using `scripts/ios-check-screenshot.py`. It also prints the app's system
    log into the job's own log.
