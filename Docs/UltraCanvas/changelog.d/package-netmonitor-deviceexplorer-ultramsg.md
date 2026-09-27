- **The macOS and Linux packages now ship UltraNetMonitor, DeviceExplorer and
  `ultramsg`.** CI already built all three on every row, and the Windows
  package already carried them (it takes every `.exe` in the build tree), but
  the macOS and Linux packagers work from a fixed list and never gained them.
  - Linux (`package-linux.sh`): the three join the portable bundle as
    `bin/<name>` with a wrapper launcher beside the other apps.
  - macOS (`package-macos.sh`): `UltraNetMonitor.app` and
    `DeviceExplorer.app` are built, signed and notarized like the Texter and
    Demo bundles, with their own icons. `ultramsg`, the UltraMessage command
    line, is not an app, so it ships as `ultramsg/bin/ultramsg` with its
    dylibs in `ultramsg/Frameworks/`. It is signed and notarized but not
    stapled, because a ticket cannot be stapled to a bare executable;
    Gatekeeper checks it online instead. `--dmg` puts that folder in the disk
    image next to the bundles.
