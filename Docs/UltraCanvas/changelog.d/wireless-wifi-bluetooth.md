- **UltraCanvasWireless: Wi-Fi and Bluetooth operated from a program.** A
  new module (`UltraCanvasWireless.h`) lists the Wi-Fi networks in reach and
  joins one (asking for the password only when the system has none saved,
  and keeping a new profile only once the network has been joined), leaves
  and forgets networks, and switches the radio; it lists the paired
  Bluetooth devices and the ones in pairing mode nearby, pairs, connects,
  disconnects and removes them, and switches the Bluetooth radio where the
  system allows. Linux through NetworkManager and BlueZ on the system bus,
  Windows through the Native Wifi and Bluetooth APIs, macOS through
  CoreWLAN and IOBluetooth (with the Location Services permission macOS 14
  asks before it names a network); other platforms answer that they have
  none. UltraDesktop's new Wi-Fi and Bluetooth panels are built on it. Doc:
  `Docs/UltraCanvas/UltraCanvasWireless.md`.
- `package-macos.sh` writes `NSBluetoothAlwaysUsageDescription` and
  `NSLocationWhenInUseUsageDescription` (and the location entitlement) for
  the applications that list Bluetooth devices or Wi-Fi networks -
  UltraDesktop.
