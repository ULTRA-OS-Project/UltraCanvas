- **VirtualFS asks the system once which RAM disc backing it has.**
  `VirtualFS_GetPreferredRamDiskBacking()` (and
  `VirtualFS_IsTrueRamDiskAvailable()` over it) launched a shell on every
  call - `where imdisk` on Windows, `command -v hdiutil` and `diskutil` on
  macOS - which takes seconds on a loaded machine, and UltraFiler asked on
  the UI thread every time its *+ Drive* menu opened. The answer is now
  probed once per process, on the first call from any thread, and kept; a
  caller that arrives while another thread is still probing waits for that
  answer rather than probing again, so an application can pay for the probe
  on a background thread at start-up. The Windows and macOS back ends read
  the kept answer when creating a disc instead of probing a second time. The
  platform contract gains `RamDiskDetail::PlatformPreferredBacking()`, the
  raw probe the core calls once. `VirtualFSRamDiskTest` checks that the
  second answer is the kept one (UltraFiler changelog 1.72.6).
