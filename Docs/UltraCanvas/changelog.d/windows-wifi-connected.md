- **UltraCanvasHardwareInfo on Windows: a joined Wi-Fi adapter reads as
  joined.** The WLAN service's record for an adapter was looked up under
  its GUID wrapped in a second pair of braces, so none was found, and every
  Wi-Fi adapter came back with an empty record saying "not joined" - no
  SSID, no signal - while it was online. The record is now found with
  braces and case aside; joined or not comes from the WLAN enumeration's
  state, which needs no location access, and an adapter WLAN does not
  report takes its link state. `UltraCanvasDesktopShell::ReadDeviceActivity`
  (`wifiConnected`, `wifiSsid`) reads from it, and so does UltraDesktop's
  Wi-Fi marker.
