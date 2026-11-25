- **CI: fewer runners per pull request and per release.** On 2026-11-24 the
  macOS runners were the bottleneck: one release run's Apple silicon row
  waited 161 minutes to start and a pull request's iOS run 98.
  - **The macOS Intel row is built only when asked for**: a pull request
    labelled `ci:intel`, or a Build dispatch with the new `intel` input. Main
    and `ci:distro` no longer build it. It took 88 to 198 minutes a run, and
    full runs 150 to 300. A release now carries five SDK and UltraCanvasStart
    archives, without the x86_64 macOS ones, unless it is dispatched with
    `intel` set.
  - **No nightly Build run.** It built every row at 02:23 UTC; `build.yml`'s
    `on:` says how to turn it back on.
  - **iOS and WebAssembly build on main only**, not on pull requests. A pull
    request that changes either backend starts them by hand against its
    branch.
  - **ccache holds a whole build again.** The cap stayed at 500 MB from when
    a leg compiled about 1,136 objects. The Linux row now compiles 3,378 and
    filled the cache to 0.39 of 0.50 GB, the level ccache trims to after an
    eviction. That evicted objects the build had not reached yet, so they
    missed too. The cap is 800 MB, and the statistics step prints evictions.
