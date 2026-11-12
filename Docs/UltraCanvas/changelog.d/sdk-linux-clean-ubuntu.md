- **The Linux SDK is tested and finished on a clean Ubuntu 22.04, the way a
  user meets it.** The CMake package test runs on the runner that built the
  SDK, where every library the framework uses is installed, so it could not
  see what a user's machine lacks.
  - **New CI job.** `sdk-linux` in `.github/workflows/build.yml`, for
    x86_64 and arm64, runs `scripts/sdk-linux-clean-consumer.sh` in a
    `ubuntu:22.04` container that has only a desktop's libraries (those
    `package-linux.sh` leaves to the host):
    1. The standalone UltraCanvasStart of the same build runs `--plan`.
       The job runs the install command it prints, and `--check` must then
       find nothing missing.
    2. The SDK gets the run-time libraries its core still cannot load there.
    3. UltraCanvasStart's skeleton is built with the Build step's two
       commands, and `PackageConsumer` is built and run.

    The SDK it packs replaces the build leg's artifact, and `publish-sdk`
    waits for it, so the release publishes the tested one.
  - **What it found at once.**
    - The 0.9.465 SDK stopped with "No rule to make target
      '/usr/lib/x86_64-linux-gnu/libmp3lame.so'" (fixed by #1093).
    - With that fixed, the link stopped at `-lFLAC`, `-larchive`, GStreamer,
      Vorbis, Opus, `-lzbar` and `-lrevenge-0.0`.
    - The core could not load 31 libraries, such as `libmp3lame.so.0` and
      `libavcodec.so.58`, 63 with their own dependencies.
  - **The link line.** For a shared core without `deps/` (the Linux SDK),
    `UltraCanvasConfig.cmake` keeps on the application's link line only the
    libraries the public headers include, which pkg-config re-finds, plus
    imported targets, generator expressions and flags. The core's own 35
    are left out, and the configure output says how many. VirtualFS is
    folded into the core, so `VirtualFS::VirtualFS` gives way to
    `UltraCanvas::VirtualFSHeaders`, its include directories and definitions
    without `libVirtualFS.a` and `-larchive`.
  - **Run time.** The Linux SDK carries the core's run-time libraries that a
    22.04 machine set up by UltraCanvasStart lacks, beside the core in
    `lib/`. They come from the standalone UltraCanvasStart's `lib/`, which
    holds the core's whole closure. They and the core carry an `$ORIGIN`
    runpath, so an application starts without `LD_LIBRARY_PATH`. The x86_64
    archive grows from about 61 MB to 82 MB.
  - **Checked** by running the script in a clean Ubuntu 22.04 root against
    the 0.9.465 SDK and standalone UltraCanvasStart, with the new
    `UltraCanvasConfig.cmake` sections applied:
    - without `librevenge-dev`, the skeleton stopped only at
      `-lrevenge-0.0`, which UltraCanvasStart now installs (its changelog);
    - with it, the skeleton built with no library missing, and
      `PackageConsumer` built and ran without `LD_LIBRARY_PATH`.
