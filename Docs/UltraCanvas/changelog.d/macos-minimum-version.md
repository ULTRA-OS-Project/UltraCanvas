- **The macOS apps CI published started only on the macOS they were built
  on - for Apple Silicon, macOS 26.** The arm64 leg ran on `macos-latest`,
  which GitHub moved to macOS 26 in July 2026. Nothing set a deployment
  target, so clang compiled our code for the runner's SDK, and the bundled
  Homebrew libraries are built for the macOS of the machine their bottle was
  built on. dyld refuses a binary built for a newer macOS than the running one,
  so on macOS 14 or 15 the apps did not start, while their `Info.plist` still
  said `LSMinimumSystemVersion` 12.0.
  - The arm64 leg runs on `macos-15` now, and both macOS legs build with
    `MACOSX_DEPLOYMENT_TARGET=15.0` (`build.yml`), which CMake, clang and
    cargo all read. The published apps run on macOS 15 and later, on both
    Apple Silicon and Intel.
  - `package-macos.sh` reads the minimum macOS from every executable, plug-in
    and dylib it bundles, writes it as `LSMinimumSystemVersion` instead of a
    fixed 12.0, and fails when `MACOSX_DEPLOYMENT_TARGET` is set and a binary
    needs a newer macOS - naming each one. The job summary's bundle table
    gains a *Needs macOS* column. Unset, as in a local build, the bundle gets
    the newest minimum among its binaries.
  - Going below macOS 15 needs the dependencies built for the older macOS:
    Homebrew 7.0 (September 2026) builds no more bottles for macOS 14 or for
    Intel, and GitHub retires the `macos-14` runner on 2 November 2026.
