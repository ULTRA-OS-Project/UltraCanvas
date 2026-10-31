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
- **Pairing codes.** `UltraCanvasWireless::SetPairingHandler` takes the codes
  a device shows or asks for while it pairs - a code to type on a keyboard,
  one to compare with a phone, a PIN, a yes to pair. On Linux the module
  registers a BlueZ agent for them on a GLib loop of its own; on macOS
  IOBluetoothDevicePair's requests go to the handler; Windows keeps its own
  prompt.
- **The default pairing agent on Linux.** Setting a pairing handler now
  registers the module's BlueZ agent straight away and as the default, so a
  pairing the device starts - a phone pairing with the computer - reaches
  the handler as well, and so does a connection from a device that is not
  trusted (`WirelessPairingStep::AllowConnection`); a service of a device
  being paired from here, or of a trusted one, is allowed without asking.
  When BlueZ was not running yet, the agent is registered on a later read.
