- **macOS release builds: the notarization ticket is stapled and validated
  with retries.** Every release build from 0.9.253 to 0.9.265 failed at
  `xcrun stapler` with "The network connection was lost" from Apple's
  CloudKit ticket service (exit 68), each after a notarization Apple had
  already accepted; in each run some bundles stapled and a later one did
  not, and once a staple went through and the validate right after it did
  not. No release carried those thirteen versions. `package-macos.sh` now
  tries the staple and then the validate up to `STAPLE_ATTEMPTS` times
  (default 5), 20 s apart, the way it already resumes the notary wait; a
  staple that worked is not repeated, only its validate, and only a lookup
  that fails every time ends the run.
