- **A RAM disc's name fits its volume label on Windows.** A drive letter
  carries no name, so an ImDisk disc keeps "ultravfs-<name>" in its NTFS
  volume label - the only way `VirtualFS_ListRamDisks()` and the duplicate
  check find it again. A label holds 32 characters, but names of up to 64
  were accepted: a name over 23 characters could not be stamped on the
  volume, and the disc was then neither found by its name nor listed, so
  nothing in VirtualFS could eject it again. Names are now limited to 23 characters on Windows
  (still 64 elsewhere), for the `%TEMP%` fallback too, so a name works
  whichever backing a machine has. The limit is computed from the prefix,
  and `VirtualFS_GetMaxRamDiskNameLength()` reports it. Each back end now
  states its own limit (`PlatformMaxNameLength`), and `IsValidName` applies
  it. `VirtualFSRamDiskTest` checks the limit, rejects a name one character
  longer, and makes and lists a disc with a name of exactly that length -
  also built for Windows and run under Wine, through the fallback.
