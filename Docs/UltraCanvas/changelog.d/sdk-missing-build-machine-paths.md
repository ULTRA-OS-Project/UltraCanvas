- **An application built against the Linux SDK stopped with "No rule to make
  target '/usr/lib/x86_64-linux-gnu/libmp3lame.so'".**
  - **Cause:** the core links LAME, MuPDF and the libraries MuPDF uses by the
    full path CI found them at, and the installed package handed those paths
    on to every application linking `UltraCanvas::UltraCanvas`.
    `/usr/lib/x86_64-linux-gnu/libmp3lame.so` exists only where
    `libmp3lame-dev` is installed. `/usr/lib/libmupdf.a` exists only where
    MuPDF was built from source, as on CI. So a machine with only the runtime
    libraries could not build UltraCanvasStart's skeleton.
  - **Fix:** the core is a shared library, so those are already inside it or
    loaded with it at run time. `UltraCanvasConfig.cmake` now leaves any such
    path that the consuming machine does not have off a shared core's link
    line, and names them in the configure output. Paths that exist, and the
    libraries named by `-l` name, stay as they were. On Windows and macOS the
    `deps/` remapping still comes first.
  - **Reproduced** with the 0.9.465 Linux SDK and a skeleton from
    `UltraCanvasStart --create`, with `libmp3lame.so` and `libmupdf.a`
    moved aside: the old package stopped with that error.
  - **Fixed:** the same package with this change configured, named both
    files, linked, and the skeleton started and showed its window. With both
    files back in place, nothing is left out and the build is unchanged.
