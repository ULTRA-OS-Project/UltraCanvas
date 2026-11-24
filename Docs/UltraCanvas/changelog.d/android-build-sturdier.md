- **The Android sysroot survives GNU's download hosts being down.** vcpkg
  fetches gperf, libiconv and gettext only from ftpmirror.gnu.org and
  ftp.gnu.org; when both timed out the whole arm64 sysroot failed after half
  an hour. `scripts/android-bootstrap-sysroot.sh` now has
  `scripts/vcpkg_seed_gnu_sources.py` put the GNU archives the manifest needs
  into vcpkg's download cache first, from other GNU mirrors, each checked
  against the port's SHA512; vcpkg takes them from there.
- **`scripts/android-package-apk.sh` packages with build-tools 34 on JDK 21.**
  JDK 21's javac writes unnamed `MethodParameters` entries for anonymous
  classes, on which build-tools 34's d8 stopped with a NullPointerException.
  When d8 refuses the classes, `scripts/strip_method_parameters.py` takes that
  attribute out of them (it only names parameters for reflection) and d8 runs
  again.
- **The Android documents say what has run.** `AndroidPortInvestigation.md`
  and the backend's README still said no APK had been installed; the
  emulator jobs install and run the demo and MobilePC, and only a physical
  device is still to come.
