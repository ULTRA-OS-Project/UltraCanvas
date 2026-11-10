- **`ReadVolumeName(path)`: a drive letter's own volume name.** The name a
  Windows volume carries ("My Passport Ultra"), for showing a drive as
  Explorer does. It reads the medium, so it is a call of its own for a
  worker thread, not part of `ListMountedVolumes()`; empty for a network
  drive, a drive with no medium, and on Linux and macOS, where
  `MountedVolume::label` already is the name (`UltraCanvasVolumeMonitor.h`
  1.2.0).
