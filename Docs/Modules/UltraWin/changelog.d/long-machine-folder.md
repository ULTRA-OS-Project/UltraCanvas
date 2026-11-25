- **The machine starts in a folder of any length.** A Unix socket's address
  holds a path of 107 bytes, and the machine has two sockets in its folder
  (`UltraWinConfig::vmDirectory`): QEMU's control socket `qmp.sock` and
  virtiofsd's `vfs.sock` for the home share. In a folder long enough to pass
  that, QEMU refused the control socket and ended at once -
  `UltraWin_VmStart` answered "QEMU did not answer on QMP" after its whole
  timeout - virtiofsd refused its own, and UltraWin's QMP client refused the
  path as well ("socket path too long").
  - QEMU and virtiofsd make their sockets themselves, so in such a folder
    both are now started **in** it and given the sockets by name
    (`qmp.sock`, `vfs.sock`), which they resolve against it; the disk, the
    ISOs and the answer-file folder are handed to QEMU as absolute paths
    there, whatever the caller gave. In a folder that fits, nothing changes.
  - The QMP client reaches the control socket through its folder
    (`UltraCanvasUnixSocketAddress.h`, the framework's), on every later
    command too: suspend, resume, stop, state.
  - `Tests/UltraWin/test_sharedhome.cpp` boots a real QEMU with the home
    share in a folder of some 150 bytes and drives it through suspend and
    resume; it fails on the code before.
