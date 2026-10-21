- **`HardwareQuery::None` is now `HardwareQuery::Nothing`.** X11's headers
  `#define None`, so any file that included a UI header (which reaches X11 on
  Linux and BSD) before `UltraCanvasHardwareInfo.h` failed to compile. UI code
  can now include the header in any order; DeviceExplorer, UltraNetMonitor and
  UltraDesktop's Task Manager no longer have to include it first. Callers that
  spelled `HardwareQuery::None` write `HardwareQuery::Nothing`.
