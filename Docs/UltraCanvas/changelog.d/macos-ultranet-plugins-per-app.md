- **DeviceExplorer finds network printers and eSCL scanners on macOS.** Its
  bundle carried no UltraNet plug-in, so the DNS-SD browse behind both - the
  `mdns` plug-in, loaded by IODeviceManager's IPP and eSCL enumeration -
  found nothing there, while Linux and Windows give every app every plug-in
  from one shared folder. `package-macos.sh` names the plug-ins each app
  loads (`ultranet_plugins_of`: UltraMail all of them, DeviceExplorer
  `mdns`) and puts only those into its `Contents/PlugIns/UltraNet/`; a
  plug-in named and not built fails the packaging. UltraNetMonitor loads
  none - it reads the system's socket table - and stays as it was;
  `package-linux.sh`'s comment that named it among the plug-ins' users is
  corrected.
- **`scripts/check_libcxx_syntax.py` takes a build outside the checkout.**
  `--db` pointing there stopped with a `ValueError` (a message named the
  folder relative to the checkout). A build configured without the database
  was configured again under the path as it was given, so one reached
  through a symbolic link had the link's path recorded as its home and no
  longer built from its own folder. It is now configured under the folders
  its own `CMakeCache.txt` names; a build whose cache names another folder
  (moved, or re-homed) is refused with what to do, instead of compiles
  started in a folder that is not there; a database the cache promises and
  the folder lacks is said, not a traceback; and the sources the checked
  build generated are left out wherever it is, not only under `build/`.
  `scripts/tests/test_check_libcxx_syntax.py` holds each case.
