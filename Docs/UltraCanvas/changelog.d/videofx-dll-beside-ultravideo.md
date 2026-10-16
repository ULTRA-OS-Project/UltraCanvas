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
- **The Windows package has the video engine.** The Windows CI rows install
  MSYS2's FFmpeg (`-ffmpeg`, FFmpeg 9), so VideoFX there is built on it
  rather than from its stub, and the package's UltraVideo can open, play and
  export; `package-win.sh` ships FFmpeg's DLLs with the rest. That includes
  the Khronos Vulkan loader, `vulkan-1.dll` (and on ARM64 `OpenCL.dll`):
  MSYS2's libavfilter links libplacebo, which links it, so without it
  UltraVideo would not start on a PC without a GPU driver. Its system name
  is allowed by name (`is_khronos_loader`), being the very library a driver
  installs under it; every other system name still fails the package. MSYS2 builds
  FFmpeg with `--enable-gpl --enable-version3` (libx264, libx265), so
  libVideoFX and UltraVideo in the Windows package are GPL-3.0-or-later
  (`THIRD_PARTY_LICENSES.md`). The Windows rows also run `VideoFXTest`
  (`ULTRACANVAS_BUILD_VIDEOFX_TESTS`, `Tests/VideoFXTests.cmake`) after
  checking that the engine, not the stub, was configured.
- **VideoFX runs on FFmpeg 9** (the one MSYS2 ships, and the Windows package
  now). A trim failed there with "Cannot feed the audio effect chain: End of
  file": FFmpeg 9 closes a filter graph's buffer source once the filters
  behind it are done - a trim past its end - and refuses every later frame
  with `AVERROR_EOF`, where older versions took the frame and dropped it, and
  VideoFX decodes on past a cut's end. Every frame now reaches a graph
  through `FeedBufferSource` (`VideoFXBackend.h`, where the FFmpeg version
  differences live), which takes that answer as success. Supported range:
  FFmpeg 4.4 to 9.x. `VideoFXTest`'s pixel helpers no longer crash the run
  on a frame that failed to extract; the failed step reports itself.
