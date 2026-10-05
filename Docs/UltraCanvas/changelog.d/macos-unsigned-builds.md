- **macOS: a pull request's disk image no longer passes for the release.**
  On macOS 27 the apps of `UCDemo-MacOS-0.9.147-arm64.dmg` were refused with
  *"UltraFiler.app is damaged and can't be opened"*. Two images of that name
  existed: the one built on `main`, signed with the Developer ID, notarized
  and stapled, and one built for a pull request with `--no-sign`, which
  Gatekeeper refuses with exactly that message once a browser has downloaded
  it. Nothing in the file, the volume or the CI artifact said which was
  which. `package-macos.sh --no-sign` now names the image
  `UCDemo-MacOS-<version>-<arch>-unsigned.dmg` and its volume
  "UltraCanvas <version> (unsigned)", and CI names the artifact the same way
  on every run that is not a release. The image carries
  `Unsigned build - read me.txt`: why macOS calls the apps damaged, and that
  `xattr -dr com.apple.quarantine /Applications/UltraCanvas` after copying the
  folder lets them run.
- **`--no-sign` signs ad hoc instead of not at all.** An unsigned arm64 app
  had only the linker's signature on its executable - no sealed `Info.plist`
  or resources - and its dylibs kept whatever `install_name_tool` and `strip`
  had left of theirs, while Apple silicon runs no code without a valid
  signature. Every dylib, plug-in, executable and bundle is now signed with
  `codesign --sign -`, with the same hardened-runtime options and
  entitlements as the release and in the same order, and verified with
  `codesign --verify --strict`, so a bundle that cannot be signed fails the
  pull request instead of the release build on `main`. Ad hoc needs no
  certificate and no `--timestamp`, so it asks Apple nothing and brings back
  none of the network failures that took signing out of pull requests in
  0.8.31.
