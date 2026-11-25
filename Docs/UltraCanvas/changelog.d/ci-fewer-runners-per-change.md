- **CI: fewer runners per pull request and per release.** On 2026-11-24 the
  macOS runners were the bottleneck: one release run's Apple silicon row
  waited 161 minutes to start and a pull request's iOS run 98.
  - **The macOS Intel row is built only when asked for**: a pull request
    labelled `ci:intel`, or a Build dispatch with the new `intel` input. Main
    and `ci:distro` no longer build it. It took 88 to 198 minutes a run, and
    full runs 150 to 300. A release no longer carries the x86_64 macOS SDK
    and UltraCanvasStart unless it is dispatched with `intel` set.
  - **No nightly Build run.** It built every row at 02:23 UTC; `build.yml`'s
    `on:` says how to turn it back on.
  - **iOS and WebAssembly build on main only**, not on pull requests. A pull
    request that changes either backend starts them by hand against its
    branch.
  - **Android: a pull request builds arm64-v8a alone**, 1 job instead of 7.
    The x86_64 build, ChatCube's two builds and the three emulator jobs run
    on main (a push, the weekly run, a dispatch).
  - **Windows ARM builds on release dispatches**, on pull requests labelled
    `ci:distro` and on a dispatch with the new `windows_arm` input, but not
    on a plain push to main. It took 96 to 102 minutes a run. Outside a
    release build it skips its installer (29 to 32 minutes), the MSIX,
    UltraCanvasStart and the store packages.
  - **ccache holds a whole build again.** The cap stayed at 500 MB from when
    a leg compiled about 1,136 objects. The Linux row now compiles 3,378 and
    filled the cache to 0.39 of 0.50 GB, the level ccache trims to after an
    eviction. That evicted objects the build had not reached yet, so they
    missed too. The cap is 800 MB, and the statistics step prints evictions.
