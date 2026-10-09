- **The SDKs are release assets, and the Windows and macOS bundles are
  smaller.** `.github/workflows/build.yml` gains `publish-sdk`: every release
  build of `main` (the dispatch `changelog-fold.yml` sends for its version
  commit) creates the GitHub release `v<version>`, with the version's
  changelog section as its notes, and attaches the six
  `UltraCanvas-SDK-<OS>-<version>-<arch>` archives, so
  `releases/download/v<version>/<archive>` is a fixed address anyone can
  fetch (`Docs/UltraCanvasSDK.md`). `scripts/sdk-bundle-deps.sh` now tells
  the public pkg-config closure (`Requires`) from the packages reached only
  through `Requires.private`: the former come whole, the latter contribute
  their `.pc` files alone, and a static archive with a DLL or dylib twin is
  left out; the run-time DLLs still come from the core's import-table walk.
