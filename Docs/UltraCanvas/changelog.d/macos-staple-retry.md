- **macOS release builds: the notarization ticket is stapled with retries.**
  Every release build from 0.9.253 to 0.9.260 failed at `xcrun stapler
  staple` with "The network connection was lost" from Apple's CloudKit
  ticket service (exit 68), each after a notarization Apple had already
  accepted, and in each run some bundles stapled and a later one did not.
  No release carried those versions. `package-macos.sh` now tries the
  staple up to `STAPLE_ATTEMPTS` times (default 5), 20 s apart, and
  validates once it has worked, the way it already resumes the notary wait;
  only a staple that fails every time ends the run.
