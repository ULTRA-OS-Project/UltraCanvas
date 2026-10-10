- **The Linux SDK archives are xz.** The SDK is the shared core and its
  libraries, which xz packs about 28% smaller than gzip (the UltraCanvasStart
  package, the same kind of tree, went from 100 MB to 70 MB), so the Linux
  legs pack `UltraCanvas-SDK-Linux-<version>-<arch>.tar.xz` instead of a
  `.tar.gz`; macOS keeps `.tar.gz` and Windows `.zip`. UltraCanvasStart 0.1.3
  names the new extension, and its unpack step read the archive by its
  contents already. `Docs/UltraCanvasSDK.md` 1.3.0 says which archive each
  platform gets.
