- **CI: the macOS legs keep their caches, and stop holding the macOS runners
  for hours.** On 2026-11-10 a macOS leg that takes half an hour with its
  caches ran two to almost four hours, and pull requests' macOS checks waited
  up to four hours for a runner behind main's release builds. Two causes, both
  in the caches:
  - The arm64 leg's vcpkg fallback key `vcpkg-macos-15-` was also the start of
    the Intel leg's `vcpkg-macos-15-intel-...` keys. With no arm64 cache for a
    changed `MacOS/deps`, it restored the x86_64 libraries, could use none of
    them, and built every library from source (45 minutes). A leg's vcpkg key
    now names its architecture as well as its runner
    (`vcpkg-macos-15-arm64-...`, `vcpkg-macos-15-intel-x86_64-...`), so no
    leg's prefix starts another's; the caches saved under the old key are
    still read for the same `MacOS/deps`.
  - Every run on main saved a new run-numbered ccache for each of its six legs
    (and vcpkg's for each macOS leg) and nothing removed the one it replaced,
    so ten release builds in a day overflowed the repository's 10 GB and the
    caches the next run needed were evicted: both macOS legs found no ccache
    at all (8.5 % of compiles hit). The new `cache-prune.yml` runs
    `scripts/prune_actions_caches.py` after every Build, Wayland, element
    modules and Android run and every six hours: it deletes each run-numbered
    cache but the newest of its kind on its ref, and the caches of closed pull
    requests. Tested in `scripts/tests/test_prune_actions_caches.py`.
