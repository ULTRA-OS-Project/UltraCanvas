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
    and dylib it packages - the suite's shared `Frameworks/` once, then each
    app's own binaries on top - writes it into each app as
    `LSMinimumSystemVersion` instead of a fixed 12.0, and fails when
    `MACOSX_DEPLOYMENT_TARGET` is set and a binary needs a newer macOS, naming
    each one. The job summary's suite table gains a *Needs macOS* column.
    Unset, as in a local build, each app gets the newest minimum among its
    binaries and the shared ones.
  - Its first run caught a bottle that breaks the rule: Homebrew's arm64
    Sequoia bottle of tesseract declares macOS 15.7.5, the exact system it was
    built on, so every app with the OCR plug-in would have refused to start on
    macOS 15.0 to 15.7.4. The new `scripts/homebrew-rebuild-newer-kegs.sh`
    runs right after `brew install`: it reads the minimum of every library the
    apps link and their dependencies, rebuilds from source each formula with
    one newer than `MACOSX_DEPLOYMENT_TARGET`, and fails within minutes, not
    after the build, if the rebuild does not bring it down. The minimum
    reading is shared with `package-macos.sh` through
    `scripts/macos-min-version.sh`.
  - Going below macOS 15 needs the dependencies built for the older macOS:
    Homebrew 7.0 (September 2026) builds no more bottles for macOS 14 or for
    Intel, and GitHub retires the `macos-14` runner on 2 November 2026.
