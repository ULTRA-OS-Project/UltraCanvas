- **A dropped connection no longer loses a macOS release.** The runner's
  network dropped out of `notarytool submit --wait` twice on 2026-10-09
  ("The Internet connection appears to be offline"), once in the suite's
  image (0.9.223) and once in UltraCanvasStart's (0.9.225), each time after
  the upload had succeeded and while Apple was still processing, and each
  time it ended the leg and its release. `package-macos.sh` now keeps the
  submission id and resumes the wait on it (`notarytool wait`, up to six
  times with a pause between) until the status is final; only a submit that
  produced no id at all is submitted again.
