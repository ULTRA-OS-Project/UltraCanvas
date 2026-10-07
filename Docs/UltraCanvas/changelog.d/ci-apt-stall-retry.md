- **CI no longer waits an hour on a stalled Ubuntu mirror.** On 2026-10-07 an
  `apt-get update` in the Android check sat on one download for fifty minutes
  without receiving a byte, and the x64 Linux build spent half an hour
  installing packages from a mirror sending about 30 KB/s; both runs were
  cancelled by hand and passed on re-run in seconds. apt's own timeouts never
  fire on such connections, so every apt call in the workflows now goes through
  `scripts/ci-apt.sh`. It watches what apt has downloaded and, after two
  minutes without a byte, stops it and starts again on a fresh connection, up
  to three times (apt resumes the partial files). A mirror that is merely slow
  is left alone, since the same day's slow installs (15 to 27 minutes) did
  finish. Only downloads are ever interrupted: packages are fetched first and
  then installed from the local cache, so dpkg is never killed mid-unpack.
  - Every install step also has its own time limit, well above its slowest
    run in the last two days, instead of only the job's 2-hour (5-hour on
    macOS) limit: Linux dependencies 45 minutes, MSYS2 30, the Android
    check's headers and the WebAssembly host tools 20, Clang and Homebrew 15,
    Rust 10, the Python websockets package 5.
