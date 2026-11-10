- **An external USB disk or stick was listed as a portable device too.**
  Windows exposes a mass-storage drive through Windows Portable Devices as
  well as with its drive letter, so `UltraCanvasPortableDevices::List()`
  returned it next to the phones and cameras. The WPD backend now leaves out
  a device whose id is a USBSTOR / `STORAGE#Volume` one or whose protocol is
  `MSC:` - it is a drive, and the volume list already has it
  (`UltraCanvasWindowsPortableDevices.cpp` 1.2.0).
