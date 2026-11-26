- **The Framework step offers the newest SDK, and moves a project to it.**
  Until now a build of UltraCanvasStart only knew the SDK of its own version.
  The first visit to step 4 now asks the server for `pub/LATEST`, the
  newest version, which the upload writes after every release (framework
  changelog: `sdk-website-latest`). A *Newest SDK* row compares it with the
  SDK in the prefix field (the version in its
  `UltraCanvasConfigVersion.cmake`, or the folder's name), or with this
  build's version when no prefix is set:
  - when it is newer and its `SHA256SUMS` lists this machine's archive,
    *Download <version>* fetches, checks and unpacks it like *Download*, and
    the UltraCanvasStart of that release is linked, because its Tools step
    knows what the newer SDK needs;
  - when that release has no archive for this machine, the row says so;
  - otherwise it says the SDK in use is the newest. *Check again* asks
    again.

  *Move a project...* points an existing project at the SDK in the prefix
  field. It sets `CMAKE_PREFIX_PATH` in the project's `CMakePresets.json` (or
  adds it), writes the file beside the old one and renames it over it, and
  removes `build/CMakeCache.txt`, where `find_package` kept the old SDK's
  folder. On the command line: `--update-check` (exit 3 when a newer SDK can
  be downloaded), `--update-sdk <folder>` and
  `--update-project <folder> --sdk <prefix>`. A new project's README says
  how. (`StartSdk` 0.4.0: `CheckSdkUpdate`, `ParseLatestVersion`,
  `SdkPrefixVersion`, `StartAppDownloadUrl`; `StartProject` 0.2.0:
  `RepointPresets`, `RepointProjectSdk`; tests
  `ParseLatestVersion_reads_the_first_line`,
  `RepointProjectSdk_rewrites_the_presets_and_drops_the_cache` and five
  more.)
