- **UltraCanvasRemovableDrives: "Safely remove".** A new module
  (`UltraCanvasRemovableDrives.h`) lists the drives a person plugs in - USB
  sticks, external disks, memory cards, never the system's own drive - with
  their mount points, and removes one safely: every volume unmounted after
  the system has written out what it still held, then the drive powered off
  or ejected. A file still open on it is reported in words. Linux through
  UDisks2, Windows through SetupAPI and `CM_Request_Device_Eject` (Windows'
  own "Safely Remove Hardware"; the framework now links `cfgmgr32`), macOS
  through NSWorkspace's eject. Doc: `Docs/UltraCanvas/UltraCanvasRemovableDrives.md`.
- The Linux system-bus helpers the wireless backend used are shared now
  (`OS/Linux/UltraCanvasLinuxSystemBus.h`), one connection for NetworkManager,
  BlueZ and UDisks2.
- On macOS the volumes of one drive are one entry, grouped by
  DiskArbitration's whole disk and named by the drive's vendor and model,
  with its size and bus, as on Linux and Windows (the framework links
  DiskArbitration where the SDK has it).
