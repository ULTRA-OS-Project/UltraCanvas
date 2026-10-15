- **UltraVideo starts on Windows.** It stopped at once with "libVideoFX.dll
  was not found" (German: "libVideoFX.dll nicht gefunden"), from the build
  tree and from the package alike. VideoFX sent its DLL to `bin/`, beside
  `videofx.exe` and `VideoFXTest.exe`, while UltraVideo - like every
  application - is built into the build root, and Windows looks for a DLL
  only in the folder of the executable that loads it (and on `PATH`).
  `package-win.sh` copies the build root's DLLs, so the package had no
  `libVideoFX.dll` at all, and its `videofx.exe` could not start either.
  On Windows the DLL, `videofx.exe` and the VideoFX tests now go where the
  build puts the applications (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`, exported as
  `VIDEOFX_RUNTIME_DIR`); Linux and macOS are unchanged (VideoFX 0.14.1,
  `VideoFX/CMakeLists.txt`, `Tests/CMakeLists.txt`).
- **`package-win.sh` refuses a package missing a DLL of its own build.** Each
  packaged executable's and DLL's imports are read with objdump, and one that
  names a DLL the build made but the package does not hold fails the build
  with the importer and the DLL named. The dependency closure follows only
  the MinGW prefix, so a DLL a CMakeLists sends outside the build root was
  left behind without a word - this is what let `libVideoFX.dll` go missing.
